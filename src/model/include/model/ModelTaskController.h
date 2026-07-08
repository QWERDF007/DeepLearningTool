#pragma once

#include "dltool/model/Export.h"
#include "model/FewShotLearningTaskService.h"
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

    bool startFewShotLearning(const FewShotLearningRequest &request, QString *err_msg = nullptr);
    bool stopFewShotLearning();
    bool isFewShotLearningRunning() const;
    QString fewShotLearningLastError() const;

signals:
    void fewShotLearningRunningChanged(bool running);
    void fewShotLearningLastErrorChanged(const QString &message);
    void fewShotLearningFinished(bool success, const QString &message);

private:
    enum class FewShotLearningStage
    {
        Idle,
        PreparingMask,
        Training,
        Predicting,
        Importing,
    };

    bool buildContext(const QString &model_uuid, ModelTaskType task_type, int task_id,
                      ModelTaskContext &context, QString *err_msg = nullptr) const;
    int  ensureTaskRecord(const ModelTaskContext &context);
    bool startTask(int task_id);
    bool stopTask(int task_id);
    bool deleteTask(int task_id);
    bool startExternalTask(const ModelTaskContext &context);
    bool taskBelongsToCurrentModelManager(int task_id) const;
    void failTask(int task_id, const QString &message) const;
    int  addOwnedTask(const QString &task_uuid, const QString &task_name, ModelTaskType task_type,
                      bool supports_pause);
    bool startPreparedExternalTask(int task_id, const ExternalProcessSpec &process_spec, QString *err_msg = nullptr);
    bool startFewShotBoxToMask(int split_index, QString *err_msg = nullptr);
    bool startFewShotTraining(QString *err_msg = nullptr);
    bool startFewShotPrediction(QString *err_msg = nullptr);
    bool taskBelongsToFewShotLearning(int task_id) const;
    void handleFewShotExternalTaskFinished(int task_id, int exit_code, bool normal_exit, bool stop_requested);
    void startFewShotPredictionImports();
    void startNextFewShotPredictionImport();
    void handleFewShotPredictionImportFinished(bool success, const QString &message);
    void markFewShotStoppedTasksForCurrentStage();
    bool storeFewShotCheckpoint(QString *err_msg = nullptr) const;
    void failFewShotLearning(const QString &message);
    void finishFewShotLearning(bool success, const QString &message = {});
    void setFewShotLearningRunning(bool running);
    void setFewShotLearningLastError(const QString &message);

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

    std::unique_ptr<FewShotLearningRunContext> few_shot_context_;
    FewShotLearningStage                       few_shot_stage_{FewShotLearningStage::Idle};
    QMetaObject::Connection                    few_shot_import_finished_connection_;
    int                                        current_few_shot_prepare_split_index_{0};
    int                                        current_few_shot_import_index_{0};
    bool                                       few_shot_learning_running_{false};
    bool                                       few_shot_stop_requested_{false};
    QString                                    few_shot_last_error_;
};

} // namespace dltool::model
