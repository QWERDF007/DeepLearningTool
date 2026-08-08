#include "data/Images.h"

#include "data/DataViewModels.h"
#include "data/Datasets.h"
#include "data/LabelClasses.h"
#include "data/Labels.h"
#include "database/DataBase.h"

#include <data/DataFormat.h>
#include <spdlog/spdlog.h>

#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <unordered_set>

namespace dltool::data {

namespace {

constexpr const char *kImageLabelClassIdKey = "image_label_class_id";

int64_t readImageLabelClassId(const std::vector<uint8_t> &blob)
{
    if (blob.empty())
    {
        return -1;
    }

    const QByteArray bytes(reinterpret_cast<const char *>(blob.data()), static_cast<qsizetype>(blob.size()));
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        return -1;
    }
    return document.object().value(QString::fromUtf8(kImageLabelClassIdKey)).toInteger(-1);
}

std::vector<uint8_t> makeImageLabelClassExtraData(const int64_t label_class_id)
{
    QJsonObject object;
    if (label_class_id >= 0)
    {
        object.insert(QString::fromUtf8(kImageLabelClassIdKey), static_cast<qint64>(label_class_id));
    }

    const QByteArray json = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return {json.cbegin(), json.cend()};
}

} // namespace

ImageInstance::ImageInstance(const int64_t dataset_id, const int64_t image_id, const QString &path,
                             const int64_t image_label_class_id, QObject *parent)
    : QObject(parent)
    , dataset_id_(dataset_id)
    , image_id_(image_id)
    , path_(path)
    , name_(QFileInfo(path).fileName())
    , image_label_class_id_(image_label_class_id)
{
}

ImageInstance::~ImageInstance() = default;

QSize ImageInstance::imageSize() const
{
    if (image_rect_.isEmpty())
    {
        QImageReader reader(path_);
        image_size_ = reader.size();
        image_rect_ = QRectF(0, 0, image_size_.width(), image_size_.height());
    }
    return image_size_;
}

QRectF ImageInstance::imageRect() const
{
    if (image_rect_.isEmpty())
    {
        QImageReader reader(path_);
        image_size_ = reader.size();
        image_rect_ = QRectF(0, 0, image_size_.width(), image_size_.height());
    }
    return image_rect_;
}

ImageInstancesListModel::ImageInstancesListModel(dltool::database::ProjectDataBase *database, QObject *parent)
    : QAbstractListModel(parent)
    , database_(database)
{
    init();
}

ImageInstancesListModel::~ImageInstancesListModel()
{
    prefetch_cancel_.store(true, std::memory_order_relaxed);
    if (prefetch_thread_.joinable())
        prefetch_thread_.join();
}

void ImageInstancesListModel::startSizePrefetch()
{
    if (prefetch_running_.exchange(true))
        return;
    std::vector<std::pair<int64_t, QString>> targets;
    targets.reserve(full_image_instances_.size());
    for (const auto &entry : full_image_instances_)
        targets.emplace_back(entry.first, entry.second->path());
    prefetch_thread_ = std::thread([this, targets = std::move(targets)]() mutable
                                   { prefetchSizes(std::move(targets)); });
}

void ImageInstancesListModel::prefetchSizes(std::vector<std::pair<int64_t, QString>> targets)
{
    for (const auto &[image_id, path] : targets)
    {
        if (prefetch_cancel_.load(std::memory_order_relaxed))
            break;
        QImageReader reader(path);
        const QSize  size = reader.size();
        if (!size.isValid() || size.width() <= 0 || size.height() <= 0)
            continue;
        QWriteLocker locker(&size_lock_);
        image_sizes_.insert(image_id, size);
    }
    prefetch_running_.store(false, std::memory_order_relaxed);
}

QSize ImageInstancesListModel::imageSize(const int64_t image_id) const
{
    {
        QReadLocker locker(&size_lock_);
        const auto  it = image_sizes_.constFind(image_id);
        if (it != image_sizes_.cend())
            return it.value();
    }
    const ImageInstance *instance = getImageInstance(image_id);
    if (instance == nullptr)
        return {};
    const QSize size = instance->imageSize();
    if (size.isValid())
    {
        QWriteLocker locker(&size_lock_);
        image_sizes_.insert(image_id, size);
    }
    return size;
}

void ImageInstancesListModel::init()
{
    if (database_ == nullptr)
    {
        spdlog::error("初始化图像失败: 数据库未初始化");
        return;
    }

    QString err_msg;
    std::vector<int64_t> dataset_ids;
    std::vector<int64_t> image_ids;
    std::vector<QString> paths;
    std::vector<std::vector<uint8_t>> extra_data;
    if (!database_->getAllImages(dataset_ids, image_ids, paths, extra_data, err_msg))
    {
        spdlog::error("初始化图像失败: {}", err_msg.toUtf8().constData());
        return;
    }

    const size_t count = std::min({dataset_ids.size(), image_ids.size(), paths.size()});
    for (size_t index = 0; index < count; ++index)
    {
        full_image_instances_.emplace(
            image_ids[index],
            new ImageInstance(dataset_ids[index], image_ids[index], paths[index],
                              index < extra_data.size() ? readImageLabelClassId(extra_data[index]) : -1, this));
    }
    rebuildImageIds();
    // 打开项目后立即在后台预取全部图像尺寸,供评估等线程复用,避免重复读文件。
    startSizePrefetch();
}

int ImageInstancesListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(image_ids_.size());
}

QVariant ImageInstancesListModel::data(const QModelIndex &index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
    {
        return {};
    }

    switch (role)
    {
    case ImageIdRole:
        return static_cast<qlonglong>(getImageId(index));
    case NameRole:
        return getImageName(index);
    case PathRole:
        return getImagePath(index);
    case HasLabelsRole:
        return getHasLabels(index);
    case ImageLabelClassIdRole:
        return getImageLabelClassId(index);
    case DatasetIdRole:
        return static_cast<qlonglong>(getImageDatasetId(getImageId(index)));
    default:
        return {};
    }
}

QHash<int, QByteArray> ImageInstancesListModel::roleNames() const
{
    return {
        {ImageIdRole, "image_id"},
        {NameRole, "name"},
        {PathRole, "path"},
        {HasLabelsRole, "hasLabels"},
        {ImageLabelClassIdRole, "image_label_class_id"},
        {DatasetIdRole, "dataset_id"},
    };
}

bool ImageInstancesListModel::removeRows(const int row, const int count, const QModelIndex &parent)
{
    if (parent.isValid() || row < 0 || count <= 0 || row + count > rowCount())
    {
        return false;
    }

    beginRemoveRows(parent, row, row + count - 1);
    for (int offset = 0; offset < count; ++offset)
    {
        const int64_t image_id = image_ids_[static_cast<size_t>(row + offset)];
        const auto found = full_image_instances_.find(image_id);
        if (found != full_image_instances_.end())
        {
            delete found->second;
            full_image_instances_.erase(found);
        }
    }
    image_ids_.erase(image_ids_.begin() + row, image_ids_.begin() + row + count);
    endRemoveRows();
    return true;
}

bool ImageInstancesListModel::addImages(const int64_t dataset_id, const std::vector<QString> &paths,
                                         std::vector<int64_t> &image_ids, const bool defer_model_update)
{
    if (database_ == nullptr)
    {
        spdlog::error("批量添加图像失败, dataset id: {}, 数据库未初始化", dataset_id);
        return false;
    }
    if (paths.empty())
    {
        image_ids.clear();
        return true;
    }

    QString err_msg;
    if (!database_->addImages(dataset_id, paths, image_ids, err_msg))
    {
        spdlog::error("批量添加图像失败, dataset id: {}, error: {}", dataset_id, err_msg.toUtf8().constData());
        return false;
    }
    if (image_ids.size() != paths.size())
    {
        spdlog::error("批量添加图像失败: 返回图像 ID 数量不一致");
        return false;
    }
    addImagesFromMemory(dataset_id, paths, image_ids, defer_model_update);
    return true;
}

bool ImageInstancesListModel::addImages(const std::vector<int64_t> &dataset_ids, const std::vector<QString> &paths,
                                         std::vector<int64_t> &image_ids, const bool defer_model_update)
{
    if (database_ == nullptr)
    {
        spdlog::error("批量添加图像失败, 数量: {}, 数据库未初始化", paths.size());
        return false;
    }
    if (dataset_ids.size() != paths.size())
    {
        spdlog::error("批量添加图像失败: 数据集 ID 和路径数量不一致");
        return false;
    }
    if (paths.empty())
    {
        image_ids.clear();
        return true;
    }

    QString err_msg;
    if (!database_->addImages(dataset_ids, paths, image_ids, err_msg))
    {
        spdlog::error("批量添加图像失败, 数量: {}, error: {}", paths.size(), err_msg.toUtf8().constData());
        return false;
    }
    if (image_ids.size() != paths.size())
    {
        spdlog::error("批量添加图像失败: 返回图像 ID 数量不一致");
        return false;
    }
    addImagesFromMemory(dataset_ids, paths, image_ids, defer_model_update);
    return true;
}

bool ImageInstancesListModel::addImages(const int64_t dataset_id, const QString &image_dir,
                                         std::vector<int64_t> &image_ids)
{
    return addImages(dataset_id, getImagePaths(image_dir), image_ids);
}

void ImageInstancesListModel::addImagesFromMemory(const int64_t dataset_id, const std::vector<QString> &paths,
                                                   const std::vector<int64_t> &image_ids,
                                                   const bool defer_model_update)
{
    if (paths.size() != image_ids.size())
    {
        spdlog::error("批量发布图像失败: 路径和图像 ID 数量不一致");
        return;
    }
    if (image_ids.empty())
    {
        return;
    }

    for (size_t index = 0; index < image_ids.size(); ++index)
    {
        if (image_ids[index] < 0 || full_image_instances_.contains(image_ids[index]))
        {
            continue;
        }
        full_image_instances_.emplace(image_ids[index],
                                      new ImageInstance(dataset_id, image_ids[index], paths[index], -1, this));
    }
    if (!defer_model_update)
    {
        publishPendingImages();
    }
}

void ImageInstancesListModel::addImagesFromMemory(const std::vector<int64_t> &dataset_ids,
                                                   const std::vector<QString> &paths,
                                                   const std::vector<int64_t> &image_ids,
                                                   const bool defer_model_update)
{
    if (dataset_ids.size() != paths.size() || image_ids.size() != paths.size())
    {
        spdlog::error("批量发布图像失败: 数据集、路径和图像 ID 数量不一致");
        return;
    }
    if (image_ids.empty())
    {
        return;
    }

    for (size_t index = 0; index < image_ids.size(); ++index)
    {
        if (image_ids[index] < 0 || full_image_instances_.contains(image_ids[index]))
        {
            continue;
        }
        full_image_instances_.emplace(
            image_ids[index], new ImageInstance(dataset_ids[index], image_ids[index], paths[index], -1, this));
    }
    if (!defer_model_update)
    {
        publishPendingImages();
    }
}

void ImageInstancesListModel::addImagesFromMemory(std::vector<LoadedImageInstance> &images,
                                                   const bool defer_model_update)
{
    if (images.empty())
    {
        return;
    }

    for (LoadedImageInstance &loaded : images)
    {
        if (loaded.image_id < 0 || loaded.path.isEmpty() || full_image_instances_.contains(loaded.image_id))
        {
            continue;
        }

        auto *image = new ImageInstance(loaded.dataset_id, loaded.image_id, std::move(loaded.path),
                                        loaded.label_class_id, this);
        for (const int64_t tag_id : loaded.tag_ids)
        {
            image->addTagId(tag_id);
        }
        full_image_instances_.emplace(loaded.image_id, image);
    }
    if (!defer_model_update)
    {
        publishPendingImages();
    }
}

void ImageInstancesListModel::refreshModelFromMemory()
{
    publishPendingImages();
}

void ImageInstancesListModel::publishPendingImages()
{
    if (full_image_instances_.size() == image_ids_.size())
    {
        return;
    }

    std::vector<int64_t> pending_ids;
    pending_ids.reserve(full_image_instances_.size() - image_ids_.size());
    const bool has_published_rows = !image_ids_.empty();
    const int64_t first_published_id = has_published_rows ? image_ids_.front() : -1;
    for (auto it = full_image_instances_.cbegin(); it != full_image_instances_.cend(); ++it)
    {
        if (has_published_rows && it->first <= first_published_id)
        {
            break;
        }
        pending_ids.push_back(it->first);
    }

    if (pending_ids.empty() || full_image_instances_.size() != image_ids_.size() + pending_ids.size())
    {
        // 只有外部绕过 DataManager 写入了非单调 ID 时才会进入这里。
        beginResetModel();
        rebuildImageIds();
        endResetModel();
        return;
    }

    beginInsertRows(QModelIndex(), 0, static_cast<int>(pending_ids.size()) - 1);
    image_ids_.insert(image_ids_.begin(), pending_ids.begin(), pending_ids.end());
    endInsertRows();
}

bool ImageInstancesListModel::updateImagesDataset(const std::vector<int64_t> &image_ids, const int64_t dataset_id)
{
    return updateImagesDataset(image_ids, std::vector<int64_t>(image_ids.size(), dataset_id));
}

bool ImageInstancesListModel::updateImagesDataset(const std::vector<int64_t> &image_ids,
                                                   const std::vector<int64_t> &dataset_ids)
{
    if (database_ == nullptr)
    {
        spdlog::error("批量移动图像失败: 数据库未初始化");
        return false;
    }
    if (image_ids.size() != dataset_ids.size())
    {
        spdlog::error("批量移动图像失败: 图像 ID 和数据集 ID 数量不一致");
        return false;
    }
    if (image_ids.empty())
    {
        return true;
    }

    QString err_msg;
    if (!database_->updateImagesDataset(image_ids, dataset_ids, err_msg))
    {
        spdlog::error("批量移动图像失败: {}", err_msg.toUtf8().constData());
        return false;
    }
    updateImagesDatasetFromMemory(image_ids, dataset_ids);
    return true;
}

void ImageInstancesListModel::updateImagesDatasetFromMemory(const std::vector<int64_t> &image_ids,
                                                             const std::vector<int64_t> &dataset_ids,
                                                             const bool notify_model)
{
    if (image_ids.size() != dataset_ids.size())
    {
        spdlog::error("批量更新图像数据集失败: 图像 ID 和数据集 ID 数量不一致");
        return;
    }

    std::vector<int64_t> changed_ids;
    if (notify_model)
    {
        changed_ids.reserve(image_ids.size());
    }
    for (size_t index = 0; index < image_ids.size(); ++index)
    {
        ImageInstance *image = getImageInstance(image_ids[index]);
        if (image == nullptr || image->datasetId() == dataset_ids[index])
        {
            continue;
        }
        image->setDatasetId(dataset_ids[index]);
        if (notify_model)
        {
            changed_ids.push_back(image_ids[index]);
        }
    }
    if (notify_model)
    {
        notifyImageRowsChanged(changed_ids, {DatasetIdRole});
    }
}

void ImageInstancesListModel::updateImagesDatasetFromMemory(const std::vector<int64_t> &image_ids,
                                                             const int64_t dataset_id,
                                                             const bool notify_model)
{
    std::vector<int64_t> changed_ids;
    if (notify_model)
    {
        changed_ids.reserve(image_ids.size());
    }
    for (const int64_t image_id : image_ids)
    {
        ImageInstance *image = getImageInstance(image_id);
        if (image == nullptr || image->datasetId() == dataset_id)
        {
            continue;
        }
        image->setDatasetId(dataset_id);
        if (notify_model)
        {
            changed_ids.push_back(image_id);
        }
    }
    if (notify_model)
    {
        notifyImageRowsChanged(changed_ids, {DatasetIdRole});
    }
}

bool ImageInstancesListModel::deleteImages(const std::vector<int64_t> &image_ids)
{
    if (image_ids.empty())
    {
        return true;
    }
    if (database_ == nullptr)
    {
        spdlog::error("批量删除图像失败: 数据库未初始化");
        return false;
    }
    QString err_msg;
    if (!database_->deleteImages(image_ids, err_msg))
    {
        spdlog::error("批量删除图像失败: {}", err_msg.toUtf8().constData());
        return false;
    }
    removeImagesFromMemory(image_ids);
    return true;
}

bool ImageInstancesListModel::deleteImages(const int64_t dataset_id, std::vector<int64_t> &image_ids)
{
    image_ids.clear();
    image_ids.reserve(full_image_instances_.size());
    for (const auto &[image_id, image] : full_image_instances_)
    {
        if (image != nullptr && image->datasetId() == dataset_id)
        {
            image_ids.push_back(image_id);
        }
    }
    return deleteImages(image_ids);
}

std::vector<int64_t> ImageInstancesListModel::getImageIdsForDatasets(const std::vector<int64_t> &dataset_ids) const
{
    if (dataset_ids.empty())
    {
        return {};
    }

    const std::unordered_set<int64_t> expected(dataset_ids.cbegin(), dataset_ids.cend());
    std::vector<int64_t> image_ids;
    image_ids.reserve(full_image_instances_.size());
    for (const auto &[image_id, image] : full_image_instances_)
    {
        if (image != nullptr && expected.contains(image->datasetId()))
        {
            image_ids.push_back(image_id);
        }
    }
    return image_ids;
}

void ImageInstancesListModel::removeImagesFromMemory(const std::vector<int64_t> &image_ids)
{
    const std::vector<int> rows = findRowsByImageIds(image_ids);
    if (rows.empty())
    {
        return;
    }

    const std::vector<std::pair<int, int>> ranges = mergeConsecutiveRanges(rows);
    for (auto range = ranges.crbegin(); range != ranges.crend(); ++range)
    {
        const int first = range->first;
        const int count = range->second;
        beginRemoveRows(QModelIndex(), first, first + count - 1);
        for (int row = first; row < first + count; ++row)
        {
            const int64_t image_id = image_ids_[static_cast<size_t>(row)];
            const auto found = full_image_instances_.find(image_id);
            if (found != full_image_instances_.end())
            {
                delete found->second;
                full_image_instances_.erase(found);
            }
        }
        image_ids_.erase(image_ids_.begin() + first, image_ids_.begin() + first + count);
        endRemoveRows();
    }
}

std::vector<QString> ImageInstancesListModel::getImagePaths(const QString &image_dir)
{
    return getFiles(image_dir, data::DataFormat::getSupportedImageFormat(), false);
}

std::vector<QString> ImageInstancesListModel::getFiles(const QString &path, const QStringList &name_filters,
                                                        const bool recursive)
{
    const QFileInfo file_info(path);
    if (file_info.isFile())
    {
        return {path};
    }
    if (!file_info.isDir())
    {
        return {};
    }

    QDir dir(path);
    dir.setNameFilters(name_filters);
    dir.setFilter(recursive ? QDir::Files : QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    dir.setSorting(QDir::Name);

    std::vector<QString> files;
    for (const QFileInfo &entry : dir.entryInfoList())
    {
        if (entry.isFile())
        {
            files.push_back(entry.absoluteFilePath());
        }
        else if (recursive && entry.isDir())
        {
            std::vector<QString> child_files = getFiles(entry.absoluteFilePath(), name_filters, true);
            files.insert(files.end(), std::make_move_iterator(child_files.begin()), std::make_move_iterator(child_files.end()));
        }
    }
    return files;
}

ImageInstance *ImageInstancesListModel::getImageInstance(const int64_t image_id) const
{
    const auto found = full_image_instances_.find(image_id);
    return found == full_image_instances_.end() ? nullptr : found->second;
}

void ImageInstancesListModel::addImagesLabelIds(const std::vector<int64_t> &image_ids,
                                                 const std::vector<int64_t> &label_ids, const bool notify_model)
{
    const size_t count = std::min(image_ids.size(), label_ids.size());
    std::unordered_set<int64_t> changed_ids;
    if (notify_model)
    {
        changed_ids.reserve(count);
    }
    for (size_t index = 0; index < count; ++index)
    {
        if (ImageInstance *image = getImageInstance(image_ids[index]))
        {
            image->addLabelId(label_ids[index]);
            if (notify_model)
            {
                changed_ids.insert(image_ids[index]);
            }
        }
    }
    if (notify_model)
    {
        notifyImageRowsChanged({changed_ids.cbegin(), changed_ids.cend()}, {HasLabelsRole});
    }
}

void ImageInstancesListModel::syncLabelRelations(const std::vector<int64_t> &image_ids,
                                                  const LabelInstancesListModel *label_instances,
                                                  const bool notify_model)
{
    if (label_instances == nullptr)
    {
        return;
    }

    std::vector<int64_t> changed_ids;
    if (notify_model)
    {
        changed_ids.reserve(image_ids.size());
    }
    for (const int64_t image_id : image_ids)
    {
        ImageInstance *image = getImageInstance(image_id);
        if (image == nullptr)
        {
            continue;
        }
        const std::set<int64_t> &labels = label_instances->labelIdsForImage(image_id);
        if (image->labelIds() == labels)
        {
            continue;
        }
        image->setLabelIds(labels);
        if (notify_model)
        {
            changed_ids.push_back(image_id);
        }
    }
    if (notify_model)
    {
        notifyImageRowsChanged(changed_ids, {HasLabelsRole});
    }
}

void ImageInstancesListModel::syncAllLabelRelations(const LabelInstancesListModel *label_instances,
                                                     const bool notify_model)
{
    if (label_instances == nullptr)
    {
        return;
    }

    std::vector<int64_t> changed_ids;
    if (notify_model)
    {
        changed_ids.reserve(full_image_instances_.size());
    }
    for (const auto &[image_id, image] : full_image_instances_)
    {
        if (image == nullptr)
        {
            continue;
        }
        const std::set<int64_t> &labels = label_instances->labelIdsForImage(image_id);
        if (image->labelIds() == labels)
        {
            continue;
        }
        image->setLabelIds(labels);
        if (notify_model)
        {
            changed_ids.push_back(image_id);
        }
    }
    if (notify_model)
    {
        notifyImageRowsChanged(changed_ids, {HasLabelsRole});
    }
}

void ImageInstancesListModel::deleteImagesLabelIds(const std::vector<int64_t> &image_ids,
                                                    const std::vector<int64_t> &label_ids)
{
    const size_t count = std::min(image_ids.size(), label_ids.size());
    std::unordered_set<int64_t> changed_ids;
    changed_ids.reserve(count);
    for (size_t index = 0; index < count; ++index)
    {
        if (ImageInstance *image = getImageInstance(image_ids[index]))
        {
            image->removeLabelId(label_ids[index]);
            changed_ids.insert(image_ids[index]);
        }
    }
    notifyImageRowsChanged({changed_ids.cbegin(), changed_ids.cend()}, {HasLabelsRole});
}

bool ImageInstancesListModel::setImageLabelClassId(const int64_t image_id, const int64_t label_class_id)
{
    return setImageLabelClassIds({image_id}, {label_class_id});
}

bool ImageInstancesListModel::setImageLabelClassIds(const std::vector<int64_t> &image_ids,
                                                     const std::vector<int64_t> &label_class_ids)
{
    if (database_ == nullptr)
    {
        spdlog::error("更新图像级类别失败: 数据库未初始化");
        return false;
    }
    if (image_ids.size() != label_class_ids.size())
    {
        spdlog::error("更新图像级类别失败: 图像 ID 和类别 ID 数量不一致");
        return false;
    }
    if (image_ids.empty())
    {
        return true;
    }

    std::vector<int64_t> valid_image_ids;
    std::vector<int64_t> valid_label_class_ids;
    std::vector<std::vector<uint8_t>> extra_data;
    valid_image_ids.reserve(image_ids.size());
    valid_label_class_ids.reserve(label_class_ids.size());
    extra_data.reserve(image_ids.size());
    for (size_t index = 0; index < image_ids.size(); ++index)
    {
        if (getImageInstance(image_ids[index]) == nullptr)
        {
            continue;
        }
        valid_image_ids.push_back(image_ids[index]);
        valid_label_class_ids.push_back(label_class_ids[index]);
        extra_data.push_back(makeImageLabelClassExtraData(label_class_ids[index]));
    }
    if (valid_image_ids.empty())
    {
        return true;
    }

    QString err_msg;
    if (!database_->updateImagesExtraData(valid_image_ids, extra_data, err_msg))
    {
        spdlog::error("更新图像级类别失败: {}", err_msg.toUtf8().constData());
        return false;
    }
    setImageLabelClassIdsFromMemory(valid_image_ids, valid_label_class_ids);
    return true;
}

void ImageInstancesListModel::setImageLabelClassIdsFromMemory(const std::vector<int64_t> &image_ids,
                                                               const std::vector<int64_t> &label_class_ids,
                                                               const bool notify_model)
{
    if (image_ids.size() != label_class_ids.size())
    {
        spdlog::error("批量发布图像级类别失败: 图像 ID 和类别 ID 数量不一致");
        return;
    }

    std::vector<int64_t> changed_ids;
    if (notify_model)
    {
        changed_ids.reserve(image_ids.size());
    }
    for (size_t index = 0; index < image_ids.size(); ++index)
    {
        ImageInstance *image = getImageInstance(image_ids[index]);
        if (image == nullptr || image->imageLabelClassId() == label_class_ids[index])
        {
            continue;
        }
        image->setImageLabelClassId(label_class_ids[index]);
        if (notify_model)
        {
            changed_ids.push_back(image_ids[index]);
        }
    }
    if (notify_model)
    {
        notifyImageRowsChanged(changed_ids, {ImageLabelClassIdRole, HasLabelsRole});
    }
}

std::vector<uint8_t> ImageInstancesListModel::extraDataForImageLabelClassId(const int64_t label_class_id)
{
    return makeImageLabelClassExtraData(label_class_id);
}

int64_t ImageInstancesListModel::imageLabelClassIdFromExtraData(const std::vector<uint8_t> &extra_data)
{
    return readImageLabelClassId(extra_data);
}

QString ImageInstancesListModel::getImageName(const int64_t image_id) const
{
    const ImageInstance *image = getImageInstance(image_id);
    return image == nullptr ? QString() : image->name();
}

QString ImageInstancesListModel::getImagePath(const int64_t image_id) const
{
    const ImageInstance *image = getImageInstance(image_id);
    return image == nullptr ? QString() : image->path();
}

int64_t ImageInstancesListModel::getImageLabelClassId(const int64_t image_id) const
{
    const ImageInstance *image = getImageInstance(image_id);
    return image == nullptr ? -1 : image->imageLabelClassId();
}

int64_t ImageInstancesListModel::getImageDatasetId(const int64_t image_id) const
{
    const ImageInstance *image = getImageInstance(image_id);
    return image == nullptr ? -1 : image->datasetId();
}

const std::set<int64_t> &ImageInstancesListModel::getImageTagIds(const int64_t image_id) const
{
    static const std::set<int64_t> empty_tag_ids;
    const ImageInstance *image = getImageInstance(image_id);
    return image == nullptr ? empty_tag_ids : image->tagIds();
}

void ImageInstancesListModel::rebuildImageIds()
{
    image_ids_.clear();
    image_ids_.reserve(full_image_instances_.size());
    for (const auto &[image_id, _] : full_image_instances_)
    {
        image_ids_.push_back(image_id);
    }
}

int64_t ImageInstancesListModel::getImageId(const QModelIndex &index) const
{
    return image_ids_[static_cast<size_t>(index.row())];
}

QVariant ImageInstancesListModel::getImageName(const QModelIndex &index) const
{
    return getImageName(getImageId(index));
}

QVariant ImageInstancesListModel::getImagePath(const QModelIndex &index) const
{
    return getImagePath(getImageId(index));
}

QVariant ImageInstancesListModel::getHasLabels(const QModelIndex &index) const
{
    const ImageInstance *image = getImageInstance(getImageId(index));
    return image != nullptr && image->hasLabels();
}

QVariant ImageInstancesListModel::getImageLabelClassId(const QModelIndex &index) const
{
    return static_cast<qlonglong>(getImageLabelClassId(getImageId(index)));
}

std::vector<std::pair<int, int>> ImageInstancesListModel::mergeConsecutiveRanges(
    const std::vector<int> &sorted_rows) const
{
    std::vector<std::pair<int, int>> ranges;
    if (sorted_rows.empty())
    {
        return ranges;
    }

    int first = sorted_rows.front();
    int last = first;
    for (size_t index = 1; index < sorted_rows.size(); ++index)
    {
        if (sorted_rows[index] == last + 1)
        {
            last = sorted_rows[index];
            continue;
        }
        ranges.emplace_back(first, last - first + 1);
        first = last = sorted_rows[index];
    }
    ranges.emplace_back(first, last - first + 1);
    return ranges;
}

std::vector<int> ImageInstancesListModel::findRowsByImageIds(const std::vector<int64_t> &image_ids) const
{
    if (image_ids.empty())
    {
        return {};
    }
    const std::unordered_set<int64_t> expected(image_ids.cbegin(), image_ids.cend());
    std::vector<int> rows;
    rows.reserve(expected.size());
    for (int row = 0; row < rowCount(); ++row)
    {
        if (expected.contains(image_ids_[static_cast<size_t>(row)]))
        {
            rows.push_back(row);
        }
    }
    return rows;
}

void ImageInstancesListModel::notifyImageRowsChanged(const std::vector<int64_t> &image_ids, const QList<int> &roles)
{
    const std::vector<int> rows = findRowsByImageIds(image_ids);
    for (const auto &[first, count] : mergeConsecutiveRanges(rows))
    {
        emit dataChanged(index(first, 0), index(first + count - 1, 0), roles);
    }
}

ImageInfoListModel::ImageInfoListModel(DatasetsListModel *datasets, ImageInstancesViewModel *image_instances,
                                       LabelClassesListModel *label_classes, LabelInstancesListModel *label_instances,
                                       QObject *parent)
    : QAbstractListModel(parent)
    , datasets_(datasets)
    , image_instances_(image_instances)
    , label_classes_(label_classes)
    , label_instances_(label_instances)
{
}

ImageInfoListModel::~ImageInfoListModel() = default;

int ImageInfoListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 6;
}

QVariant ImageInfoListModel::data(const QModelIndex &index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
    {
        return {};
    }
    if (role == TitleRole)
    {
        return getTitle(index);
    }
    if (role == ValueRole)
    {
        return getValue(index);
    }
    return {};
}

QHash<int, QByteArray> ImageInfoListModel::roleNames() const
{
    return {{TitleRole, "title"}, {ValueRole, "value"}};
}

void ImageInfoListModel::onCurrentImageChanged()
{
    resetModel();
}

void ImageInfoListModel::updateLabelInfo()
{
    emit dataChanged(index(4, 0), index(5, 0), {ValueRole});
}

void ImageInfoListModel::resetModel()
{
    beginResetModel();
    endResetModel();
}

QVariant ImageInfoListModel::getTitle(const QModelIndex &index) const
{
    switch (index.row())
    {
    case 0:
        return QString("图像名称:");
    case 1:
        return QString("图像路径:");
    case 2:
        return QString("图像大小:");
    case 3:
        return QString("所属数据集:");
    case 4:
        return QString("图像类别:");
    case 5:
        return QString("标签实例:");
    default:
        return {};
    }
}

QVariant ImageInfoListModel::getValue(const QModelIndex &index) const
{
    if (image_instances_ == nullptr || image_instances_->source() == nullptr)
    {
        return QString();
    }
    const int64_t image_id = image_instances_->currentImageId();
    const ImageInstance *image = image_instances_->source()->getImageInstance(image_id);
    if (image == nullptr)
    {
        return QString();
    }

    switch (index.row())
    {
    case 0:
        return image->name();
    case 1:
        return image->path();
    case 2:
    {
        const QSize size = image->imageSize();
        return QString("%1x%2").arg(size.width()).arg(size.height());
    }
    case 3:
        return datasets_ != nullptr ? datasets_->getDatasetName(static_cast<int>(image->datasetId())) : QString();
    case 4:
        return label_classes_ != nullptr && image->imageLabelClassId() >= 0
            ? label_classes_->getLabelClassName(image->imageLabelClassId())
            : QString();
    case 5:
    {
        if (label_instances_ == nullptr || label_classes_ == nullptr)
        {
            return QString();
        }
        std::map<QString, int> class_counts;
        for (const int64_t label_id : image->labelIds())
        {
            const int64_t class_id = label_instances_->getLabelClassId(label_id);
            if (class_id >= 0)
            {
                ++class_counts[label_classes_->getLabelClassName(class_id)];
            }
        }
        QString summary;
        for (const auto &[name, count] : class_counts)
        {
            summary += QString("%1 (%2), ").arg(name).arg(count);
        }
        if (!summary.isEmpty())
        {
            summary.chop(2);
        }
        return summary;
    }
    default:
        return {};
    }
}

} // namespace dltool::data
