#pragma once

#include "dltool/model/Export.h"
#include "model/ModelManager.h"
#include "model/ModelTaskTypes.h"
#include "model/PreparedExternalModelTask.h"

#include <QString>

namespace dltool::data {
class DataManager;
} // namespace dltool::data

namespace dltool::model {

class IModel;

class MODEL_API ModelTaskPreparationService
{
public:
    struct Request
    {
        int                               task_id{-1};
        QString                           model_uuid;
        QString                           model_name;
        ModelTaskType                     task_type{ModelTaskType::Unknown};
        IModel                           *model{nullptr};
        ModelManager::FrameworkDefinition framework;
    };

    ModelTaskPreparationService(int method, QString project_dir, dltool::data::DataManager *data_manager);

    bool prepare(const Request &request, PreparedExternalModelTask &prepared, QString *err_msg = nullptr) const;

    static bool    frameworkHasScript(const ModelManager::FrameworkDefinition &framework, ModelTaskType task_type);
    static QString scriptForTask(const ModelManager::FrameworkDefinition &framework, ModelTaskType task_type);

private:
    int                       method_{-1};
    QString                   project_dir_;
    dltool::data::DataManager *data_manager_{nullptr};
};

} // namespace dltool::model
