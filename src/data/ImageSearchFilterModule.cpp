#include "data/ImageSearchFilterModule.h"

namespace dltool::data {

ImageSearchFilterModule::ImageSearchFilterModule(DataManager *data_manager, QObject *parent)
    : FilterModule(data_manager, parent)
{
}

void ImageSearchFilterModule::setCriteria(const std::vector<int64_t> &image_ids)
{
    result_image_ids_.clear();
    result_image_ids_.insert(image_ids.begin(), image_ids.end());
    inverted_ = false;
    emit criteriaChanged();
}

void ImageSearchFilterModule::clear()
{
    if (!result_image_ids_.empty() || inverted_)
    {
        result_image_ids_.clear();
        inverted_ = false;
        emit criteriaChanged();
    }
}

void ImageSearchFilterModule::setEnabled(bool enabled)
{
    if (enabled_ != enabled)
    {
        enabled_ = enabled;
        emit enabledChanged(enabled);
    }
}

bool ImageSearchFilterModule::isEnabled() const
{
    return enabled_;
}

bool ImageSearchFilterModule::isActive() const
{
    return enabled_ && !result_image_ids_.empty();
}

std::unordered_set<int64_t> ImageSearchFilterModule::getActiveCriteria() const
{
    if (enabled_)
    {
        return result_image_ids_;
    }
    return {};
}

bool ImageSearchFilterModule::isInverted() const
{
    return inverted_;
}

bool ImageSearchFilterModule::passes(int64_t image_id) const
{
    if (!enabled_)
    {
        return true;
    }

    const bool matches = result_image_ids_.find(image_id) != result_image_ids_.end();
    return inverted_ ? !matches : matches;
}

void ImageSearchFilterModule::selectAll()
{
    inverted_ = false;
    emit criteriaChanged();
}

void ImageSearchFilterModule::deselectAll()
{
    inverted_ = true;
    emit criteriaChanged();
}

int ImageSearchFilterModule::resultCount() const
{
    return static_cast<int>(result_image_ids_.size());
}

bool ImageSearchFilterModule::hasResults() const
{
    return !result_image_ids_.empty();
}

} // namespace dltool::data
