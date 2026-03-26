#include "data/LabelClassFilterModule.h"

namespace dltool::data {

LabelClassFilterModule::LabelClassFilterModule(QObject *parent)
    : FilterModule(parent)
{
}

void LabelClassFilterModule::setCriteria(const std::vector<int64_t> &label_class_ids)
{
    selected_label_class_ids_.clear();

    if (!label_class_ids.empty())
    {
        selected_label_class_ids_.insert(label_class_ids.front());
    }

    emit criteriaChanged();
}

void LabelClassFilterModule::clear()
{
    if (!selected_label_class_ids_.empty())
    {
        selected_label_class_ids_.clear();
        emit criteriaChanged();
    }
}

void LabelClassFilterModule::setEnabled(bool enabled)
{
    if (enabled_ != enabled)
    {
        enabled_ = enabled;
        emit enabledChanged(enabled);
    }
}

bool LabelClassFilterModule::isEnabled() const
{
    return enabled_;
}

bool LabelClassFilterModule::isActive() const
{
    return enabled_;
}

std::unordered_set<int64_t> LabelClassFilterModule::getActiveCriteria() const
{
    if (enabled_)
    {
        return selected_label_class_ids_;
    }
    return std::unordered_set<int64_t>();
}

bool LabelClassFilterModule::passes(int64_t label_class_id) const
{
    if (!enabled_)
    {
        return true;
    }

    if (selected_label_class_ids_.empty())
    {
        return false;
    }

    return selected_label_class_ids_.find(label_class_id) != selected_label_class_ids_.end();
}

void LabelClassFilterModule::selectAll()
{
    selected_label_class_ids_.clear();
    emit criteriaChanged();
}

void LabelClassFilterModule::deselectAll()
{
    clear();
}

} // namespace dltool::data
