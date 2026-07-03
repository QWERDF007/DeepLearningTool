#pragma once

#include "dltool/feature/Export.h"
#include "feature/FeatureDataProvider.h"

#include <cstdint>
#include <vector>

namespace dltool::feature {

class FEATURE_API RoiSearchDataProvider : public FeatureDataProvider
{
public:
    explicit RoiSearchDataProvider(dltool::data::DataManager *data_manager)
        : FeatureDataProvider(data_manager)
    {
    }

    ~RoiSearchDataProvider() override = default;

    virtual void clearLabelSearchResults() = 0;

    virtual void setLabelSearchResults(const std::vector<int64_t> &label_ids, bool enable_filter) = 0;
};

} // namespace dltool::feature
