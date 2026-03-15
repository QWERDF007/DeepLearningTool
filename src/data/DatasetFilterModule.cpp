#include "data/DatasetFilterModule.h"

#include "data/Datasets.h"
#include "data/Images.h"

namespace dltool::data {

DatasetFilterModule::DatasetFilterModule(ImageInstancesListModel *image_model, DatasetsListModel *datasets_model,
                                         QObject *parent)
    : FilterModule(parent)
    , image_model_(image_model)
    , datasets_model_(datasets_model)
{
}

void DatasetFilterModule::setCriteria(const std::vector<int64_t> &dataset_ids)
{
    selected_dataset_ids_.clear();
    selected_dataset_ids_.insert(dataset_ids.begin(), dataset_ids.end());
    emit criteriaChanged();
}

void DatasetFilterModule::clear()
{
    if (!selected_dataset_ids_.empty())
    {
        selected_dataset_ids_.clear();
        emit criteriaChanged();
    }
}

void DatasetFilterModule::setEnabled(bool enabled)
{
    if (enabled_ != enabled)
    {
        enabled_ = enabled;
        emit enabledChanged(enabled);
    }
}

bool DatasetFilterModule::isEnabled() const
{
    return enabled_;
}

bool DatasetFilterModule::isActive() const
{
    return enabled_;
}

std::set<int64_t> DatasetFilterModule::getActiveCriteria() const
{
    if (enabled_)
    {
        return selected_dataset_ids_;
    }
    return std::set<int64_t>();
}

bool DatasetFilterModule::passes(int64_t image_id) const
{
    // If filter is disabled, all images pass
    if (!enabled_)
    {
        return true;
    }

    // If no datasets are selected, no images pass
    if (selected_dataset_ids_.empty())
    {
        return false;
    }

    // Get the image instance and check if its dataset is in the selected set
    if (image_model_)
    {
        ImageInstance *image = image_model_->getImageInstance(image_id);
        if (image)
        {
            int64_t dataset_id = image->datasetId();
            return selected_dataset_ids_.find(dataset_id) != selected_dataset_ids_.end();
        }
    }

    return false;
}

void DatasetFilterModule::selectAll()
{
    selected_dataset_ids_.clear();

    if (datasets_model_)
    {
        // Get all dataset IDs from the datasets model
        int row_count = datasets_model_->rowCount();
        for (int i = 0; i < row_count; ++i)
        {
            QModelIndex index      = datasets_model_->index(i, 0);
            int64_t     dataset_id = datasets_model_->data(index, DatasetsListModel::DatasetIdRole).toLongLong();
            selected_dataset_ids_.insert(dataset_id);
        }
    }

    emit criteriaChanged();
}

void DatasetFilterModule::deselectAll()
{
    if (!selected_dataset_ids_.empty())
    {
        selected_dataset_ids_.clear();
        emit criteriaChanged();
    }
}

} // namespace dltool::data
