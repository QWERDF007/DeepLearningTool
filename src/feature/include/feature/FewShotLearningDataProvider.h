#pragma once

#include "dltool/feature/Export.h"
#include "feature/FeatureDataProvider.h"

#include <QMetaObject>
#include <QString>
#include <cstdint>
#include <functional>

class QObject;

namespace dltool::feature {

class FEATURE_API FewShotLearningDataProvider : public FeatureDataProvider
{
public:
    using ImportFinishedHandler = std::function<void(bool, const QString &)>;

    explicit FewShotLearningDataProvider(dltool::data::DataManager *data_manager)
        : FeatureDataProvider(data_manager)
    {
    }

    ~FewShotLearningDataProvider() override = default;

    virtual int method() const = 0;

    virtual void importMaskData(int64_t dataset_id, const QString &image_manifest_path,
                                const QString &prediction_output_dir) = 0;

    virtual QMetaObject::Connection connectImportFinished(QObject *context, ImportFinishedHandler handler) = 0;

    virtual void disconnectImportFinished(const QMetaObject::Connection &connection) = 0;
};

} // namespace dltool::feature
