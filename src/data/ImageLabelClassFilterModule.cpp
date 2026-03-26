#include "data/ImageLabelClassFilterModule.h"

#include "data/Images.h"
#include "data/LabelClasses.h"
#include "data/Labels.h"

namespace dltool::data {

ImageLabelClassFilterModule::ImageLabelClassFilterModule(ImageInstancesListModel *image_model,
                                                         LabelInstancesListModel *label_model,
                                                         LabelClassesListModel   *label_classes_model,
                                                         QObject *parent)
    : FilterModule(parent)
    , image_model_(image_model)
    , label_model_(label_model)
    , label_classes_model_(label_classes_model)
{
}

void ImageLabelClassFilterModule::setCriteria(const std::vector<int64_t> &label_class_ids)
{
    selected_label_class_ids_.clear();

    if (!label_class_ids.empty())
    {
        selected_label_class_ids_.insert(label_class_ids.begin(), label_class_ids.end());
    }

    emit criteriaChanged();
}

void ImageLabelClassFilterModule::clear()
{
    if (!selected_label_class_ids_.empty())
    {
        selected_label_class_ids_.clear();
        emit criteriaChanged();
    }
}

void ImageLabelClassFilterModule::setEnabled(bool enabled)
{
    if (enabled_ != enabled)
    {
        enabled_ = enabled;
        emit enabledChanged(enabled);
    }
}

bool ImageLabelClassFilterModule::isEnabled() const
{
    return enabled_;
}

bool ImageLabelClassFilterModule::isActive() const
{
    return enabled_;
}

std::unordered_set<int64_t> ImageLabelClassFilterModule::getActiveCriteria() const
{
    if (enabled_)
    {
        return selected_label_class_ids_;
    }
    return std::unordered_set<int64_t>();
}

bool ImageLabelClassFilterModule::passes(int64_t image_id) const
{
    if (!enabled_)
    {
        return true;
    }

    if (selected_label_class_ids_.empty())
    {
        return false;
    }

    if (!image_model_ || !label_model_)
    {
        return false;
    }

    ImageInstance *image = image_model_->getImageInstance(image_id);
    if (!image)
    {
        return false;
    }

    const std::set<int64_t> &label_ids = image->labelIds();
    for (const auto &label_id : label_ids)
    {
        const int64_t label_class_id = label_model_->getLabelClassId(label_id);
        if (selected_label_class_ids_.find(label_class_id) != selected_label_class_ids_.end())
        {
            return true;
        }
    }

    return false;
}

void ImageLabelClassFilterModule::selectAll()
{
    selected_label_class_ids_.clear();

    if (label_classes_model_)
    {
        const int row_count = label_classes_model_->rowCount();
        for (int i = 0; i < row_count; ++i)
        {
            const QModelIndex index = label_classes_model_->index(i, 0);
            const int64_t     id
                = label_classes_model_->data(index, LabelClassesListModel::LabelClassIdRole).toLongLong();
            selected_label_class_ids_.insert(id);
        }
    }

    emit criteriaChanged();
}

void ImageLabelClassFilterModule::deselectAll()
{
    clear();
}

} // namespace dltool::data
