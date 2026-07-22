#pragma once

#include "dltool/feature/Export.h"
#include "feature/FeatureDataProvider.h"

#include <cstdint>
#include <vector>

namespace dltool::feature {

class FEATURE_API ImageClusterDataProvider : public FeatureDataProvider
{
public:
    explicit ImageClusterDataProvider(dltool::data::DataManager *data_manager)
        : FeatureDataProvider(data_manager)
    {
    }

    ~ImageClusterDataProvider() override = default;

};

} // namespace dltool::feature
