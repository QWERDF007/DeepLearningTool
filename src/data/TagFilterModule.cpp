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

std::unordered_set<int64_t> TagFilterModule::getActiveCriteria() const
{
    if (enabled_)
    {
        return selected_tag_ids_;
    }
    return std::unordered_set<int64_t>();
}

bool TagFilterModule::passes(int64_t image_id) const
{
    // 如果过滤器禁用，所有图像都通过
    if (!enabled_)
    {
        return true;
    }

    // 如果没有选中任何标签，没有图像通过
    if (selected_tag_ids_.empty())
    {
        return false;
    }

    // 获取图像实例并检查它是否拥有任一选中的标签
    if (image_model_)
    {
        ImageInstance *image = image_model_->getImageInstance(image_id);
        if (image)
        {
            const std::set<int64_t> &image_tag_ids = image->tagIds();

            // 检查图像的标签中是否有任一选中的标签（模块内OR逻辑）
            for (const auto &tag_id : selected_tag_ids_)
            {
                if (image_tag_ids.find(tag_id) != image_tag_ids.end())
                {
                    return true; // 图像至少拥有一个选中的标签
                }
            }
        }
    }

    return false; // 图像没有任何选中的标签
}

void TagFilterModule::selectAll()
{
    selected_tag_ids_.clear();

    if (tags_model_)
    {
        // 从标签模型获取所有标签ID
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
