#include "data/Images.h"

#include "data/DataBase.h"
#include "data/Datasets.h"
#include "data/LabelClasses.h"
#include "data/Labels.h"

#include <data/DataFormat.h>
#include <spdlog/spdlog.h>

#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <algorithm>

namespace dltool::data {
ImageInstance::ImageInstance(const int64_t dataset_id, const int64_t image_id, const QString &path, QObject *parent)
    : QObject(parent)
    , dataset_id_(dataset_id)
    , image_id_(image_id)
    , path_(path)
{
    name_ = QFileInfo(path_).fileName();
}

ImageInstance::~ImageInstance() {}

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

ImageInstancesListModel::ImageInstancesListModel(data::ProjectDataBase *database, QObject *parent)
    : QAbstractListModel(parent)
    , database_(database)
    , selection_(new QItemSelectionModel(this))
{
    init();
}

ImageInstancesListModel::~ImageInstancesListModel() {}

void ImageInstancesListModel::init()
{
    connect(selection_, &QItemSelectionModel::selectionChanged, this, &ImageInstancesListModel::updateSelection);
    connect(selection_, &QItemSelectionModel::currentChanged, this, &ImageInstancesListModel::onCurrentChanged);

    QString              err_msg;
    std::vector<int64_t> dataset_ids;
    std::vector<int64_t> image_ids;
    std::vector<QString> paths;
    database_->getAllImages(dataset_ids, image_ids, paths, err_msg);

    for (size_t i = 0; i < image_ids.size(); ++i)
    {
        image_instances_.emplace(image_ids[i], new ImageInstance(dataset_ids[i], image_ids[i], paths[i], this));
    }
    std::sort(image_ids.begin(), image_ids.end(), std::greater<int64_t>());
    image_ids_.insert(image_ids_.begin(), image_ids.begin(), image_ids.end());
}

int ImageInstancesListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return count();
}

QVariant ImageInstancesListModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= rowCount())
        return QVariant();
    switch (role)
    {
    case ImageIdRole:
        return getImageId(index);
    case NameRole:
        return getImageName(index);
    case PathRole:
        return getImagePath(index);
    case SelectedRole:
        return getSelected(index);
    case IsCurrentRole:
        return getIsCurrent(index);
    case HasLabelsRole:
        return getHasLabels(index);
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> ImageInstancesListModel::roleNames() const
{
    return {
        {  ImageIdRole,  "image_id"},
        {     NameRole,      "name"},
        {     PathRole,      "path"},
        { SelectedRole,  "selected"},
        {HasLabelsRole, "hasLabels"},
    };
}

bool ImageInstancesListModel::addImages(const int64_t dataset_id, const std::vector<QString> &paths,
                                        std::vector<int64_t> &image_ids)
{
    if (database_ == nullptr)
    {
        spdlog::error("批量添加图像失败, dataset id: {}, 数据库未初始化", dataset_id);
        return false;
    }
    QString err_msg;
    bool    ok = database_->addImages(dataset_id, paths, image_ids, err_msg);
    if (!ok)
    {
        spdlog::error("批量添加图像失败, dataset id: {}, error: {}", dataset_id, err_msg.toUtf8().constData());
        return false;
    }
    spdlog::info("批量添加图像, dataset id: {}, 数量: {}", dataset_id, image_ids.size());

    QModelIndexList selected_indexes = selection_->selectedIndexes();
    QModelIndex     current_index    = selection_->currentIndex();

    int count = static_cast<int>(image_ids.size() - 1);
    beginInsertRows(QModelIndex(), 0, count);
    for (size_t i = 0; i < image_ids.size(); ++i)
    {
        image_instances_.emplace(image_ids[i], new ImageInstance(dataset_id, image_ids[i], paths[i], this));
    }
    std::sort(image_ids.begin(), image_ids.end(), std::greater<int64_t>());
    image_ids_.insert(image_ids_.begin(), image_ids.begin(), image_ids.end());
    endInsertRows();

    if (!selected_indexes.empty())
    {
        int offset = static_cast<int>(image_ids.size());
        for (const auto &selected_index : selected_indexes)
        {
            QModelIndex new_index = index(selected_index.row() + offset);
            selection_->select(new_index, QItemSelectionModel::ClearAndSelect);
        }
        QModelIndex new_index = index(current_index.row() + offset);
        selection_->setCurrentIndex(new_index, QItemSelectionModel::Select);
    }

    emit statsChanged();
    return true;
}

bool ImageInstancesListModel::addImages(const int64_t dataset_id, const QString &image_idr,
                                        std::vector<int64_t> &image_ids)
{
    if (database_ == nullptr)
    {
        spdlog::error("添加图像失败: {}, 数据库未初始化", image_idr.toUtf8().constData());
        return false;
    }
    std::vector<QString> paths = getImagePaths(image_idr);
    return addImages(dataset_id, paths, image_ids);
}

bool ImageInstancesListModel::deleteImages(const std::vector<int64_t> &image_ids)
{
    if (database_ == nullptr)
    {
        spdlog::error("批量删除图像失败, 数量: {}, 数据库未初始化", image_ids.size());
        return false;
    }
    QString err_msg;
    bool    ok = database_->deleteImages(image_ids, err_msg);
    if (!ok)
    {
        spdlog::error("批量删除图像失败: {}, error: {}", image_ids.size(), err_msg.toUtf8().constData());
        return false;
    }
    spdlog::info("批量删除图像, 数量: {}", image_ids.size());
    for (const auto &image_id : image_ids)
    {
        auto found = image_instances_.find(image_id);
        if (found == image_instances_.end())
            continue;
        delete found->second;
        image_instances_.erase(found);
    }
    resetModel();
    emit statsChanged();
    emit currentImageChanged();
    return true;
}

bool ImageInstancesListModel::deleteImages(const int64_t dataset_id, std::vector<int64_t> &image_ids)
{
    image_ids.reserve(image_instances_.size());
    for (const auto &[image_id, image_instance] : image_instances_)
    {
        if (image_instance->datasetId() == dataset_id)
        {
            image_ids.push_back(image_id);
        }
    }
    return deleteImages(image_ids);
}

std::vector<int64_t> ImageInstancesListModel::getDatasetIds(const std::vector<int64_t> &image_ids) const
{
    std::vector<int64_t> dataset_ids;
    dataset_ids.reserve(image_ids.size());
    for (const auto &image_id : image_ids)
    {
        auto found = image_instances_.find(image_id);
        if (found != image_instances_.end())
            dataset_ids.push_back(found->second->datasetId());
    }
    return dataset_ids;
}

std::vector<std::vector<int64_t>> ImageInstancesListModel::getLabelIds(const std::vector<int64_t> &image_ids) const
{
    std::vector<std::vector<int64_t>> label_ids;
    label_ids.reserve(image_ids.size());
    for (const auto &image_id : image_ids)
    {
        auto found = image_instances_.find(image_id);
        if (found != image_instances_.end())
            label_ids.push_back(
                std::vector<int64_t>{found->second->labelIds().begin(), found->second->labelIds().end()});
    }
    return label_ids;
}

std::vector<QString> ImageInstancesListModel::getImagePaths(const QString &image_idr)
{
    return getFiles(image_idr, data::DataFormat::getSupportedImageFormat(), false);
}

std::vector<QString> ImageInstancesListModel::getFiles(const QString &path, const QStringList &name_filters,
                                                       bool recursive)
{
    QFileInfo fileinfo(path);
    if (fileinfo.isFile())
    {
        return {path};
    }
    else if (fileinfo.isDir())
    {
        QDir dir(path);
        dir.setNameFilters(name_filters);
        dir.setFilter(recursive ? QDir::Files : QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
        dir.setSorting(QDir::Name);
        std::vector<QString> files;
        for (const auto &entry_info : dir.entryInfoList())
        {
            if (entry_info.isFile())
            {
                files.emplace_back(entry_info.absoluteFilePath());
            }
            else if (recursive && entry_info.isDir())
            {
                std::vector<QString> tmp_files = getFiles(entry_info.absoluteFilePath(), name_filters, recursive);
                files.insert(files.end(), tmp_files.begin(), tmp_files.end());
            }
        }
        return files;
    }
    else
    {
        return {};
    }
}

void ImageInstancesListModel::shiftSelect(int current_index, int previous_index,
                                          QItemSelectionModel::SelectionFlags command)
{
    const int top    = std::min(current_index, previous_index);
    const int bottom = std::max(current_index, previous_index);

    QItemSelection selection;
    selection.select(index(top), index(bottom));
    selection_->select(selection, command);
}

void ImageInstancesListModel::selectAll()
{
    QItemSelection selection;
    selection.select(index(0), index(rowCount() - 1));
    selection_->select(selection, QItemSelectionModel::Select);
}

QString ImageInstancesListModel::currentImageName() const
{
    QModelIndex index = selection_->currentIndex();
    if (index.row() < 0 || index.row() >= rowCount())
        return QString();
    return getImageName(index).toString();
}

QString ImageInstancesListModel::currentImagePath() const
{
    QModelIndex index = selection_->currentIndex();
    if (index.row() < 0 || index.row() >= rowCount())
        return QString();
    return getImagePath(index).toString();
}

int ImageInstancesListModel::getImageId(const QModelIndex &index) const
{
    return image_ids_[index.row()];
}

QVariant ImageInstancesListModel::getImageName(const QModelIndex &index) const
{
    const int64_t image_id = image_ids_[index.row()];
    return image_instances_.at(image_id)->name();
}

QVariant ImageInstancesListModel::getImagePath(const QModelIndex &index) const
{
    const int64_t image_id = image_ids_[index.row()];
    return image_instances_.at(image_id)->path();
}

QVariant ImageInstancesListModel::getSelected(const QModelIndex &index) const
{
    return selection_->isSelected(index);
}

QVariant ImageInstancesListModel::getIsCurrent(const QModelIndex &index) const
{
    return selection_->currentIndex() == index;
}

QVariant ImageInstancesListModel::getHasLabels(const QModelIndex &index) const
{
    const int64_t image_id = image_ids_[index.row()];
    auto          it       = image_instances_.find(image_id);
    if (it != image_instances_.end())
    {
        return !it->second->labelIds().empty();
    }
    return false;
}

int ImageInstancesListModel::getCurrentImageId() const
{
    QModelIndex index = selection_->currentIndex();
    if (index.row() < 0 || index.row() >= rowCount())
        return -1;
    return getImageId(index);
}

std::vector<int64_t> ImageInstancesListModel::getSelectedImagesId() const
{
    auto selected_indexes = selection_->selectedIndexes();

    std::vector<int64_t> image_ids;
    image_ids.reserve(selected_indexes.size());
    for (const QModelIndex &selected_index : selected_indexes)
    {
        image_ids.push_back(getImageId(selected_index));
    }
    return image_ids;
}

ImageInstance *ImageInstancesListModel::getImageInstance(const int64_t image_id)
{
    auto found = image_instances_.find(image_id);
    if (found != image_instances_.end())
        return found->second;
    return nullptr;
}

std::vector<ImageInstance *> ImageInstancesListModel::getImageInstances(const std::vector<int64_t> &image_ids)
{
    std::vector<ImageInstance *> image_instances;
    image_instances.reserve(image_ids.size());
    for (const auto &image_id : image_ids)
    {
        auto found = image_instances_.find(image_id);
        if (found != image_instances_.end())
        {
            image_instances.push_back(found->second);
        }
    }
    return image_instances;
}

void ImageInstancesListModel::setLastIndex(int last_index)
{
    if (last_index_ != last_index)
    {
        last_index_ = last_index;
        emit lastSelectedIndexChanged();
    }
}

void ImageInstancesListModel::addImagesLabelIds(const std::vector<int64_t> &image_ids,
                                                const std::vector<int64_t> &label_ids)
{
    std::map<int64_t, std::vector<int64_t>> images_label_ids;
    for (size_t i = 0; i < image_ids.size(); ++i)
    {
        const int64_t image_id = image_ids[i];
        if (images_label_ids.find(image_id) == images_label_ids.end())
        {
            images_label_ids[image_id] = std::vector<int64_t>();
            images_label_ids[image_id].reserve(image_ids.size());
        }
        images_label_ids[image_id].push_back(label_ids[i]);
    }
    for (const auto &[image_id, image_label_ids] : images_label_ids)
    {
        ImageInstance *image_instance = getImageInstance(image_id);
        if (image_instance)
        {
            image_instance->addLabelIds(image_label_ids);
            notifyHasLabelsChanged(image_id);
        }
    }
}

void ImageInstancesListModel::addImagesLabelIds(const std::vector<int64_t>              &image_ids,
                                                const std::vector<std::vector<int64_t>> &label_ids)
{
    for (size_t i = 0; i < image_ids.size(); ++i)
    {
        const int64_t  image_id       = image_ids[i];
        ImageInstance *image_instance = getImageInstance(image_id);
        if (image_instance)
        {
            image_instance->addLabelIds(label_ids[i]);
            notifyHasLabelsChanged(image_id);
        }
    }
}

void ImageInstancesListModel::deleteImagesLabelIds(const std::vector<int64_t> &image_ids,
                                                   const std::vector<int64_t> &label_ids)
{
    std::map<int64_t, std::vector<int64_t>> images_label_ids;
    for (size_t i = 0; i < image_ids.size(); ++i)
    {
        const int64_t image_id = image_ids[i];
        if (images_label_ids.find(image_id) == images_label_ids.end())
        {
            images_label_ids[image_id] = std::vector<int64_t>();
            images_label_ids[image_id].reserve(image_ids.size());
        }
        images_label_ids[image_id].push_back(label_ids[i]);
    }
    for (const auto &[image_id, image_label_ids] : images_label_ids)
    {
        ImageInstance *image_instance = getImageInstance(image_id);
        if (image_instance)
        {
            image_instance->removeLabelIds(image_label_ids);
            notifyHasLabelsChanged(image_id);
        }
    }
}

void ImageInstancesListModel::notifyHasLabelsChanged(int64_t image_id)
{
    auto it = std::find(image_ids_.begin(), image_ids_.end(), image_id);
    if (it != image_ids_.end())
    {
        int         row = std::distance(image_ids_.begin(), it);
        QModelIndex idx = index(row, 0);
        emit        dataChanged(idx, idx, {HasLabelsRole});
    }
}

void ImageInstancesListModel::addImagesTagIds(const std::vector<int64_t> &image_ids,
                                              const std::vector<int64_t> &tag_ids)
{
    std::map<int64_t, std::vector<int64_t>> images_tag_ids;
    for (size_t i = 0; i < image_ids.size(); ++i)
    {
        const int64_t image_id = image_ids[i];
        if (images_tag_ids.find(image_id) == images_tag_ids.end())
        {
            images_tag_ids[image_id] = std::vector<int64_t>();
            images_tag_ids[image_id].reserve(image_ids.size());
        }
        images_tag_ids[image_id].push_back(tag_ids[i]);
    }
    for (const auto &[image_id, image_tag_ids] : images_tag_ids)
    {
        ImageInstance *image_instance = getImageInstance(image_id);
        if (image_instance)
            image_instance->addTagIds(image_tag_ids);
    }
}

void ImageInstancesListModel::addImagesTagIds(const std::vector<int64_t>              &image_ids,
                                              const std::vector<std::vector<int64_t>> &tag_ids)
{
    for (size_t i = 0; i < image_ids.size(); ++i)
    {
        const int64_t  image_id       = image_ids[i];
        ImageInstance *image_instance = getImageInstance(image_id);
        if (image_instance)
            image_instance->addTagIds(tag_ids[i]);
    }
}

std::vector<int64_t> ImageInstancesListModel::getAllImageIds() const
{
    std::vector<int64_t> image_ids;
    image_ids.reserve(image_instances_.size());
    for (const auto &[image_id, _] : image_instances_)
    {
        image_ids.push_back(image_id);
    }
    return image_ids;
}

std::vector<int64_t> ImageInstancesListModel::getImagesDatasetIds(const std::vector<int64_t> &image_ids) const
{
    std::vector<int64_t> dataset_ids;
    dataset_ids.reserve(image_ids.size());
    for (const auto &image_id : image_ids)
    {
        auto found = image_instances_.find(image_id);
        if (found != image_instances_.end())
            dataset_ids.push_back(found->second->datasetId());
    }
    return dataset_ids;
}

void ImageInstancesListModel::getAllDatasetsImagesLabels(std::vector<int64_t>              &dataset_ids,
                                                         std::vector<int64_t>              &image_ids,
                                                         std::vector<std::vector<int64_t>> &images_label_ids) const
{
    dataset_ids.reserve(image_instances_.size());
    image_ids.reserve(image_instances_.size());
    images_label_ids.reserve(image_instances_.size());
    for (const auto &[_, image_instance] : image_instances_)
    {
        dataset_ids.push_back(image_instance->datasetId());
        image_ids.push_back(image_instance->imageId());
        images_label_ids.push_back(
            std::vector<int64_t>{image_instance->labelIds().begin(), image_instance->labelIds().end()});
    }
}

void ImageInstancesListModel::updateSelection(const QItemSelection &selected, const QItemSelection &deselected)
{
    const QModelIndexList &dselected_items = deselected.indexes();
    int                    top{-1};
    int                    bottom{-1};
    for (const QModelIndex &index : dselected_items)
    {
        const int row = index.row();
        if (top == -1)
            top = row;
        else
            top = std::min(top, row);
        bottom = std::max(bottom, row);
    }
    emit dataChanged(index(top), index(bottom), {SelectedRole});

    top    = -1;
    bottom = -1;

    const QModelIndexList &selected_items = selected.indexes();
    for (const QModelIndex &index : selected_items)
    {
        const int row = index.row();
        if (top == -1)
            top = row;
        else
            top = std::min(top, row);
        bottom = std::max(bottom, row);
    }
    emit dataChanged(index(top), index(bottom), {SelectedRole});
}

void ImageInstancesListModel::onCurrentChanged(const QModelIndex &current, const QModelIndex &previous)
{
    Q_UNUSED(current)
    Q_UNUSED(previous)
    emit currentImageChanged();
}

void ImageInstancesListModel::resetModel()
{
    beginResetModel();
    image_ids_.clear();
    image_ids_.reserve(image_instances_.size());
    for (const auto &[image_id, _] : image_instances_)
    {
        image_ids_.push_back(image_id);
    }
    endResetModel();
}

ImageInfoListModel::ImageInfoListModel(DatasetsListModel *datasets, ImageInstancesListModel *image_instances,
                                       LabelClassesListModel *label_classes, LabelInstancesListModel *label_instances,
                                       QObject *parent)
    : QAbstractListModel(parent)
    , datasets_(datasets)
    , image_instances_(image_instances)
    , label_classes_(label_classes)
    , label_instances_(label_instances)
{
}

ImageInfoListModel::~ImageInfoListModel() {}

int ImageInfoListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return 5;
}

QVariant ImageInfoListModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= rowCount())
        return QVariant();
    switch (role)
    {
    case TitleRole:
        return getTitle(index);
    case ValueRole:
        return getValue(index);
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> ImageInfoListModel::roleNames() const
{
    return {
        {TitleRole, "title"},
        {ValueRole, "value"},
    };
}

void ImageInfoListModel::onCurrentImageChanged()
{
    resetModel();
}

void ImageInfoListModel::updateLabelInfo()
{
    emit dataChanged(index(4), index(4), {ValueRole});
}

void ImageInfoListModel::resetModel()
{
    beginResetModel();
    // TODO: 重置模型
    endResetModel();
}

QVariant ImageInfoListModel::getTitle(const QModelIndex &index) const
{
    const int row = index.row();
    switch (row)
    {
    case 0:
        return "图像名称:";
    case 1:
        return "图像路径:";
    case 2:
        return "图像大小:";
    case 3:
        return "所属数据集:";
    case 4:
        return "标签实例:";
    default:
        return QVariant();
    }
}

QVariant ImageInfoListModel::getValue(const QModelIndex &index) const
{
    const int      row      = index.row();
    const int64_t  image_id = image_instances_->getCurrentImageId();
    ImageInstance *instance = image_instances_->getImageInstance(image_id);
    if (instance == nullptr)
        return "";
    switch (row)
    {
    case 0:
        return instance->name();
    case 1:
        return instance->path();
    case 2:
    {
        QSize size = instance->imageSize();
        return QString("%1x%2").arg(size.width()).arg(size.height());
    }
    case 3:
        return datasets_->getDatasetName(instance->datasetId());
    case 4:
    {
        std::set<int64_t>      label_ids = instance->labelIds();
        std::map<QString, int> classes_count;
        for (const auto &label_id : label_ids)
        {
            const int64_t label_class_id = label_instances_->getLabelClassId(label_id);
            if (label_class_id == -1)
                continue;
            const QString class_name = label_classes_->getLabelClassName(label_class_id);
            if (classes_count.find(class_name) == classes_count.end())
                classes_count[class_name] = 0;
            classes_count[class_name]++;
        }
        QString label_info;
        for (const auto &[class_name, count] : classes_count)
        {
            label_info += QString("%1 (%2), ").arg(class_name).arg(count);
        }
        if (!label_info.isEmpty())
            label_info.chop(2);
        return label_info;
    }
    default:
        return QVariant();
    }
}

} // namespace dltool::data
