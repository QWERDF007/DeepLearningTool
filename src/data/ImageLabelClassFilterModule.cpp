#include "data/ImageLabelClassFilterModule.h"

#include "data/DataManager.h"
#include "data/Images.h"
#include "data/LabelClasses.h"
#include "data/Labels.h"

namespace dltool::data {

ImageLabelClassFilterModule::ImageLabelClassFilterModule(DataManager *data_manager, QObject *parent)
    : FilterModule(data_manager, parent)
{
}

void ImageLabelClassFilterModule::setCriteria(const std::vector<int64_t> &label_class_ids)
{
    selected_label_class_ids_.clear();
    selected_label_class_ids_.insert(label_class_ids.begin(), label_class_ids.end());
    inverted_ = false;
    emit criteriaChanged();
}

void ImageLabelClassFilterModule::clear()
{
    if (!selected_label_class_ids_.empty() || inverted_)
    {
        selected_label_class_ids_.clear();
        inverted_ = false;
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
    return {};
}

bool ImageLabelClassFilterModule::isInverted() const
{
    return inverted_;
}

bool ImageLabelClassFilterModule::passes(int64_t image_id) const
{
    if (!enabled_)
    {
        return true;
    }

    if (selected_label_class_ids_.empty())
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

    bool matches = false;
    for (const auto &label_id : image->labelIds())
    {
        const int64_t label_class_id = label_model->getLabelClassId(label_id);
        if (selected_label_class_ids_.find(label_class_id) != selected_label_class_ids_.end())
        {
            matches = true;
            break;
        }
    }

    return inverted_ ? !matches : matches;
}

void ImageLabelClassFilterModule::selectAll()
{
    selected_label_class_ids_.clear();
    inverted_ = false;

    DataManager *dm = dataManager();
    if (dm)
    {
        LabelClassesListModel *label_classes_model = dm->labelClasses();
        if (label_classes_model)
        {
            const int row_count = label_classes_model->rowCount();
            for (int i = 0; i < row_count; ++i)
            {
                const QModelIndex index = label_classes_model->index(i, 0);
                const int64_t     id
                    = label_classes_model->data(index, LabelClassesListModel::LabelClassIdRole).toLongLong();
                selected_label_class_ids_.insert(id);
            }
        }
    }

    emit criteriaChanged();
}

void ImageLabelClassFilterModule::deselectAll()
{
    selected_label_class_ids_.clear();
    inverted_ = true;

    DataManager *dm = dataManager();
    if (dm)
    {
        LabelClassesListModel *label_classes_model = dm->labelClasses();
        if (label_classes_model)
        {
            const int row_count = label_classes_model->rowCount();
            for (int i = 0; i < row_count; ++i)
            {
                const QModelIndex index = label_classes_model->index(i, 0);
                const int64_t     id
                    = label_classes_model->data(index, LabelClassesListModel::LabelClassIdRole).toLongLong();
                selected_label_class_ids_.insert(id);
            }
        }
    }

    emit criteriaChanged();
}

} // namespace dltool::data
