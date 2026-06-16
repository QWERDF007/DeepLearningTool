#include "data/LabelSearchFilterModule.h"

#include "data/DataManager.h"
#include "data/Labels.h"

namespace dltool::data {

LabelSearchFilterModule::LabelSearchFilterModule(DataManager *data_manager, QObject *parent)
    : FilterModule(data_manager, parent)
{
}

void LabelSearchFilterModule::setCriteria(const std::vector<int64_t> &label_ids)
{
    result_label_ids_.clear();
    result_label_ids_.insert(label_ids.begin(), label_ids.end());
    rebuildImageIds();
    inverted_ = false;
    emit criteriaChanged();
}

void LabelSearchFilterModule::clear()
{
    if (!result_label_ids_.empty() || !result_image_ids_.empty() || inverted_)
    {
        result_label_ids_.clear();
        result_image_ids_.clear();
        inverted_ = false;
        emit criteriaChanged();
    }
}

void LabelSearchFilterModule::setEnabled(bool enabled)
{
    if (enabled_ != enabled)
    {
        enabled_ = enabled;
        emit enabledChanged(enabled);
    }
}

bool LabelSearchFilterModule::isEnabled() const
{
    return enabled_;
}

bool LabelSearchFilterModule::isActive() const
{
    return enabled_ && !result_label_ids_.empty();
}

std::unordered_set<int64_t> LabelSearchFilterModule::getActiveCriteria() const
{
    if (enabled_)
    {
        return result_label_ids_;
    }
    return {};
}

bool LabelSearchFilterModule::isInverted() const
{
    return inverted_;
}

bool LabelSearchFilterModule::passes(int64_t image_id) const
{
    if (!enabled_)
    {
        return true;
    }

    const bool matches = result_image_ids_.find(image_id) != result_image_ids_.end();
    return inverted_ ? !matches : matches;
}

bool LabelSearchFilterModule::passesLabel(int64_t label_id) const
{
    if (!enabled_)
    {
        return true;
    }

    const bool matches = result_label_ids_.find(label_id) != result_label_ids_.end();
    return inverted_ ? !matches : matches;
}

void LabelSearchFilterModule::selectAll()
{
    inverted_ = false;
    emit criteriaChanged();
}

void LabelSearchFilterModule::deselectAll()
{
    inverted_ = true;
    emit criteriaChanged();
}

int LabelSearchFilterModule::resultCount() const
{
    return static_cast<int>(result_label_ids_.size());
}

bool LabelSearchFilterModule::hasResults() const
{
    return !result_label_ids_.empty();
}

void LabelSearchFilterModule::rebuildImageIds()
{
    result_image_ids_.clear();

    auto *manager = dataManager();
    if (manager == nullptr || manager->labelInstances() == nullptr)
    {
        return;
    }

    const std::vector<int64_t> label_ids(result_label_ids_.begin(), result_label_ids_.end());
    const auto                 image_ids = manager->labelInstances()->getImageIds(label_ids);
    result_image_ids_.insert(image_ids.begin(), image_ids.end());
}

} // namespace dltool::data
