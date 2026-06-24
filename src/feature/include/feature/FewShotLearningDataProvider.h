#pragma once

#include "dltool/feature/Export.h"

#include <QMetaObject>
#include <QString>
#include <QVariantMap>

#include <cstdint>
#include <functional>
#include <vector>

class QObject;

namespace dltool::feature {

class FEATURE_API FewShotLearningDataProvider
{
public:
    using ImportFinishedHandler = std::function<void(bool, const QString &)>;

    virtual ~FewShotLearningDataProvider() = default;

    virtual int     method() const       = 0;
    virtual QString databasePath() const = 0;

    virtual std::vector<int64_t> allImageIds() const = 0;
    virtual QString              imagePath(int64_t image_id) const = 0;
    virtual int64_t              imageDatasetId(int64_t image_id) const = 0;

    virtual std::vector<int64_t> allLabelIds() const = 0;
    virtual int64_t              labelImageId(int64_t label_id) const = 0;
    virtual int64_t              labelClassId(int64_t label_id) const = 0;
    virtual QVariantMap          labelData(int64_t label_id) const = 0;

    virtual QString labelClassName(int64_t label_class_id) const = 0;
    virtual QString datasetName(int64_t dataset_id) const = 0;

    virtual void importMaskData(int64_t dataset_id, const QString &image_manifest_path,
                                const QString &prediction_output_dir) = 0;
    virtual QMetaObject::Connection connectImportFinished(QObject *context, ImportFinishedHandler handler) = 0;
    virtual void                    disconnectImportFinished(const QMetaObject::Connection &connection) = 0;
};

} // namespace dltool::feature
