#include "data/TagFilterModule.h"

#include "data/ImageTags.h"
#include "data/Images.h"

namespace dltool::data {

TagFilterModule::TagFilterModule(ImageInstancesListModel *image_model, ImageTagsListModel *tags_model, QObject *parent)
    : FilterModule(parent)
    , image_model_(image_model)
    , tags_model_(tags_model)
{
}

void TagFilterModule::setCriteria(const std::vector<int64_t> &tag_ids)
{
    selected_tag_ids_.clear();
    selected_tag_ids_.insert(tag_ids.begin(), tag_ids.end());
    emit criteriaChanged();
}

void TagFilterModule::clear()
{
    if (!selected_tag_ids_.empty())
    {
        selected_tag_ids_.clear();
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

std::set<int64_t> TagFilterModule::getActiveCriteria() const
{
    if (enabled_)
    {
        return selected_tag_ids_;
    }
    return std::set<int64_t>();
}

bool TagFilterModule::passes(int64_t image_id) const
{
    // If filter is disabled, all images pass
    if (!enabled_)
    {
        return true;
    }

    // If no tags are selected, no images pass
    if (selected_tag_ids_.empty())
    {
        return false;
    }

    // Get the image instance and check if it has any of the selected tags
    if (image_model_)
    {
        ImageInstance *image = image_model_->getImageInstance(image_id);
        if (image)
        {
            const std::set<int64_t> &image_tag_ids = image->tagIds();

            // Check if any of the image's tags are in the selected tags (OR logic within module)
            for (const auto &tag_id : selected_tag_ids_)
            {
                if (image_tag_ids.find(tag_id) != image_tag_ids.end())
                {
                    return true; // Image has at least one of the selected tags
                }
            }
        }
    }

    return false; // Image doesn't have any of the selected tags
}

void TagFilterModule::selectAll()
{
    selected_tag_ids_.clear();

    if (tags_model_)
    {
        // Get all tag IDs from the tags model
        int row_count = tags_model_->rowCount();
        for (int i = 0; i < row_count; ++i)
        {
            QModelIndex index  = tags_model_->index(i, 0);
            int64_t     tag_id = tags_model_->data(index, ImageTagsListModel::TagIdRole).toLongLong();
            selected_tag_ids_.insert(tag_id);
        }
    }

    emit criteriaChanged();
}

void TagFilterModule::deselectAll()
{
    if (!selected_tag_ids_.empty())
    {
        selected_tag_ids_.clear();
        emit criteriaChanged();
    }
}

} // namespace dltool::data
