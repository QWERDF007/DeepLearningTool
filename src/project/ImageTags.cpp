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
        std::vector<int64_t> tag_classes_id;
        std::vector<QString> tags_name;
        database_->getAllTagClasses(tag_classes_id, tags_name, err_msg);
        if (!err_msg.isEmpty())
        {
            spdlog::error("查询所有标签(TagClass)失败, error: {}", err_msg.toUtf8().constData());
        }
        else
        {
            for (size_t i = 0; i < tag_classes_id.size(); ++i)
            {
                image_tags_.emplace(tag_classes_id[i], new ImageTag(tag_classes_id[i], tags_name[i], this));
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
        return getTagClassId(index);
    case NameRole:
        return getTagClassName(index);
    case StatsRole:
        return getTagClassStats(index);
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

bool ImageTagsListModel::addTagClass(const QString &name)
{
    if (database_ == nullptr)
    {
        spdlog::error("添加标签(TagClass)失败: {}, 数据库未初始化", name.toUtf8().constData());
        return false;
    }
    QString err_msg;
    int64_t tag_class_id{-1};
    bool    ok = database_->addTagClass(name, tag_class_id, err_msg);
    if (!ok)
    {
        spdlog::error("添加标签(TagClass)失败: {}, error: {}", name.toUtf8().constData(), err_msg.toUtf8().constData());
        return false;
    }
    spdlog::info("添加标签(TagClass): {}", name.toUtf8().constData());
    const int row   = rowCount(); // 添加到队列尾部
    const int count = 1;
    beginInsertRows(QModelIndex(), row, row + count - 1);
    image_tags_.emplace(tag_class_id, new ImageTag(tag_class_id, name, this));
    endInsertRows();
    return true;
}

bool ImageTagsListModel::updateTagClass(const int64_t tag_class_id, const QString &name)
{
    if (database_ == nullptr)
    {
        spdlog::error("更新标签(TagClass)失败: {}, 数据库未初始化", tag_class_id);
        return false;
    }
    auto found = image_tags_.find(tag_class_id);
    if (found == image_tags_.end())
    {
        spdlog::error("更新标签(TagClass)失败: {}, 标签不存在", tag_class_id);
        return false;
    }
    if (found->second->name() == name)
        return true;
    QString err_msg;
    bool    ok = database_->updateTagClass(tag_class_id, name, err_msg);
    if (!ok)
    {
        spdlog::error("更新标签(TagClass)失败: {}, error: {}", tag_class_id, err_msg.toUtf8().constData());
        return false;
    }
    spdlog::info("更新标签(TagClass): {} -> {}", found->second->name().toUtf8().constData(), name.toUtf8().constData());
    int idx{0};
    for (const auto &[_, tag] : image_tags_)
    {
        if (tag && tag->id() == tag_class_id)
        {
            tag->setName(name);
            emit dataChanged(index(idx), index(idx), {NameRole});
            break;
        }
        ++idx;
    }
    return true;
}

bool ImageTagsListModel::deleteTagClass(const int64_t tag_class_id)
{
    return false;
}

int ImageTagsListModel::getTagClassId(const QModelIndex &index) const
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

QVariant ImageTagsListModel::getTagClassName(const QModelIndex &index) const
{
    const int id = getTagClassId(index);
    if (id != -1)
        return image_tags_.at(id)->name();
    return "";
}

QVariant ImageTagsListModel::getTagClassStats(const QModelIndex &index) const
{
    const int id = getTagClassId(index);
    if (id != -1)
        return "(1)";
    return "";
}

} // namespace dltool::project
