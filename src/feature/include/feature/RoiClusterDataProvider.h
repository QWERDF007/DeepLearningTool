#pragma once

#include "dltool/feature/Export.h"
#include "feature/FeatureDataProvider.h"

namespace dltool::feature {

class FEATURE_API RoiClusterDataProvider : public FeatureDataProvider
{
public:
    explicit RoiClusterDataProvider(dltool::data::DataManager *data_manager)
        : FeatureDataProvider(data_manager)
    {
    }

    ~RoiClusterDataProvider() override = default;
};

} // namespace dltool::feature
