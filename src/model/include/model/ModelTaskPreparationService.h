#pragma once

#include "dltool/model/Export.h"
#include "model/ExternalProcessSpec.h"
#include "model/ModelRegistry.h"
#include "model/ModelTaskTypes.h"

#include <QString>
#include <QtGlobal>

namespace dltool::data {
class DataManager;
} // namespace dltool::data

namespace dltool::model {

class IModel;

struct MODEL_API ExternalModelTaskRequest
{
    int                 task_id{-1};
    QString             model_name;
    ModelTaskType       task_type{ModelTaskType::Unknown};
    IModel             *model{nullptr};
    FrameworkDefinition framework;
    QString             task_server_host;
    quint16             task_server_port{0};
};

class MODEL_API ModelTaskPreparationService
{
public:
    ModelTaskPreparationService(int method, QString project_dir, dltool::data::DataManager *data_manager);

    bool prepare(const ExternalModelTaskRequest &request, ExternalProcessSpec &process_spec,
                 QString *err_msg = nullptr) const;

private:
    int                       method_{-1};
    QString                   project_dir_;
    dltool::data::DataManager *data_manager_{nullptr};
};

} // namespace dltool::model
