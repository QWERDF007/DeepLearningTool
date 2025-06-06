#include "Tags.h"

#include "data/DataBase.h"
#include "project/Datasets.h"

#include <spdlog/spdlog.h>

namespace dltool::project {

Tag::Tag(const int64_t id, const QString &name, const QString &shortcut, QObject *parent)
    : id_(id)
    , name_(name)
    , shortcut_(shortcut)
    , QObject(parent)
{
}

Tag::~Tag() {}

TagsListModel::TagsListModel(data::ProjectDataBase *database, QObject *parent)
    : database_(database)
    , QAbstractListModel(parent)
{
}

TagsListModel::~TagsListModel() {}

int TagsListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(tags_.size());
}

QVariant TagsListModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= rowCount())
        return QVariant();
    switch (role)
    {
    case TagIdRole:
        return getTagId(index);
    case NameRole:
        return getTagName(index);
    case ShortcutRole:
        return getTagShortcut(index);
    case StatsRole:
        return getTagStats(index);
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> TagsListModel::roleNames() const
{
    return {
        {   TagIdRole,   "tag_id"},
        {    NameRole,     "name"},
        {ShortcutRole, "shortcut"},
        {   StatsRole,    "stats"},
    };
}

bool TagsListModel::addTag(const QString &name, const QString &shortcut)
{
    return false;
}

bool TagsListModel::updateTag(const int64_t tag_id, const QString &name, const QString &shortcut)
{
    return false;
}

bool TagsListModel::deleteTag(const int64_t tag_id)
{
    return false;
}

} // namespace dltool::project
