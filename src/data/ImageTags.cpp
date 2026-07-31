#include "data/ImageTags.h"

#include "data/DataViewModels.h"
#include "data/Images.h"
#include "data/Labels.h"
#include "database/DataBase.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <iterator>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

std::vector<uint8_t> tagExtraData(const QString &shortcut)
{
    const QByteArray json = QJsonDocument(QJsonObject{{QStringLiteral("shortcut"), shortcut}})
                                .toJson(QJsonDocument::Compact);
    return {json.cbegin(), json.cend()};
}

QString tagShortcutFromExtraData(const std::vector<uint8_t> &extra_data)
{
    if (extra_data.empty())
        return {};
    const QByteArray bytes(reinterpret_cast<const char *>(extra_data.data()), static_cast<qsizetype>(extra_data.size()));
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    return error.error == QJsonParseError::NoError && document.isObject()
        ? document.object().value(QStringLiteral("shortcut")).toString()
        : QString{};
}

} // namespace

namespace dltool::data {

ImageTagsListModel::ImageTagsListModel(dltool::database::ProjectDataBase *database,
                                       ImageInstancesListModel *image_instances,
                                       ImageInstancesViewModel *image_view,
                                       LabelInstancesListModel *label_instances,
                                       ImageLabelsListModel *image_labels_list, QObject *parent)
    : QAbstractListModel(parent)
    , database_(database)
    , image_instances_(image_instances)
    , image_view_(image_view)
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
    std::vector<std::vector<uint8_t>> extra_data;
    if (!database_->getAllTagClasses(tag_ids, tag_names, extra_data, err_msg))
    {
        spdlog::error("查询 Tag 类别失败: {}", err_msg.toUtf8().constData());
        return false;
    }

    for (size_t i = 0; i < tag_ids.size(); ++i)
    {
        const QString shortcut = i < extra_data.size() ? tagShortcutFromExtraData(extra_data[i]) : QString{};
        tags_.emplace(tag_ids[i], Tag(tag_ids[i], tag_names[i], shortcut));
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
                tag->addImageId(image_ids[i]);
        }
    }
    for (size_t i = 0; i < label_ids.size() && i < label_tag_ids.size(); ++i)
    {
        for (const int64_t tag_id : label_tag_ids[i])
        {
            if (Tag *tag = getTag(tag_id))
                tag->addLabelId(label_ids[i]);
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
    case ShortcutRole:
        return getTagClassShortcut(index);
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
        {           ShortcutRole,              "shortcut"},
        {SelectedImagesStatsRole, "selected_images_stats"},
        {  CurrentImageStatsRole,   "current_image_stats"},
        {SelectedLabelsStatsRole, "selected_labels_stats"},
    };
}

bool ImageTagsListModel::addTagClass(const QString &name, const QString &shortcut)
{
    if (mutation_blocked_)
    {
        return false;
    }
    if (database_ == nullptr)
    {
        spdlog::error("添加 Tag 失败: 数据库未初始化");
        return false;
    }

    QString err_msg;
    int64_t tag_id{-1};
    if (!database_->addTagClass(name, tagExtraData(shortcut), tag_id, err_msg))
    {
        spdlog::error("添加 Tag 失败: {}, error: {}", name.toUtf8().constData(), err_msg.toUtf8().constData());
        return false;
    }

    const int row = rowCount();
    beginInsertRows(QModelIndex(), row, row);
    tags_.emplace(tag_id, Tag(tag_id, name, shortcut));
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

int64_t ImageTagsListModel::findByShortcut(const QString &shortcut) const
{
    if (shortcut.isEmpty())
        return -1;
    for (const auto &[tag_id, tag] : tags_)
    {
        if (tag.shortcut().compare(shortcut, Qt::CaseInsensitive) == 0)
            return tag_id;
    }
    return -1;
}

bool ImageTagsListModel::updateTagClass(const int64_t tag_id, const QString &name, const QString &shortcut)
{
    if (mutation_blocked_)
    {
        return false;
    }
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
    if (tag->name() == name && tag->shortcut() == shortcut)
    {
        return true;
    }

    QString err_msg;
    if (!database_->updateTagClass(tag_id, name, tagExtraData(shortcut), err_msg))
    {
        spdlog::error("更新 Tag 失败: {}, error: {}", tag_id, err_msg.toUtf8().constData());
        return false;
    }

    tag->setName(name);
    tag->setShortcut(shortcut);
    const int row = rowForTag(tag_id);
    if (row >= 0)
    {
        emit dataChanged(index(row), index(row), {NameRole, ShortcutRole});
    }
    return true;
}

bool ImageTagsListModel::deleteTagClass(const int64_t tag_id)
{
    if (mutation_blocked_)
    {
        return false;
    }
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

    for (const int64_t image_id : found->second.imageIds())
    {
        if (ImageInstance *image = image_instances_ ? image_instances_->getImageInstance(image_id) : nullptr)
        {
            image->removeTagId(tag_id);
        }
    }
    for (const int64_t label_id : found->second.labelIds())
    {
        if (LabelInstance *label = label_instances_ ? label_instances_->getLabelInstance(label_id) : nullptr)
        {
            label->removeTagId(tag_id);
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

void ImageTagsListModel::addRelationsFromMemory(const std::vector<LoadedImageInstance> &images,
                                                const std::vector<LoadedLabelInstance> &labels)
{
    for (const LoadedImageInstance &image : images)
    {
        for (const int64_t tag_id : image.tag_ids)
        {
            auto tag = tags_.find(tag_id);
            if (tag != tags_.end())
            {
                tag->second.addImageId(image.image_id);
            }
        }
    }
    for (const LoadedLabelInstance &label : labels)
    {
        for (const int64_t tag_id : label.tag_ids)
        {
            auto tag = tags_.find(tag_id);
            if (tag != tags_.end())
            {
                tag->second.addLabelId(label.label_id);
            }
        }
    }
    updateStats();
}

void ImageTagsListModel::applyTagsToImages()
{
    if (image_instances_ == nullptr)
    {
        return;
    }

    for (const auto &[tag_id, tag] : tags_)
    {
        for (const int64_t image_id : tag.imageIds())
        {
            if (ImageInstance *image = image_instances_->getImageInstance(image_id))
            {
                image->addTagId(tag_id);
            }
        }
    }
    updateStats();
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
                label->addTagId(tag_id);
            }
        }
    }
    updateStats();
}

void ImageTagsListModel::updateStats()
{
    selected_image_tag_counts_.clear();
    selected_label_tag_counts_.clear();

    if (image_instances_ != nullptr && image_view_ != nullptr && image_view_->selection() != nullptr)
    {
        QAbstractItemModel *image_model = image_view_;
        for (const QItemSelectionRange &range : image_view_->selection()->selection())
        {
            const int first = std::max(0, range.top());
            const int last  = std::min(image_model->rowCount() - 1, range.bottom());
            for (int row = first; row <= last; ++row)
            {
                const QModelIndex index = image_model->index(row, 0);
                const int64_t image_id
                    = image_model->data(index, ImageInstancesViewModel::ImageIdRole).toLongLong();
                for (const auto &[tag_id, tag] : tags_)
                {
                    if (tag.imageIds().contains(image_id))
                    {
                        ++selected_image_tag_counts_[tag_id];
                    }
                }
            }
        }
    }

    if (label_instances_ != nullptr && image_labels_list_ != nullptr && image_labels_list_->selection() != nullptr)
    {
        for (const QItemSelectionRange &range : image_labels_list_->selection()->selection())
        {
            const int first = std::max(0, range.top());
            const int last  = std::min(image_labels_list_->rowCount() - 1, range.bottom());
            for (int row = first; row <= last; ++row)
            {
                const QModelIndex index = image_labels_list_->index(row, 0);
                const int64_t label_id
                    = image_labels_list_->data(index, ImageLabelsListModel::LabelIdRole).toLongLong();
                for (const auto &[tag_id, tag] : tags_)
                {
                    if (tag.labelIds().contains(label_id))
                    {
                        ++selected_label_tag_counts_[tag_id];
                    }
                }
            }
        }
    }

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
    if (mutation_blocked_)
    {
        return false;
    }
    if (database_ == nullptr)
    {
        spdlog::error("设置 Tag 失败: 数据库未初始化");
        return false;
    }
    if (target_ids.empty())
    {
        return false;
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
                adding ? image->addTagId(tag_id) : image->removeTagId(tag_id);
            }
        }
        else if (LabelInstance *label = label_instances_ ? label_instances_->getLabelInstance(target_id) : nullptr)
        {
            adding ? label->addTagId(tag_id) : label->removeTagId(tag_id);
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
    if (mutation_blocked_)
    {
        return false;
    }
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
                image->clearTagIds();
            }
        }
    }
    else if (target == TagTarget::Label && label_instances_ != nullptr)
    {
        for (const int64_t label_id : target_ids)
        {
            if (LabelInstance *label = label_instances_->getLabelInstance(label_id))
            {
                label->clearTagIds();
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

QVariant ImageTagsListModel::getTagClassShortcut(const QModelIndex &index) const
{
    const int64_t id = getTagClassId(index);
    const auto found = tags_.find(id);
    return found != tags_.end() ? QVariant(found->second.shortcut()) : QVariant{};
}

QVariant ImageTagsListModel::getSelectedImagesTagStats(const QModelIndex &index) const
{
    const int64_t tag_id = getTagClassId(index);
    const auto found = selected_image_tag_counts_.find(tag_id);
    const int count = found == selected_image_tag_counts_.end() ? 0 : found->second;
    return count > 0 ? QString("(%1)").arg(count) : QString();
}

QVariant ImageTagsListModel::getCurrentImageTagStats(const QModelIndex &index) const
{
    if (image_instances_ == nullptr || image_view_ == nullptr)
    {
        return {};
    }

    const int64_t image_id = image_view_->currentImageId();
    const int64_t tag_id   = getTagClassId(index);
    const auto    found    = tags_.find(tag_id);
    return image_id >= 0 && found != tags_.end() && found->second.imageIds().contains(image_id)
        ? QStringLiteral("(1)")
        : QString();
}

QVariant ImageTagsListModel::getSelectedLabelsTagStats(const QModelIndex &index) const
{
    const int64_t tag_id = getTagClassId(index);
    const auto found = selected_label_tag_counts_.find(tag_id);
    const int count = found == selected_label_tag_counts_.end() ? 0 : found->second;
    return count > 0 ? QString("(%1)").arg(count) : QString();
}

} // namespace dltool::data
