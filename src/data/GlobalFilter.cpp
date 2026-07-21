#include "data/GlobalFilter.h"

#include "data/CustomFilterModule.h"
#include "data/DataManager.h"
#include "data/DatasetFilterModule.h"
#include "data/Datasets.h"
#include "data/ImageLabelClassFilterModule.h"
#include "data/ImageTags.h"
#include "data/Images.h"
#include "data/LabelClassFilterModule.h"
#include "data/Labels.h"
#include "data/TagFilterModule.h"

#include <QScopedValueRollback>
#include <QStringList>

namespace dltool::data {

GlobalFilter::GlobalFilter(DataManager *data_manager, QObject *parent)
    : QObject(parent)
    , data_manager_(data_manager)
{
}

GlobalFilter::~GlobalFilter() {}

void GlobalFilter::initializeFilterModules(DataManager *data_manager)
{
    if (!data_manager)
    {
        qWarning() << "GlobalFilter: initializeFilterModules called with null DataManager";
        return;
    }

    dataset_filter_           = std::make_unique<DatasetFilterModule>(data_manager, this);
    tag_filter_               = std::make_unique<TagFilterModule>(data_manager, this);
    label_class_filter_       = std::make_unique<LabelClassFilterModule>(data_manager, this);
    image_label_class_filter_ = std::make_unique<ImageLabelClassFilterModule>(data_manager, this);
    custom_filter_            = std::make_unique<CustomFilterModule>(data_manager, this);

    filter_modules_[FilterType::Dataset]         = dataset_filter_.get();
    filter_modules_[FilterType::Tag]             = tag_filter_.get();
    filter_modules_[FilterType::LabelClass]      = label_class_filter_.get();
    filter_modules_[FilterType::ImageLabelClass] = image_label_class_filter_.get();
    filter_modules_[FilterType::Custom]          = custom_filter_.get();

    for (auto &[type, module] : filter_modules_)
    {
        Q_UNUSED(type)
        connect(module, &FilterModule::criteriaChanged, this, &GlobalFilter::applyFilters);
        connect(module, &FilterModule::enabledChanged, this, &GlobalFilter::applyFilters);
    }
}

FilterModule *GlobalFilter::getFilterModule(FilterType type) const
{
    auto it = filter_modules_.find(type);
    if (it == filter_modules_.end())
    {
        qWarning() << "GlobalFilter: Invalid filter type requested:" << static_cast<int>(type);
        return nullptr;
    }
    return it->second;
}

void GlobalFilter::setFilter(FilterType type, const std::vector<int64_t> &ids)
{
    FilterModule *module = getFilterModule(type);
    if (!module)
    {
        qWarning() << "GlobalFilter: Cannot set filter for invalid type:" << static_cast<int>(type);
        return;
    }

    module->setCriteria(ids);
    updateFilterCriteria();
    applyFilters();
    emit filterStateChanged();
}

void GlobalFilter::setFilterEnabled(FilterType type, bool enabled)
{
    FilterModule *module = getFilterModule(type);
    if (!module)
    {
        qWarning() << "GlobalFilter: Cannot set filter enabled for invalid type:" << static_cast<int>(type);
        return;
    }

    const bool was_enabled = module->isEnabled();
    module->setEnabled(enabled);

    if (was_enabled != enabled)
    {
        updateFilterCriteria();
        force_apply_ = true;
        applyFilters();
    }

    emit filterStateChanged();
}

bool GlobalFilter::isFilterEnabled(FilterType type) const
{
    FilterModule *module = getFilterModule(type);
    return module && module->isEnabled();
}

bool GlobalFilter::isFilterInverted(FilterType type) const
{
    FilterModule *module = getFilterModule(type);
    return module && module->isInverted();
}

void GlobalFilter::clearFilter(FilterType type)
{
    FilterModule *module = getFilterModule(type);
    if (!module)
    {
        qWarning() << "GlobalFilter: Cannot clear filter for invalid type:" << static_cast<int>(type);
        return;
    }

    module->clear();
    updateFilterCriteria();
    applyFilters();
    emit filterStateChanged();
    if (type == FilterType::Custom)
    {
        emit customFilterSearchResultsChanged(false, false);
    }
}

void GlobalFilter::selectAll(FilterType type)
{
    FilterModule *module = getFilterModule(type);
    if (!module)
    {
        qWarning() << "GlobalFilter: Cannot select all for invalid type:" << static_cast<int>(type);
        return;
    }

    module->selectAll();
    updateFilterCriteria();
    force_apply_ = true;
    applyFilters();
    emit filterStateChanged();
}

void GlobalFilter::deselectAll(FilterType type)
{
    FilterModule *module = getFilterModule(type);
    if (!module)
    {
        qWarning() << "GlobalFilter: Cannot deselect all for invalid type:" << static_cast<int>(type);
        return;
    }

    module->deselectAll();
    updateFilterCriteria();
    force_apply_ = true;
    applyFilters();
    emit filterStateChanged();
}

std::vector<int64_t> GlobalFilter::getActiveIds(FilterType type) const
{
    FilterModule *module = getFilterModule(type);
    if (!module || !module->isActive())
    {
        return {};
    }

    auto criteria = module->getActiveCriteria();
    return std::vector<int64_t>(criteria.begin(), criteria.end());
}

bool GlobalFilter::isActive() const
{
    for (const auto &[type, module] : filter_modules_)
    {
        Q_UNUSED(type)
        if (module && module->isActive())
        {
            return true;
        }
    }
    return !file_name_filter_text_.isEmpty();
}

int GlobalFilter::activeFilterCount() const
{
    int count = 0;
    for (const auto &[type, module] : filter_modules_)
    {
        Q_UNUSED(type)
        if (module && module->isActive())
        {
            count++;
        }
    }
    if (!file_name_filter_text_.isEmpty())
    {
        count++;
    }
    return count;
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

    if (label_class_filter_)
    {
        label_class_filter_->clear();
        label_class_filter_->setEnabled(false);
        changed = true;
    }

    if (image_label_class_filter_)
    {
        image_label_class_filter_->clear();
        image_label_class_filter_->setEnabled(false);
        changed = true;
    }

    if (custom_filter_)
    {
        custom_filter_->clear();
        custom_filter_->setEnabled(false);
        changed = true;
    }

    if (!file_name_filter_text_.isEmpty())
    {
        file_name_filter_text_.clear();
        changed = true;
    }

    if (changed)
    {
        updateFilterCriteria();
        force_apply_ = true;
        applyFilters();
        emit filterStateChanged();
        emit customFilterSearchResultsChanged(false, false);
    }
}

void GlobalFilter::refresh()
{
    updateFilterCriteria();
    force_apply_ = true;
    applyFilters();
    emit filterStateChanged();
}

void GlobalFilter::clearImageSearchResults()
{
    if (!custom_filter_)
    {
        return;
    }

    custom_filter_->clearImageSearchResults();
    updateFilterCriteria();
    force_apply_ = true;
    applyFilters();
    emit filterStateChanged();
    emit customFilterSearchResultsChanged(custom_filter_->hasImageSearchResults(),
                                          custom_filter_->hasLabelSearchResults());
}

void GlobalFilter::setImageSearchResults(const std::vector<int64_t> &image_ids, bool enable_filter)
{
    if (!custom_filter_)
    {
        return;
    }

    custom_filter_->setImageSearchResults(image_ids, enable_filter);
    updateFilterCriteria();
    force_apply_ = true;
    applyFilters();
    emit filterStateChanged();
    emit customFilterSearchResultsChanged(custom_filter_->hasImageSearchResults(),
                                          custom_filter_->hasLabelSearchResults());
}

void GlobalFilter::clearLabelSearchResults()
{
    if (!custom_filter_)
    {
        return;
    }

    custom_filter_->clearLabelSearchResults();
    updateFilterCriteria();
    force_apply_ = true;
    applyFilters();
    emit filterStateChanged();
    emit customFilterSearchResultsChanged(custom_filter_->hasImageSearchResults(),
                                          custom_filter_->hasLabelSearchResults());
}

void GlobalFilter::setLabelSearchResults(const std::vector<int64_t> &label_ids, bool enable_filter)
{
    if (!custom_filter_)
    {
        return;
    }

    custom_filter_->setLabelSearchResults(label_ids, enable_filter);
    updateFilterCriteria();
    force_apply_ = true;
    applyFilters();
    emit filterStateChanged();
    emit customFilterSearchResultsChanged(custom_filter_->hasImageSearchResults(),
                                          custom_filter_->hasLabelSearchResults());
}

void GlobalFilter::setFileNameFilterText(const QString &text)
{
    const QString normalized_text = text.trimmed();
    if (file_name_filter_text_ == normalized_text)
    {
        return;
    }

    file_name_filter_text_ = normalized_text;
    updateFilterCriteria();
    force_apply_ = true;
    applyFilters();
    emit filterStateChanged();
}

void GlobalFilter::applyFilters()
{
    if (applying_filters_)
    {
        return;
    }
    QScopedValueRollback<bool> applying_guard(applying_filters_, true);

    const bool should_force_apply = force_apply_;
    force_apply_                  = false;

    if (!isActive())
    {
        if (data_manager_)
        {
            if (auto *image_model = data_manager_->imageInstances())
            {
                image_model->clearFilter();
            }
            if (auto *label_model = data_manager_->labelInstances())
            {
                label_model->clearFilter();
            }
        }

        previous_criteria_ = FilterCriteria();

        emit filterApplied();
        return;
    }

    if (!should_force_apply && !hasFilterCriteriaChanged())
    {
        return;
    }

    if (data_manager_)
    {
        if (auto *image_model = data_manager_->imageInstances())
        {
            if (custom_filter_ && custom_filter_->usesRegularConditions())
            {
                std::vector<int64_t> candidate_ids;
                for (const int64_t image_id : image_model->getAllImageIds())
                {
                    if (shouldIncludeImageWithoutCustom(image_id))
                    {
                        candidate_ids.push_back(image_id);
                    }
                }
                custom_filter_->prepare(candidate_ids);
            }

            auto image_filter_func = [this](int64_t image_id) -> bool
            {
                return shouldIncludeImage(image_id);
            };

            image_model->applyFilter(image_filter_func);
        }
    }

    if (data_manager_)
    {
        if (auto *label_model = data_manager_->labelInstances())
        {
            auto image_filter_func = [this](int64_t image_id) -> bool
            {
                return shouldIncludeImage(image_id);
            };

            auto label_class_filter_func = [this](int64_t label_class_id) -> bool
            {
                if (label_class_filter_ && label_class_filter_->isEnabled())
                {
                    return label_class_filter_->passes(label_class_id);
                }
                return true;
            };

            auto label_filter_func = [this](int64_t label_id) -> bool
            {
                if (custom_filter_ && custom_filter_->isEnabled())
                {
                    return custom_filter_->passesLabel(label_id);
                }
                return true;
            };

            label_model->applyFilter(image_filter_func, label_class_filter_func, label_filter_func);
        }
    }

    previous_criteria_ = current_criteria_;

    emit filterApplied();
}

void GlobalFilter::updateFilterCriteria()
{
    current_criteria_.dataset_ids.clear();
    current_criteria_.tag_ids.clear();
    current_criteria_.label_class_ids.clear();
    current_criteria_.image_label_class_ids.clear();
    current_criteria_.custom_condition_ids.clear();
    current_criteria_.file_name_text.clear();
    current_criteria_.dataset_inverted           = false;
    current_criteria_.tag_inverted               = false;
    current_criteria_.label_class_inverted       = false;
    current_criteria_.image_label_class_inverted = false;
    current_criteria_.custom_condition_inverted  = false;

    if (dataset_filter_ && dataset_filter_->isActive())
    {
        current_criteria_.dataset_ids      = dataset_filter_->getActiveCriteria();
        current_criteria_.dataset_inverted = dataset_filter_->isInverted();
    }

    if (tag_filter_ && tag_filter_->isActive())
    {
        current_criteria_.tag_ids      = tag_filter_->getActiveCriteria();
        current_criteria_.tag_inverted = tag_filter_->isInverted();
    }

    if (label_class_filter_ && label_class_filter_->isActive())
    {
        current_criteria_.label_class_ids      = label_class_filter_->getActiveCriteria();
        current_criteria_.label_class_inverted = label_class_filter_->isInverted();
    }

    if (image_label_class_filter_ && image_label_class_filter_->isActive())
    {
        current_criteria_.image_label_class_ids      = image_label_class_filter_->getActiveCriteria();
        current_criteria_.image_label_class_inverted = image_label_class_filter_->isInverted();
    }

    if (custom_filter_ && custom_filter_->isActive())
    {
        current_criteria_.custom_condition_ids      = custom_filter_->getActiveCriteria();
        current_criteria_.custom_condition_inverted = custom_filter_->isInverted();
    }

    current_criteria_.file_name_text = file_name_filter_text_;
}

bool GlobalFilter::shouldIncludeImage(int64_t image_id) const
{
    if (!shouldIncludeImageWithoutCustom(image_id))
    {
        return false;
    }

    if (custom_filter_ && custom_filter_->isEnabled() && !custom_filter_->passes(image_id))
    {
        return false;
    }

    return true;
}

bool GlobalFilter::shouldIncludeImageWithoutCustom(int64_t image_id) const
{
    if (dataset_filter_ && dataset_filter_->isEnabled() && !dataset_filter_->passes(image_id))
    {
        return false;
    }

    if (tag_filter_ && tag_filter_->isEnabled() && !tag_filter_->passes(image_id))
    {
        return false;
    }

    if (image_label_class_filter_ && image_label_class_filter_->isEnabled()
        && !image_label_class_filter_->passes(image_id))
    {
        return false;
    }

    if (!file_name_filter_text_.isEmpty())
    {
        if (!data_manager_ || !data_manager_->imageInstances())
        {
            return false;
        }

        const QString file_path = data_manager_->imageInstances()->getImagePath(image_id);
        if (!file_path.contains(file_name_filter_text_, Qt::CaseInsensitive))
        {
            return false;
        }
    }

    return true;
}

bool GlobalFilter::shouldIncludeLabel(int64_t label_id) const
{
    if (data_manager_)
    {
        if (auto *label_model = data_manager_->labelInstances())
        {
            int64_t image_id = label_model->getImageId(label_id);
            if (custom_filter_ && custom_filter_->isEnabled() && !custom_filter_->passesLabel(label_id))
            {
                return false;
            }
            return shouldIncludeImage(image_id);
        }
    }

    return false;
}

bool GlobalFilter::hasFilterCriteriaChanged() const
{
    if (current_criteria_.dataset_ids != previous_criteria_.dataset_ids)
    {
        return true;
    }
    if (current_criteria_.dataset_inverted != previous_criteria_.dataset_inverted)
    {
        return true;
    }

    if (current_criteria_.tag_ids != previous_criteria_.tag_ids)
    {
        return true;
    }
    if (current_criteria_.tag_inverted != previous_criteria_.tag_inverted)
    {
        return true;
    }

    if (current_criteria_.label_class_ids != previous_criteria_.label_class_ids)
    {
        return true;
    }
    if (current_criteria_.label_class_inverted != previous_criteria_.label_class_inverted)
    {
        return true;
    }

    if (current_criteria_.image_label_class_ids != previous_criteria_.image_label_class_ids)
    {
        return true;
    }
    if (current_criteria_.image_label_class_inverted != previous_criteria_.image_label_class_inverted)
    {
        return true;
    }

    if (current_criteria_.custom_condition_ids != previous_criteria_.custom_condition_ids)
    {
        return true;
    }
    if (current_criteria_.custom_condition_inverted != previous_criteria_.custom_condition_inverted)
    {
        return true;
    }

    if (current_criteria_.file_name_text != previous_criteria_.file_name_text)
    {
        return true;
    }

    return false;
}

} // namespace dltool::data
