#include "data/ImageTags.h"

#include "data/Images.h"
#include "data/Labels.h"
#include "database/DataBase.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <iterator>

namespace dltool::data {

ImageTagsListModel::ImageTagsListModel(dltool::database::ProjectDataBase *database,
                                       ImageInstancesListModel *image_instances,
                                       LabelInstancesListModel *label_instances,
                                       ImageLabelsListModel *image_labels_list, QObject *parent)
    : QAbstractListModel(parent)
    , database_(database)
    , image_instances_(image_instances)
    , label_instances_(label_instances)
    , image_labels_list_(image_labels_list)
{
    init();

    if (label_instances_ != nullptr)
    {
        connect(label_instances_, &LabelInstancesListModel::labelsAboutToBeRemoved, this,
                [this](const std::vector<int64_t> &label_ids)
                { removeTagsFromMemory(label_ids, TagTarget::Label); });
    }
}

void ImageTagsListModel::init()
{
    if (database_ == nullptr)
    {
        spdlog::error("初始化 Tag 失败: 数据库未初始化");
        return;
    }

    if (!initTagClasses())
    {
        return;
    }
    initTagRelations();
}

bool ImageTagsListModel::initTagClasses()
{
    QString              err_msg;
    std::vector<int64_t> tag_ids;
    std::vector<QString> tag_names;
    if (!database_->getAllTagClasses(tag_ids, tag_names, err_msg))
    {
        spdlog::error("查询 Tag 类别失败: {}", err_msg.toUtf8().constData());
        return false;
    }

    for (size_t i = 0; i < tag_ids.size(); ++i)
    {
        tags_.emplace(tag_ids[i], Tag(tag_ids[i], tag_names[i]));
    }
    return true;
}

bool ImageTagsListModel::initTagRelations()
{
    QString              err_msg;
    std::vector<int64_t> image_ids;
    std::vector<std::vector<int64_t>> image_tag_ids;
    std::vector<int64_t> label_ids;
    std::vector<std::vector<int64_t>> label_tag_ids;
    if (!database_->getAllTags(image_ids, image_tag_ids, label_ids, label_tag_ids, err_msg))
    {
        spdlog::error("查询 Tag 关系失败: {}", err_msg.toUtf8().constData());
        return false;
    }

    for (size_t i = 0; i < image_ids.size() && i < image_tag_ids.size(); ++i)
    {
        for (const int64_t tag_id : image_tag_ids[i])
        {
            if (Tag *tag = getTag(tag_id))
                tag->addImageIds({image_ids[i]});
        }
    }
    for (size_t i = 0; i < label_ids.size() && i < label_tag_ids.size(); ++i)
    {
        for (const int64_t tag_id : label_tag_ids[i])
        {
            if (Tag *tag = getTag(tag_id))
                tag->addLabelIds({label_ids[i]});
        }
    }
    return true;
}

int ImageTagsListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(tags_.size());
}

QVariant ImageTagsListModel::data(const QModelIndex &index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
    {
        return {};
    }

    switch (role)
    {
    case TagIdRole:
        return getTagClassId(index);
    case NameRole:
        return getTagClassName(index);
    case SelectedImagesStatsRole:
        return getSelectedImagesTagStats(index);
    case CurrentImageStatsRole:
        return getCurrentImageTagStats(index);
    case SelectedLabelsStatsRole:
        return getSelectedLabelsTagStats(index);
    default:
        return {};
    }
}

QHash<int, QByteArray> ImageTagsListModel::roleNames() const
{
    return {
        {              TagIdRole,                "tag_id"},
        {               NameRole,                  "name"},
        {SelectedImagesStatsRole, "selected_images_stats"},
        {  CurrentImageStatsRole,   "current_image_stats"},
        {SelectedLabelsStatsRole, "selected_labels_stats"},
    };
}

bool ImageTagsListModel::addTagClass(const QString &name)
{
    if (database_ == nullptr)
    {
        spdlog::error("添加 Tag 失败: 数据库未初始化");
        return false;
    }

    QString err_msg;
    int64_t tag_id{-1};
    if (!database_->addTagClass(name, tag_id, err_msg))
    {
        spdlog::error("添加 Tag 失败: {}, error: {}", name.toUtf8().constData(), err_msg.toUtf8().constData());
        return false;
    }

    const int row = rowCount();
    beginInsertRows(QModelIndex(), row, row);
    tags_.emplace(tag_id, Tag(tag_id, name));
    endInsertRows();
    return true;
}

int64_t ImageTagsListModel::findTagClassId(const QString &name) const
{
    const QString normalized_name = name.trimmed();
    if (normalized_name.isEmpty())
    {
        return -1;
    }

    for (const auto &[tag_id, tag] : tags_)
    {
        if (tag.name() == normalized_name)
        {
            return tag_id;
        }
    }
    return -1;
}

bool ImageTagsListModel::updateTagClass(const int64_t tag_id, const QString &name)
{
    if (database_ == nullptr)
    {
        spdlog::error("更新 Tag 失败: 数据库未初始化");
        return false;
    }

    Tag *tag = getTag(tag_id);
    if (tag == nullptr)
    {
        spdlog::error("更新 Tag 失败: Tag {} 不存在", tag_id);
        return false;
    }
    if (tag->name() == name)
    {
        return true;
    }

    QString err_msg;
    if (!database_->updateTagClass(tag_id, name, err_msg))
    {
        spdlog::error("更新 Tag 失败: {}, error: {}", tag_id, err_msg.toUtf8().constData());
        return false;
    }

    tag->setName(name);
    const int row = rowForTag(tag_id);
    if (row >= 0)
    {
        emit dataChanged(index(row), index(row), {NameRole});
    }
    return true;
}

bool ImageTagsListModel::deleteTagClass(const int64_t tag_id)
{
    if (database_ == nullptr)
    {
        spdlog::error("删除 Tag 失败: 数据库未初始化");
        return false;
    }

    const auto found = tags_.find(tag_id);
    if (found == tags_.end())
    {
        return false;
    }

    QString err_msg;
    if (!database_->deleteTagClass(tag_id, err_msg))
    {
        spdlog::error("删除 Tag 失败: {}, error: {}", tag_id, err_msg.toUtf8().constData());
        return false;
    }

    const std::vector<int64_t> image_ids(found->second.imageIds().begin(), found->second.imageIds().end());
    const std::vector<int64_t> label_ids(found->second.labelIds().begin(), found->second.labelIds().end());
    for (const int64_t image_id : image_ids)
    {
        if (ImageInstance *image = image_instances_ ? image_instances_->getImageInstance(image_id) : nullptr)
        {
            image->removeTagIds({tag_id});
        }
    }
    for (const int64_t label_id : label_ids)
    {
        if (LabelInstance *label = label_instances_ ? label_instances_->getLabelInstance(label_id) : nullptr)
        {
            label->removeTagIds({tag_id});
        }
    }

    const int row = rowForTag(tag_id);
    beginRemoveRows(QModelIndex(), row, row);
    tags_.erase(found);
    endRemoveRows();
    updateStats();
    return true;
}

bool ImageTagsListModel::setImagesTag(const std::vector<int64_t> &image_ids, const int64_t tag_id)
{
    return setTags(image_ids, tag_id, TagTarget::Image, true);
}

bool ImageTagsListModel::setImageTag(const int64_t image_id, const int64_t tag_id)
{
    return setImagesTag({image_id}, tag_id);
}

bool ImageTagsListModel::setLabelsTag(const std::vector<int64_t> &label_ids, const int64_t tag_id)
{
    return setTags(label_ids, tag_id, TagTarget::Label, true);
}

bool ImageTagsListModel::setLabelTag(const int64_t label_id, const int64_t tag_id)
{
    return setLabelsTag({label_id}, tag_id);
}

bool ImageTagsListModel::addLabelsTag(const std::vector<int64_t> &label_ids, const int64_t tag_id)
{
    return setTags(label_ids, tag_id, TagTarget::Label, false);
}

bool ImageTagsListModel::removeImagesTags(const std::vector<int64_t> &image_ids)
{
    return removeTags(image_ids, TagTarget::Image);
}

void ImageTagsListModel::removeImagesTagsFromMemory(const std::vector<int64_t> &image_ids)
{
    removeTagsFromMemory(image_ids, TagTarget::Image);
}

void ImageTagsListModel::addImagesTagsFromMemory(const std::vector<int64_t>              &image_ids,
                                                 const std::vector<std::vector<int64_t>> &tag_ids)
{
    const size_t count = std::min(image_ids.size(), tag_ids.size());
    for (size_t i = 0; i < count; ++i)
    {
        if (ImageInstance *image = image_instances_ ? image_instances_->getImageInstance(image_ids[i]) : nullptr)
        {
            image->addTagIds(tag_ids[i]);
        }
        for (const int64_t tag_id : tag_ids[i])
        {
            auto tag = tags_.find(tag_id);
            if (tag != tags_.end())
            {
                tag->second.addImageIds({image_ids[i]});
            }
        }
    }
    updateStats();
}

void ImageTagsListModel::addLabelsTagsFromMemory(const std::vector<int64_t>              &label_ids,
                                                 const std::vector<std::vector<int64_t>> &tag_ids)
{
    const size_t count = std::min(label_ids.size(), tag_ids.size());
    for (size_t i = 0; i < count; ++i)
    {
        if (LabelInstance *label = label_instances_ ? label_instances_->getLabelInstance(label_ids[i]) : nullptr)
        {
            label->addTagIds(tag_ids[i]);
        }
        for (const int64_t tag_id : tag_ids[i])
        {
            auto tag = tags_.find(tag_id);
            if (tag != tags_.end())
            {
                tag->second.addLabelIds({label_ids[i]});
            }
        }
    }
    updateStats();
}

std::vector<std::vector<int64_t>> ImageTagsListModel::getImagesTagIds(
    const std::vector<int64_t> &image_ids) const
{
    std::vector<std::vector<int64_t>> image_tag_ids;
    image_tag_ids.reserve(image_ids.size());
    for (const int64_t image_id : image_ids)
    {
        std::vector<int64_t> tag_ids;
        for (const auto &[tag_id, tag] : tags_)
        {
            if (tag.imageIds().count(image_id) > 0)
            {
                tag_ids.push_back(tag_id);
            }
        }
        image_tag_ids.push_back(std::move(tag_ids));
    }
    return image_tag_ids;
}

void ImageTagsListModel::applyTagsToLabels()
{
    if (label_instances_ == nullptr)
    {
        return;
    }

    for (const auto &[tag_id, tag] : tags_)
    {
        for (const int64_t label_id : tag.labelIds())
        {
            if (LabelInstance *label = label_instances_->getLabelInstance(label_id))
            {
                label->addTagIds({tag_id});
            }
        }
    }
    updateStats();
}

void ImageTagsListModel::updateStats()
{
    if (tags_.empty())
    {
        return;
    }

    emit dataChanged(index(0), index(rowCount() - 1),
                     {SelectedImagesStatsRole, CurrentImageStatsRole, SelectedLabelsStatsRole});
}

QString ImageTagsListModel::getTagClassName(const int64_t tag_id) const
{
    const auto found = tags_.find(tag_id);
    return found == tags_.end() ? QString() : found->second.name();
}

Tag *ImageTagsListModel::getTag(const int64_t tag_id)
{
    const auto found = tags_.find(tag_id);
    return found == tags_.end() ? nullptr : &found->second;
}

int ImageTagsListModel::rowForTag(const int64_t tag_id) const
{
    const auto found = tags_.find(tag_id);
    return found == tags_.end() ? -1 : static_cast<int>(std::distance(tags_.begin(), found));
}

bool ImageTagsListModel::setTags(const std::vector<int64_t> &target_ids, const int64_t tag_id,
                                  const TagTarget target, const bool toggle)
{
    if (database_ == nullptr)
    {
        spdlog::error("设置 Tag 失败: 数据库未初始化");
        return false;
    }
    if (target_ids.empty())
    {
        return true;
    }

    Tag *tag = getTag(tag_id);
    if (tag == nullptr)
    {
        spdlog::error("设置 Tag 失败: Tag {} 不存在", tag_id);
        return false;
    }

    const std::set<int64_t> &tagged_ids = target == TagTarget::Image ? tag->imageIds() : tag->labelIds();
    std::vector<int64_t> untagged_ids   = getUntaggedIds(target_ids, tagged_ids);
    const bool            adding        = !untagged_ids.empty() || !toggle;
    const std::vector<int64_t> &updated_ids = adding ? untagged_ids : target_ids;

    if (updated_ids.empty())
    {
        return true;
    }

    QString err_msg;
    const bool ok = target == TagTarget::Image
                        ? (adding ? database_->addTagsToImages(updated_ids, tag_id, err_msg)
                                  : database_->removeTagsFromImages(updated_ids, tag_id, err_msg))
                        : (adding ? database_->addTagsToLabels(updated_ids, tag_id, err_msg)
                                  : database_->removeTagsFromLabels(updated_ids, tag_id, err_msg));
    if (!ok)
    {
        spdlog::error("{} {} Tag 失败: {}, error: {}", adding ? "添加" : "删除",
                      target == TagTarget::Image ? "图像" : "标注实例", tag_id, err_msg.toUtf8().constData());
        return false;
    }

    for (const int64_t target_id : updated_ids)
    {
        if (target == TagTarget::Image)
        {
            if (ImageInstance *image = image_instances_ ? image_instances_->getImageInstance(target_id) : nullptr)
            {
                adding ? image->addTagIds({tag_id}) : image->removeTagIds({tag_id});
            }
        }
        else if (LabelInstance *label = label_instances_ ? label_instances_->getLabelInstance(target_id) : nullptr)
        {
            adding ? label->addTagIds({tag_id}) : label->removeTagIds({tag_id});
        }
    }

    if (target == TagTarget::Image)
    {
        adding ? tag->addImageIds(updated_ids) : tag->removeImageIds(updated_ids);
    }
    else
    {
        adding ? tag->addLabelIds(updated_ids) : tag->removeLabelIds(updated_ids);
    }

    updateStats();
    return true;
}

bool ImageTagsListModel::removeTags(const std::vector<int64_t> &target_ids, const TagTarget target)
{
    if (database_ == nullptr)
    {
        spdlog::error("删除 Tag 失败: 数据库未初始化");
        return false;
    }
    if (target_ids.empty())
    {
        return true;
    }

    QString err_msg;
    const bool ok = target == TagTarget::Image ? database_->removeTagsForImages(target_ids, err_msg)
                                                : database_->removeTagsForLabels(target_ids, err_msg);
    if (!ok)
    {
        spdlog::error("删除 {} Tag 失败: {}, error: {}", target == TagTarget::Image ? "图像" : "标注实例",
                      target_ids.size(), err_msg.toUtf8().constData());
        return false;
    }

    removeTagsFromMemory(target_ids, target);
    return true;
}

void ImageTagsListModel::removeTagsFromMemory(const std::vector<int64_t> &target_ids, const TagTarget target)
{
    if (target_ids.empty())
    {
        return;
    }

    if (target == TagTarget::Image && image_instances_ != nullptr)
    {
        for (const int64_t image_id : target_ids)
        {
            if (ImageInstance *image = image_instances_->getImageInstance(image_id))
            {
                image->removeAllTagIds();
            }
        }
    }
    else if (target == TagTarget::Label && label_instances_ != nullptr)
    {
        for (const int64_t label_id : target_ids)
        {
            if (LabelInstance *label = label_instances_->getLabelInstance(label_id))
            {
                label->removeAllTagIds();
            }
        }
    }

    for (auto &[_, tag] : tags_)
    {
        if (target == TagTarget::Image)
        {
            tag.removeImageIds(target_ids);
        }
        else
        {
            tag.removeLabelIds(target_ids);
        }
    }
    updateStats();
}

std::vector<int64_t> ImageTagsListModel::getUntaggedIds(const std::vector<int64_t> &target_ids,
                                                         const std::set<int64_t> &tagged_ids)
{
    const std::set<int64_t> unique_target_ids(target_ids.begin(), target_ids.end());
    std::vector<int64_t>    untagged_ids;
    std::set_difference(unique_target_ids.begin(), unique_target_ids.end(), tagged_ids.begin(), tagged_ids.end(),
                        std::back_inserter(untagged_ids));
    return untagged_ids;
}

int64_t ImageTagsListModel::getTagClassId(const QModelIndex &index) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
    {
        return -1;
    }

    auto found = tags_.begin();
    std::advance(found, index.row());
    return found->first;
}

QVariant ImageTagsListModel::getTagClassName(const QModelIndex &index) const
{
    return getTagClassName(getTagClassId(index));
}

QVariant ImageTagsListModel::getSelectedImagesTagStats(const QModelIndex &index) const
{
    if (image_instances_ == nullptr)
    {
        return {};
    }

    const int64_t tag_id = getTagClassId(index);
    int           count{0};
    for (const int64_t image_id : image_instances_->getSelectedImagesId())
    {
        const ImageInstance *image = image_instances_->getImageInstance(image_id);
        if (image != nullptr && image->tagIds().count(tag_id) > 0)
        {
            ++count;
        }
    }
    return count > 0 ? QString("(%1)").arg(count) : QString();
}

QVariant ImageTagsListModel::getCurrentImageTagStats(const QModelIndex &index) const
{
    if (image_instances_ == nullptr)
    {
        return {};
    }

    const ImageInstance *image = image_instances_->getImageInstance(image_instances_->getCurrentImageId());
    const int64_t        tag_id = getTagClassId(index);
    return image != nullptr && image->tagIds().count(tag_id) > 0 ? QStringLiteral("(1)") : QString();
}

QVariant ImageTagsListModel::getSelectedLabelsTagStats(const QModelIndex &index) const
{
    if (label_instances_ == nullptr || image_labels_list_ == nullptr)
    {
        return {};
    }

    const int64_t tag_id = getTagClassId(index);
    int           count{0};
    for (const int64_t label_id : image_labels_list_->getSelectedLabelIds())
    {
        const LabelInstance *label = label_instances_->getLabelInstance(label_id);
        if (label != nullptr && label->tagIds().count(tag_id) > 0)
        {
            ++count;
        }
    }
    return count > 0 ? QString("(%1)").arg(count) : QString();
}

} // namespace dltool::data
