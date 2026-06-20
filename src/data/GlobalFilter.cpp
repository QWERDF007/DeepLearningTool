#include "data/GlobalFilter.h"

#include "data/DataManager.h"
#include "data/DatasetFilterModule.h"
#include "data/Datasets.h"
#include "data/ImageLabelClassFilterModule.h"
#include "data/ImageSearchFilterModule.h"
#include "data/ImageTags.h"
#include "data/LabelSearchFilterModule.h"
#include "data/Images.h"
#include "data/LabelClassFilterModule.h"
#include "data/Labels.h"
#include "data/TagFilterModule.h"

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
    image_search_filter_      = std::make_unique<ImageSearchFilterModule>(data_manager, this);
    label_search_filter_      = std::make_unique<LabelSearchFilterModule>(data_manager, this);

    filter_modules_[FilterType::Dataset]         = dataset_filter_.get();
    filter_modules_[FilterType::Tag]             = tag_filter_.get();
    filter_modules_[FilterType::LabelClass]      = label_class_filter_.get();
    filter_modules_[FilterType::ImageLabelClass] = image_label_class_filter_.get();
    filter_modules_[FilterType::ImageSearch]     = image_search_filter_.get();
    filter_modules_[FilterType::LabelSearch]     = label_search_filter_.get();

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
    return false;
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
    return count;
}

QString GlobalFilter::filterSummary() const
{
    QStringList summary_parts;

    for (const auto &[type, module] : filter_modules_)
    {
        if (!module || !module->isActive())
        {
            continue;
        }

        QString type_name;
        switch (type)
        {
        case FilterType::Dataset:
            type_name = QString("数据集");
            break;
        case FilterType::Tag:
            type_name = QString("标签");
            break;
        case FilterType::LabelClass:
            type_name = QString("类别");
            break;
        case FilterType::ImageLabelClass:
            type_name = QString("图像类别");
            break;
        case FilterType::ImageSearch:
            type_name = QString("图像搜索");
            break;
        case FilterType::LabelSearch:
            type_name = QString("标注搜索");
            break;
        default:
            type_name = QString("未知");
            break;
        }

        summary_parts.append(QString("%1: %2").arg(type_name).arg(module->getActiveCriteria().size()));
    }

    if (summary_parts.isEmpty())
    {
        return QString("无过滤");
    }

    return summary_parts.join(QStringLiteral(", "));
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

    if (image_search_filter_)
    {
        image_search_filter_->selectAll();
        image_search_filter_->setEnabled(false);
        changed = true;
    }

    if (label_search_filter_)
    {
        label_search_filter_->selectAll();
        label_search_filter_->setEnabled(false);
        changed = true;
    }

    if (changed)
    {
        updateFilterCriteria();
        force_apply_ = true;
        applyFilters();
        emit filterStateChanged();
    }
}

void GlobalFilter::refresh()
{
    updateFilterCriteria();
    force_apply_ = true;
    applyFilters();
    emit filterStateChanged();
}

void GlobalFilter::setImageSearchFilterEnabled(bool enabled)
{
    if (!image_search_filter_)
    {
        return;
    }

    if (enabled && !image_search_filter_->hasResults())
    {
        enabled = false;
    }

    const bool was_enabled = image_search_filter_->isEnabled();
    image_search_filter_->setEnabled(enabled);
    if (was_enabled != enabled)
    {
        updateFilterCriteria();
        force_apply_ = true;
        applyFilters();
        emit filterStateChanged();
    }
}

void GlobalFilter::clearImageSearchResults()
{
    if (!image_search_filter_)
    {
        return;
    }

    image_search_filter_->setEnabled(false);
    image_search_filter_->clear();
    updateFilterCriteria();
    force_apply_ = true;
    applyFilters();
    emit filterStateChanged();
}

void GlobalFilter::setImageSearchResults(const std::vector<int64_t> &image_ids, bool enable_filter)
{
    if (!image_search_filter_)
    {
        return;
    }

    image_search_filter_->setCriteria(image_ids);
    image_search_filter_->setEnabled(enable_filter && !image_ids.empty());
    updateFilterCriteria();
    force_apply_ = true;
    applyFilters();
    emit filterStateChanged();
}

bool GlobalFilter::hasImageSearchResults() const
{
    return image_search_filter_ && image_search_filter_->hasResults();
}

bool GlobalFilter::imageSearchFilterEnabled() const
{
    return image_search_filter_ && image_search_filter_->isEnabled();
}

int GlobalFilter::imageSearchResultCount() const
{
    return image_search_filter_ ? image_search_filter_->resultCount() : 0;
}

void GlobalFilter::setLabelSearchFilterEnabled(bool enabled)
{
    if (!label_search_filter_)
    {
        return;
    }

    if (enabled && !label_search_filter_->hasResults())
    {
        enabled = false;
    }

    const bool was_enabled = label_search_filter_->isEnabled();
    label_search_filter_->setEnabled(enabled);
    if (was_enabled != enabled)
    {
        updateFilterCriteria();
        force_apply_ = true;
        applyFilters();
        emit filterStateChanged();
    }
}

void GlobalFilter::clearLabelSearchResults()
{
    if (!label_search_filter_)
    {
        return;
    }

    label_search_filter_->setEnabled(false);
    label_search_filter_->clear();
    updateFilterCriteria();
    force_apply_ = true;
    applyFilters();
    emit filterStateChanged();
}

void GlobalFilter::setLabelSearchResults(const std::vector<int64_t> &label_ids, bool enable_filter)
{
    if (!label_search_filter_)
    {
        return;
    }

    label_search_filter_->setCriteria(label_ids);
    label_search_filter_->setEnabled(enable_filter && !label_ids.empty());
    updateFilterCriteria();
    force_apply_ = true;
    applyFilters();
    emit filterStateChanged();
}

bool GlobalFilter::hasLabelSearchResults() const
{
    return label_search_filter_ && label_search_filter_->hasResults();
}

bool GlobalFilter::labelSearchFilterEnabled() const
{
    return label_search_filter_ && label_search_filter_->isEnabled();
}

int GlobalFilter::labelSearchResultCount() const
{
    return label_search_filter_ ? label_search_filter_->resultCount() : 0;
}

void GlobalFilter::applyFilters()
{
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
                if (label_search_filter_ && label_search_filter_->isEnabled())
                {
                    return label_search_filter_->passesLabel(label_id);
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
    current_criteria_.image_search_ids.clear();
    current_criteria_.label_search_ids.clear();
    current_criteria_.dataset_inverted           = false;
    current_criteria_.tag_inverted               = false;
    current_criteria_.label_class_inverted       = false;
    current_criteria_.image_label_class_inverted = false;
    current_criteria_.image_search_inverted      = false;
    current_criteria_.label_search_inverted      = false;

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

    if (image_search_filter_ && image_search_filter_->isActive())
    {
        current_criteria_.image_search_ids      = image_search_filter_->getActiveCriteria();
        current_criteria_.image_search_inverted = image_search_filter_->isInverted();
    }

    if (label_search_filter_ && label_search_filter_->isActive())
    {
        current_criteria_.label_search_ids      = label_search_filter_->getActiveCriteria();
        current_criteria_.label_search_inverted = label_search_filter_->isInverted();
    }
}

bool GlobalFilter::shouldIncludeImage(int64_t image_id) const
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

    if (image_search_filter_ && image_search_filter_->isEnabled() && !image_search_filter_->passes(image_id))
    {
        return false;
    }

    if (label_search_filter_ && label_search_filter_->isEnabled() && !label_search_filter_->passes(image_id))
    {
        return false;
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
            if (label_search_filter_ && label_search_filter_->isEnabled()
                && !label_search_filter_->passesLabel(label_id))
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

    if (current_criteria_.image_search_ids != previous_criteria_.image_search_ids)
    {
        return true;
    }
    if (current_criteria_.image_search_inverted != previous_criteria_.image_search_inverted)
    {
        return true;
    }

    if (current_criteria_.label_search_ids != previous_criteria_.label_search_ids)
    {
        return true;
    }
    if (current_criteria_.label_search_inverted != previous_criteria_.label_search_inverted)
    {
        return true;
    }

    return false;
}

} // namespace dltool::data
