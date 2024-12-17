#include "project/Image.h"

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

} // namespace dltool::project
