#pragma once

#include "dltool/feature/Export.h"

#include <QPointer>
#include <QString>
#include <QVariantMap>
#include <cstdint>
#include <vector>

namespace dltool::data {
class DataManager;
} // namespace dltool::data

namespace dltool::feature {

class FEATURE_API FeatureDataProvider
{
public:
    explicit FeatureDataProvider(dltool::data::DataManager *data_manager);
    virtual ~FeatureDataProvider();

    virtual std::vector<int64_t> allImageIds() const;

    virtual QString imagePath(int64_t image_id) const;

    virtual int64_t imageDatasetId(int64_t image_id) const;

    virtual QString databasePath() const;

    virtual std::vector<int64_t> allLabelIds() const;

    virtual int64_t labelImageId(int64_t label_id) const;

    virtual int64_t labelClassId(int64_t label_id) const;

    virtual QVariantMap labelData(int64_t label_id) const;

    virtual QString labelClassName(int64_t label_class_id) const;

    virtual QString datasetName(int64_t dataset_id) const;

protected:
    dltool::data::DataManager *dataManager() const;

private:
    QPointer<dltool::data::DataManager> data_manager_;
};

} // namespace dltool::feature
