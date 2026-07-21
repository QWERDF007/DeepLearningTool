#include "data/TagFilterModule.h"

#include "data/DataManager.h"
#include "data/ImageTags.h"
#include "data/Images.h"
#include "data/Labels.h"

namespace dltool::data {

TagFilterModule::TagFilterModule(DataManager *data_manager, QObject *parent)
    : FilterModule(data_manager, parent)
{
}

void TagFilterModule::setCriteria(const std::vector<int64_t> &tag_ids)
{
    selected_tag_ids_.clear();
    selected_tag_ids_.insert(tag_ids.begin(), tag_ids.end());
    inverted_ = false;
    emit criteriaChanged();
}

void TagFilterModule::clear()
{
    if (!selected_tag_ids_.empty() || inverted_)
    {
        selected_tag_ids_.clear();
        inverted_ = false;
        emit criteriaChanged();
    }
}

void TagFilterModule::setEnabled(bool enabled)
{
    if (enabled_ != enabled)
    {
        enabled_ = enabled;
        emit enabledChanged(enabled);
    }
}

bool TagFilterModule::isEnabled() const
{
    return enabled_;
}

bool TagFilterModule::isActive() const
{
    return enabled_;
}

std::unordered_set<int64_t> TagFilterModule::getActiveCriteria() const
{
    if (enabled_)
    {
        return selected_tag_ids_;
    }
    return {};
}

bool TagFilterModule::isInverted() const
{
    return inverted_;
}

bool TagFilterModule::passes(int64_t image_id) const
{
    if (!enabled_)
    {
        return true;
    }

    if (selected_tag_ids_.empty())
    {
        return inverted_;
    }

    DataManager *dm = dataManager();
    if (!dm)
    {
        return false;
    }

    ImageInstancesListModel *image_model = dm->imageInstances();
    LabelInstancesListModel *label_model = dm->labelInstances();
    if (!image_model || !label_model)
    {
        return false;
    }

    ImageInstance *image = image_model->getImageInstance(image_id);
    if (!image)
    {
        return false;
    }

    const auto hasSelectedTag = [this](const std::set<int64_t> &tag_ids)
    {
        for (const int64_t tag_id : selected_tag_ids_)
        {
            if (tag_ids.find(tag_id) != tag_ids.end())
            {
                return true;
            }
        }
        return false;
    };

    bool matches = false;
    for (const int64_t label_id : image->labelIds())
    {
        const LabelInstance *label = label_model->getLabelInstance(label_id);
        if (label != nullptr && hasSelectedTag(label->tagIds()))
        {
            matches = true;
            break;
        }
    }
    if (!matches)
    {
        matches = hasSelectedTag(image->tagIds());
    }

    return inverted_ ? !matches : matches;
}

void TagFilterModule::selectAll()
{
    selected_tag_ids_.clear();
    inverted_ = false;

    DataManager *dm = dataManager();
    if (dm)
    {
        ImageTagsListModel *tags_model = dm->imageTags();
        if (tags_model)
        {
            const int row_count = tags_model->rowCount();
            for (int i = 0; i < row_count; ++i)
            {
                const QModelIndex index  = tags_model->index(i, 0);
                const int64_t     tag_id = tags_model->data(index, ImageTagsListModel::TagIdRole).toLongLong();
                selected_tag_ids_.insert(tag_id);
            }
        }
    }

    emit criteriaChanged();
}

void TagFilterModule::deselectAll()
{
    selected_tag_ids_.clear();
    inverted_ = true;

    DataManager *dm = dataManager();
    if (dm)
    {
        ImageTagsListModel *tags_model = dm->imageTags();
        if (tags_model)
        {
            const int row_count = tags_model->rowCount();
            for (int i = 0; i < row_count; ++i)
            {
                const QModelIndex index  = tags_model->index(i, 0);
                const int64_t     tag_id = tags_model->data(index, ImageTagsListModel::TagIdRole).toLongLong();
                selected_tag_ids_.insert(tag_id);
            }
        }
    }

    emit criteriaChanged();
}

} // namespace dltool::data
