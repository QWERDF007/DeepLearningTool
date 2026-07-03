#pragma once

#include "dltool/feature/Export.h"
#include "feature/FeatureDataProvider.h"

#include <cstdint>
#include <vector>

namespace dltool::feature {

class FEATURE_API ImageSearchDataProvider : public FeatureDataProvider
{
public:
    explicit ImageSearchDataProvider(dltool::data::DataManager *data_manager)
        : FeatureDataProvider(data_manager)
    {
    }

    ~ImageSearchDataProvider() override = default;

    virtual std::vector<int64_t> selectedImageIds() const = 0;

    virtual void clearImageSearchResults() = 0;

    virtual void setImageSearchResults(const std::vector<int64_t> &image_ids, bool enable_filter) = 0;
};

} // namespace dltool::feature
