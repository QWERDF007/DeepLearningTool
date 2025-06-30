#include "project/Images.h"

#include "data/DataBase.h"

#include <data/DataFormat.h>
#include <spdlog/spdlog.h>

#include <QDir>
#include <QFileInfo>
#include <QImageReader>

namespace dltool::project {
ImageInstance::ImageInstance(const int64_t dataset_id, const int64_t image_id, const QString &path, QObject *parent)
    : QObject(parent)
    , dataset_id_(dataset_id)
    , image_id_(image_id)
    , path_(path)
{
    name_ = QFileInfo(path_).fileName();
}

ImageInstance::~ImageInstance() {}

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
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> ImageInstancesListModel::roleNames() const
{
    return {
        { ImageIdRole, "image_id"},
        {    NameRole,     "name"},
        {    PathRole,     "path"},
        {SelectedRole, "selected"},
    };
}

bool ImageInstancesListModel::addImageInstances(const int64_t dataset_id, const std::vector<QString> &paths)
{
    if (database_ == nullptr)
    {
        spdlog::error("批量添加图像失败, dataset id: {}, 数据库未初始化", dataset_id);
        return false;
    }
    QString              err_msg;
    std::vector<int64_t> new_image_ids;
    bool                 ok = database_->addImages(dataset_id, paths, new_image_ids, err_msg);
    if (!ok)
    {
        spdlog::error("批量添加图像失败, dataset id: {}, error: {}", dataset_id, err_msg.toUtf8().constData());
        return false;
    }
    spdlog::info("批量添加图像, dataset id: {}, 数量: {}", dataset_id, new_image_ids.size());

    QModelIndexList selected_indexes = selection_->selectedIndexes();
    QModelIndex     current_index    = selection_->currentIndex();

    int count = static_cast<int>(new_image_ids.size() - 1);
    beginInsertRows(QModelIndex(), 0, count);
    for (size_t i = 0; i < new_image_ids.size(); ++i)
    {
        image_instances_.emplace(new_image_ids[i], new ImageInstance(dataset_id, new_image_ids[i], paths[i], this));
    }
    std::sort(new_image_ids.begin(), new_image_ids.end(), std::greater<int64_t>());
    image_ids_.insert(image_ids_.begin(), new_image_ids.begin(), new_image_ids.end());
    endInsertRows();

    if (!selected_indexes.empty())
    {
        int offset = static_cast<int>(new_image_ids.size());
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

bool ImageInstancesListModel::addImageInstances(const int64_t dataset_id, const QString &image_idr)
{
    if (database_ == nullptr)
    {
        spdlog::error("添加图像失败: {}, 数据库未初始化", image_idr.toUtf8().constData());
        return false;
    }
    std::vector<QString> paths = getImagePaths(image_idr);
    return addImageInstances(dataset_id, paths);
}

bool ImageInstancesListModel::deleteImageInstances(const std::vector<int64_t> &image_ids)
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
        if (found != image_instances_.end())
        {
            delete found->second;
            image_instances_.erase(found);
        }
    }
    resetModel();
    emit statsChanged();
    return true;
}

bool ImageInstancesListModel::deleteImageInstances(const int64_t dataset_id)
{
    std::vector<int64_t> image_ids;
    for (const auto &[image_id, image_instance] : image_instances_)
    {
        if (image_instance->datasetId() == dataset_id)
        {
            image_ids.push_back(image_id);
        }
    }
    return deleteImageInstances(image_ids);
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

void ImageInstancesListModel::deleteSelected()
{
    std::set<int>        indices;
    std::vector<int64_t> image_ids;
    for (const auto &index : selection_->selectedIndexes())
    {
        indices.insert(index.row());
    }
    selection_->clear(); // 清空选中, 注意要在resetModel前调用, 否则不会清空
    int idx = 0;
    for (const auto &[id, image_instance] : image_instances_)
    {
        if (indices.count(idx))
        {
            image_ids.emplace_back(id);
        }
        ++idx;
    }
    deleteImageInstances(image_ids);
}

QVariantMap ImageInstancesListModel::getImageInstanceInfo(const int64_t image_id)
{
    QVariantMap info;
    auto        found = image_instances_.find(image_id);
    if (found == image_instances_.end())
    {
        info["image_id"]   = -1;
        info["name"]       = "";
        info["path"]       = "";
        info["dataset_id"] = -1;
        info["imageSize"]  = "";
        return info;
    }
    info["image_id"]   = found->second->imageId();
    info["name"]       = found->second->name();
    info["path"]       = found->second->path();
    info["dataset_id"] = found->second->datasetId();
    QImageReader reader(found->second->path());
    QSize        image_size = reader.size();
    info["imageSize"]       = QString("%1x%2").arg(image_size.width()).arg(image_size.height());
    return info;
}

QString ImageInstancesListModel::curImageName() const
{
    if (selection_ == nullptr)
        return QString();
    QModelIndex index = selection_->currentIndex();
    if (index.row() < 0 || index.row() >= rowCount())
        return QString();
    return getImageName(index).toString();
}

QString ImageInstancesListModel::curImagePath() const
{
    if (selection_ == nullptr)
        return QString();
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
    if (selection_ == nullptr)
        return false;
    return selection_->isSelected(index);
}

int ImageInstancesListModel::getCurImageId() const
{
    QModelIndex index = selection_->currentIndex();
    if (index.row() < 0 || index.row() >= rowCount())
        return -1;
    return getImageId(index);
}

std::vector<int64_t> ImageInstancesListModel::getSelectedImagesId() const
{
    std::vector<int64_t> image_ids;
    for (QModelIndex selected_index : selection_->selectedIndexes())
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
            images_label_ids[image_id] = std::vector<int64_t>();
        images_label_ids[image_id].push_back(label_ids[i]);
    }
    for (const auto &[image_id, image_label_ids] : images_label_ids)
    {
        ImageInstance *image_instance = getImageInstance(image_id);
        if (image_instance)
            image_instance->addLabelIds(image_label_ids);
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
            images_tag_ids[image_id] = std::vector<int64_t>();
        images_tag_ids[image_id].push_back(tag_ids[i]);
    }
    for (const auto &[image_id, image_tag_ids] : images_tag_ids)
    {
        ImageInstance *image_instance = getImageInstance(image_id);
        if (image_instance)
            image_instance->addTagIds(image_tag_ids);
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
    emit curImageChanged();
}

void ImageInstancesListModel::resetModel()
{
    beginResetModel();
    image_ids_.clear();
    for (const auto &[image_id, image_instance] : image_instances_)
    {
        image_ids_.push_back(image_id);
    }
    endResetModel();
}

} // namespace dltool::project
