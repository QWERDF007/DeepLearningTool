#pragma once

#include "dltool/model/Export.h"

#include <QString>
#include <QVariantMap>

namespace dltool::data {
class DataManager;
}

namespace dltool::model {

class IModel;

struct ModelDatasetExportContext
{
    int                       method{-1};
    QString                   framework_name;
    QString                   model_architecture;
    QString                   model_uuid;
    QString                   task_type;
    QString                   dataset_dir;
    IModel                   *model{nullptr};
    dltool::data::DataManager *data_manager{nullptr};
};

class MODEL_API ModelDatasetOrganizer
{
public:
    static QVariantMap organize(const ModelDatasetExportContext &context, QString *err_msg = nullptr);
};

} // namespace dltool::model
