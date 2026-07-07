#pragma once

#include "dltool/model/Export.h"
#include "model/ModelDatasetSelection.h"
#include "model/ModelTaskTypes.h"

#include <QString>
#include <QVariantMap>
#include <vector>

namespace dltool::model {

class MODEL_API IModelDatasetSource
{
public:
    virtual ~IModelDatasetSource() = default;

    virtual std::vector<int64_t> allImageIds() const = 0;
    virtual qint64               imageDatasetId(qint64 image_id) const = 0;
    virtual QString              imagePath(qint64 image_id) const = 0;
    virtual QVariantMap          imageLevelLabelData(qint64 image_id) const = 0;
    virtual std::vector<int64_t> imageLabelIds(qint64 image_id) const = 0;
    virtual qint64               labelClassId(qint64 label_id) const = 0;
    virtual QVariantMap          labelData(qint64 label_id) const = 0;
    virtual QString              labelClassName(qint64 label_class_id) const = 0;
    virtual QString              labelClassGroup(qint64 label_class_id) const = 0;
    virtual QString              datasetName(qint64 dataset_id) const = 0;
};

struct MODEL_API ModelDatasetExportRequest
{
    int                        method{-1};
    QString                    framework_name;
    QString                    model_architecture;
    QString                    model_uuid;
    ModelTaskType              task_type{ModelTaskType::Unknown};
    QString                    dataset_dir;
    ModelDatasetSelections     selections;
    const IModelDatasetSource *source{nullptr};
};

class MODEL_API ModelDatasetOrganizer
{
public:
    static QVariantMap organize(const ModelDatasetExportRequest &request, QString *err_msg = nullptr);
};

} // namespace dltool::model
