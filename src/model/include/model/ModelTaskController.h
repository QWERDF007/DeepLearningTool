#pragma once

#include "dltool/model/Export.h"
#include "model/ModelTaskPreparationService.h"
#include "model/ModelTaskTypes.h"

#include <QMetaObject>
#include <QObject>
#include <QString>
#include <QtQml>
#include <memory>
#include <unordered_set>

namespace dltool::data {
class DataManager;
} // namespace dltool::data

namespace dltool::model {

class ExternalModelTaskRunner;
class IModel;
class ModelManager;
class TaskManager;

class MODEL_API ModelTaskController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ModelTaskController)
    QML_UNCREATABLE("ModelTaskController is owned by Project.")

public:
    ModelTaskController(int method, QString project_dir, ModelManager *model_manager,
                        dltool::data::DataManager *data_manager, TaskManager *task_manager,
                        QObject *parent = nullptr);
    ~ModelTaskController() override;

    Q_INVOKABLE int  addModelTask(const QString &model_uuid, ModelTaskTypes::Type task_type);
    Q_INVOKABLE int  startModelTask(const QString &model_uuid, ModelTaskTypes::Type task_type);
    Q_INVOKABLE bool stopModelTask(const QString &model_uuid, ModelTaskTypes::Type task_type);
    Q_INVOKABLE bool deleteModelTask(const QString &model_uuid, ModelTaskTypes::Type task_type);

private:
    bool buildContext(const QString &model_uuid, ModelTaskType task_type, int task_id,
                      ModelTaskContext &context, QString *err_msg = nullptr) const;
    int  ensureTaskRecord(const ModelTaskContext &context);
    bool startTask(int task_id);
    bool stopTask(int task_id);
    bool deleteTask(int task_id);
    bool startExternalTask(const ModelTaskContext &context);
    bool taskBelongsToCurrentModelManager(int task_id) const;
    void failTask(int task_id, const QString &message) const;

private slots:
    void handleTaskStopRequested(int task_id);
    void handleExternalTaskFinished(int task_id, int exit_code, bool normal_exit, bool stop_requested);

private:
    int                       method_{-1};
    QString                   project_dir_;
    ModelManager             *model_manager_{nullptr};
    dltool::data::DataManager *data_manager_{nullptr};
    TaskManager              *task_manager_{nullptr};
    std::unique_ptr<ExternalModelTaskRunner> external_task_runner_;
    std::unordered_set<int> owned_task_ids_;
};

} // namespace dltool::model
