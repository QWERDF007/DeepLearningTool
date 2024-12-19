#include "project/Image.h"

#include "data/DataBase.h"

#include <spdlog/spdlog.h>

namespace dltool::project {
ImageInstance::ImageInstance(const int64_t id, const QString &path, QObject *parent)
    : QObject(parent)
    , id_(id)
    , path_(path)
{
}

ImageInstance::~ImageInstance() {}

ImageInstancesListModel::ImageInstancesListModel(data::ProjectDataBase *database, QObject *parent)
    : QAbstractListModel(parent)
    , database_(database)
{
}

ImageInstancesListModel::~ImageInstancesListModel() {}

int ImageInstancesListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(image_instances_.size());
}

QVariant ImageInstancesListModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= rowCount())
        return QVariant();
    switch (role)
    {
    case ImageIdRole:
        return QVariant();
    case NameRole:
        return QVariant();
    case PathRole:
        return QVariant();
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> ImageInstancesListModel::roleNames() const
{
    return {
        {ImageIdRole, "image_id"},
        {   NameRole,     "name"},
        {   PathRole,     "path"},
    };
}

bool ImageInstancesListModel::addImageInstance(const int64_t dataset_id, const QString &path)
{
    if (database_ == nullptr)
    {
        spdlog::error("添加图像失败: {}, 数据库未初始化", path.toUtf8().constData());
        return false;
    }
    QString err_msg;
    int64_t image_id{-1};
    bool    ok = database_->addImage(dataset_id, path, image_id, err_msg);
    if (!ok)
    {
        spdlog::error("添加图像失败: {}, error: {}", path.toUtf8().constData(), err_msg.toUtf8().constData());
        return false;
    }
    spdlog::info("添加图像: {}, id: {}", path.toUtf8().constData(), image_id);
    const int row = rowCount();
    beginInsertRows(QModelIndex(), row, row);
    image_instances_.emplace(dataset_id, new ImageInstance(image_id, path, this));
    endInsertRows();
    return true;
}

bool ImageInstancesListModel::deleteImageInstance(const int64_t image_id)
{
    if (database_ == nullptr)
    {
        spdlog::error("删除图像失败: {}, 数据库未初始化", image_id);
        return false;
    }
    QString err_msg;
    bool    ok = database_->deleteImage(image_id, err_msg);
    if (!ok)
    {
        spdlog::error("删除图像失败: {}, error: {}", image_id, err_msg.toUtf8().constData());
        return false;
    }
    spdlog::info("删除图像: {}", image_id);
    int idx{0};
    for (const auto &[_image_id, image_instance] : image_instances_)
    {
        if (image_instance && image_instance->id() == image_id)
        {
            beginRemoveRows(QModelIndex(), idx, idx);
            delete image_instance;
            image_instances_.erase(image_id);
            endRemoveRows();
            break;
        }
        ++idx;
    }
    return true;
}

} // namespace dltool::project
