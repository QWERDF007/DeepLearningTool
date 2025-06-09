#include "project/ImageTags.h"

#include "data/DataBase.h"
#include "project/Datasets.h"

#include <spdlog/spdlog.h>

namespace dltool::project {

ImageTagsListModel::ImageTagsListModel(data::ProjectDataBase *database, QObject *parent)
    : QAbstractListModel(parent)
    , database_(database)
{
    init();
}

ImageTagsListModel::~ImageTagsListModel() {}

void ImageTagsListModel::init()
{
    if (database_)
    {
        QString              err_msg;
        std::vector<int64_t> tags_id;
        std::vector<QString> tags_name;
        database_->getAllTags(tags_id, tags_name, err_msg);
        if (!err_msg.isEmpty())
        {
            spdlog::error("查询所有标签失败, error: {}", err_msg.toUtf8().constData());
        }
        else
        {
            for (size_t i = 0; i < tags_id.size(); ++i)
            {
                image_tags_.emplace(tags_id[i], new ImageTag(tags_id[i], tags_name[i], this));
            }
        }
    }
}

int ImageTagsListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(image_tags_.size());
}

QVariant ImageTagsListModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= rowCount())
        return QVariant();
    switch (role)
    {
    case TagIdRole:
        return getTagId(index);
    case NameRole:
        return getTagName(index);
    case StatsRole:
        return getTagStats(index);
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> ImageTagsListModel::roleNames() const
{
    return {
        {TagIdRole, "tag_id"},
        { NameRole,   "name"},
        {StatsRole,  "stats"},
    };
}

bool ImageTagsListModel::addTag(const QString &name)
{
    if (database_ == nullptr)
    {
        spdlog::error("添加标签失败: {}, 数据库未初始化", name.toUtf8().constData());
        return false;
    }
    QString err_msg;
    int64_t tag_id{-1};
    bool    ok = database_->addTag(name, tag_id, err_msg);
    if (!ok)
    {
        spdlog::error("添加标签失败: {}, error: {}", name.toUtf8().constData(), err_msg.toUtf8().constData());
        return false;
    }
    spdlog::info("添加标签: {}", name.toUtf8().constData());
    const int row   = rowCount(); // 添加到队列尾部
    const int count = 1;
    beginInsertRows(QModelIndex(), row, row + count - 1);
    image_tags_.emplace(tag_id, new ImageTag(tag_id, name, this));
    endInsertRows();
    return true;
}

bool ImageTagsListModel::updateTag(const int64_t tag_id, const QString &name)
{
    if (database_ == nullptr)
    {
        spdlog::error("更新标签失败: {}, 数据库未初始化", tag_id);
        return false;
    }
    auto found = image_tags_.find(tag_id);
    if (found == image_tags_.end())
    {
        spdlog::error("更新标签失败: {}, 标签不存在", tag_id);
        return false;
    }
    if (found->second->name() == name)
        return true;
    QString err_msg;
    bool    ok = database_->updateTag(tag_id, name, err_msg);
    if (!ok)
    {
        spdlog::error("更新标签失败: {}, error: {}", tag_id, err_msg.toUtf8().constData());
        return false;
    }
    spdlog::info("更新标签: {} -> {}", found->second->name().toUtf8().constData(), name.toUtf8().constData());
    int idx{0};
    for (const auto &[_, tag] : image_tags_)
    {
        if (tag && tag->id() == tag_id)
        {
            tag->setName(name);
            emit dataChanged(index(idx), index(idx), {NameRole});
            break;
        }
        ++idx;
    }
    return true;
}

bool ImageTagsListModel::deleteTag(const int64_t tag_id)
{
    return false;
}

int ImageTagsListModel::getTagId(const QModelIndex &index) const
{
    int idx = 0;
    for (const auto &[id, tag] : image_tags_)
    {
        if (index.row() == idx)
            return id;
        ++idx;
    }
    return -1;
}

QVariant ImageTagsListModel::getTagName(const QModelIndex &index) const
{
    const int id = getTagId(index);
    if (id != -1)
        return image_tags_.at(id)->name();
    return "";
}

QVariant ImageTagsListModel::getTagStats(const QModelIndex &index) const
{
    const int id = getTagId(index);
    if (id != -1)
        return "(1)";
    return "";
}

} // namespace dltool::project
