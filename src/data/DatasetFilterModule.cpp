#include "data/DatasetFilterModule.h"

#include "data/DataManager.h"
#include "data/Datasets.h"
#include "data/Images.h"

namespace dltool::data {

DatasetFilterModule::DatasetFilterModule(DataManager *data_manager, QObject *parent)
    : FilterModule(data_manager, parent)
{
}

void DatasetFilterModule::setCriteria(const std::vector<int64_t> &dataset_ids)
{
    selected_dataset_ids_.clear();
    selected_dataset_ids_.insert(dataset_ids.begin(), dataset_ids.end());
    inverted_ = false;
    emit criteriaChanged();
}

void DatasetFilterModule::clear()
{
    if (!selected_dataset_ids_.empty() || inverted_)
    {
        selected_dataset_ids_.clear();
        inverted_ = false;
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

std::unordered_set<int64_t> DatasetFilterModule::getActiveCriteria() const
{
    if (enabled_)
    {
        return selected_dataset_ids_;
    }
    return std::unordered_set<int64_t>();
}

bool DatasetFilterModule::isInverted() const
{
    return inverted_;
}

bool DatasetFilterModule::passes(int64_t image_id) const
{
    // 如果过滤器禁用，所有图像都通过
    if (!enabled_)
    {
        return true;
    }

    // 如果没有选中任何数据集，正向过滤不通过；反向过滤表示“不在空条件内”，全部通过。
    if (selected_dataset_ids_.empty())
    {
        return inverted_;
    }

    DataManager *dm = dataManager();
    if (!dm)
    {
        return false;
    }

    ImageInstancesListModel *image_model = dm->imageInstances();
    if (!image_model)
    {
        return false;
    }

    ImageInstance *image = image_model->getImageInstance(image_id);
    if (!image)
    {
        return false;
    }

    const int64_t dataset_id = image->datasetId();
    const bool    matches    = selected_dataset_ids_.find(dataset_id) != selected_dataset_ids_.end();
    return inverted_ ? !matches : matches;
}

void DatasetFilterModule::selectAll()
{
    selected_dataset_ids_.clear();
    inverted_ = false;

    DataManager *dm = dataManager();
    if (dm)
    {
        DatasetsListModel *datasets_model = dm->datasets();
        if (datasets_model)
        {
            const int row_count = datasets_model->rowCount();
            for (int i = 0; i < row_count; ++i)
            {
                const QModelIndex index  = datasets_model->index(i, 0);
                const int64_t dataset_id = datasets_model->data(index, DatasetsListModel::DatasetIdRole).toLongLong();
                selected_dataset_ids_.insert(dataset_id);
            }
        }
    }

    emit criteriaChanged();
}

void DatasetFilterModule::deselectAll()
{
    selected_dataset_ids_.clear();
    inverted_ = true;

    DataManager *dm = dataManager();
    if (dm)
    {
        DatasetsListModel *datasets_model = dm->datasets();
        if (datasets_model)
        {
            const int row_count = datasets_model->rowCount();
            for (int i = 0; i < row_count; ++i)
            {
                const QModelIndex index  = datasets_model->index(i, 0);
                const int64_t dataset_id = datasets_model->data(index, DatasetsListModel::DatasetIdRole).toLongLong();
                selected_dataset_ids_.insert(dataset_id);
            }
        }
    }

    emit criteriaChanged();
}

} // namespace dltool::data
