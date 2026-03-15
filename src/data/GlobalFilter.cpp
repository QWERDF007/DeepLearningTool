#include "data/GlobalFilter.h"

#include "data/DatasetFilterModule.h"
#include "data/Datasets.h"
#include "data/ImageTags.h"
#include "data/Images.h"
#include "data/Labels.h"
#include "data/TagFilterModule.h"

namespace dltool::data {

GlobalFilter::GlobalFilter(ImageInstancesListModel *image_model, LabelInstancesListModel *label_model, QObject *parent)
    : QObject(parent)
    , image_model_(image_model)
    , label_model_(label_model)
{
    // Filter modules will be initialized later via initializeFilterModules()
    // after DataManager is fully constructed
}

GlobalFilter::~GlobalFilter()
{
    // Unique pointers will automatically clean up
}

void GlobalFilter::initializeFilterModules(DatasetsListModel *datasets_model, ImageTagsListModel *tags_model)
{
    // Initialize dataset filter module
    dataset_filter_ = std::make_unique<DatasetFilterModule>(image_model_, datasets_model, this);

    // Initialize tag filter module
    tag_filter_ = std::make_unique<TagFilterModule>(image_model_, tags_model, this);

    // Connect filter module signals to applyFilters slot
    connect(dataset_filter_.get(), &FilterModule::criteriaChanged, this, &GlobalFilter::applyFilters);
    connect(dataset_filter_.get(), &FilterModule::enabledChanged, this, &GlobalFilter::applyFilters);

    connect(tag_filter_.get(), &FilterModule::criteriaChanged, this, &GlobalFilter::applyFilters);
    connect(tag_filter_.get(), &FilterModule::enabledChanged, this, &GlobalFilter::applyFilters);
}

bool GlobalFilter::isActive() const
{
    return (dataset_filter_ && dataset_filter_->isActive()) || (tag_filter_ && tag_filter_->isActive());
}

int GlobalFilter::activeFilterCount() const
{
    int count = 0;
    if (dataset_filter_ && dataset_filter_->isActive())
    {
        count++;
    }
    if (tag_filter_ && tag_filter_->isActive())
    {
        count++;
    }
    return count;
}

QString GlobalFilter::filterSummary() const
{
    QStringList summary_parts;

    if (dataset_filter_ && dataset_filter_->isActive())
    {
        auto criteria = dataset_filter_->getActiveCriteria();
        summary_parts.append(QString("数据集: %1").arg(criteria.size()));
    }

    if (tag_filter_ && tag_filter_->isActive())
    {
        auto criteria = tag_filter_->getActiveCriteria();
        summary_parts.append(QString("标签: %1").arg(criteria.size()));
    }

    if (summary_parts.isEmpty())
    {
        return "无过滤";
    }

    return summary_parts.join(", ");
}

void GlobalFilter::setDatasetFilter(const std::vector<int64_t> &dataset_ids)
{
    if (dataset_filter_)
    {
        dataset_filter_->setCriteria(dataset_ids);
        updateFilterCriteria();
        applyFilters();
        emit filterStateChanged();
    }
}

void GlobalFilter::setTagFilter(const std::vector<int64_t> &tag_ids)
{
    if (tag_filter_)
    {
        tag_filter_->setCriteria(tag_ids);
        updateFilterCriteria();
        applyFilters();
        emit filterStateChanged();
    }
}

void GlobalFilter::setDatasetFilterEnabled(bool enabled)
{
    if (dataset_filter_)
    {
        dataset_filter_->setEnabled(enabled);
        applyFilters();
        emit filterStateChanged();
    }
}

void GlobalFilter::setTagFilterEnabled(bool enabled)
{
    if (tag_filter_)
    {
        tag_filter_->setEnabled(enabled);
        applyFilters();
        emit filterStateChanged();
    }
}

void GlobalFilter::clearAllFilters()
{
    bool changed = false;

    if (dataset_filter_)
    {
        dataset_filter_->clear();
        dataset_filter_->setEnabled(false);
        changed = true;
    }

    if (tag_filter_)
    {
        tag_filter_->clear();
        tag_filter_->setEnabled(false);
        changed = true;
    }

    if (changed)
    {
        updateFilterCriteria();
        applyFilters();
        emit filterStateChanged();
    }
}

void GlobalFilter::clearDatasetFilter()
{
    if (dataset_filter_)
    {
        dataset_filter_->clear();
        updateFilterCriteria();
        applyFilters();
        emit filterStateChanged();
    }
}

void GlobalFilter::clearTagFilter()
{
    if (tag_filter_)
    {
        tag_filter_->clear();
        updateFilterCriteria();
        applyFilters();
        emit filterStateChanged();
    }
}

std::vector<int64_t> GlobalFilter::getActiveDatasetIds() const
{
    if (dataset_filter_ && dataset_filter_->isActive())
    {
        auto criteria = dataset_filter_->getActiveCriteria();
        return std::vector<int64_t>(criteria.begin(), criteria.end());
    }
    return std::vector<int64_t>();
}

std::vector<int64_t> GlobalFilter::getActiveTagIds() const
{
    if (tag_filter_ && tag_filter_->isActive())
    {
        auto criteria = tag_filter_->getActiveCriteria();
        return std::vector<int64_t>(criteria.begin(), criteria.end());
    }
    return std::vector<int64_t>();
}

void GlobalFilter::applyFilters()
{
    // Check if any filter is active
    if (!isActive())
    {
        // No filters active, clear any existing filters on models
        if (image_model_)
        {
            image_model_->clearFilter();
        }
        if (label_model_)
        {
            label_model_->clearFilter();
        }
        emit filterApplied();
        return;
    }

    // Apply filter to image model
    if (image_model_)
    {
        // Create a lambda that captures this and checks if an image should be included
        auto image_filter_func = [this](int64_t image_id) -> bool
        {
            return shouldIncludeImage(image_id);
        };

        image_model_->applyFilter(image_filter_func);
    }

    // Apply filter to label model
    if (label_model_)
    {
        // Create a lambda that checks if an image should be included
        // Note: LabelInstancesListModel expects a function that takes image_id, not label_id
        auto image_filter_func = [this](int64_t image_id) -> bool
        {
            return shouldIncludeImage(image_id);
        };

        label_model_->applyFilter(image_filter_func);
    }

    emit filterApplied();
}

void GlobalFilter::updateFilterCriteria()
{
    // Update the current_criteria_ structure based on active filter modules
    current_criteria_.dataset_ids.clear();
    current_criteria_.tag_ids.clear();

    if (dataset_filter_ && dataset_filter_->isActive())
    {
        current_criteria_.dataset_ids = dataset_filter_->getActiveCriteria();
    }

    if (tag_filter_ && tag_filter_->isActive())
    {
        current_criteria_.tag_ids = tag_filter_->getActiveCriteria();
    }
}

bool GlobalFilter::shouldIncludeImage(int64_t image_id) const
{
    // An image is included if it passes ALL enabled filter modules (AND logic)

    // Check dataset filter (if enabled)
    if (dataset_filter_ && dataset_filter_->isEnabled())
    {
        if (!dataset_filter_->passes(image_id))
        {
            return false; // Failed dataset filter
        }
    }

    // Check tag filter (if enabled)
    if (tag_filter_ && tag_filter_->isEnabled())
    {
        if (!tag_filter_->passes(image_id))
        {
            return false; // Failed tag filter
        }
    }

    // Passed all enabled filters (or no filters enabled)
    return true;
}

bool GlobalFilter::shouldIncludeLabel(int64_t label_id) const
{
    // A label is included if its associated image passes the image filter
    if (label_model_)
    {
        int64_t image_id = label_model_->getImageId(label_id);
        return shouldIncludeImage(image_id);
    }

    return false;
}

} // namespace dltool::data
