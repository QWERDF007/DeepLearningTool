#pragma once

#include "dltool/feature/Export.h"
#include "model/ModelTaskTypes.h"

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QtQml>
#include <vector>

namespace dltool::model {
class ModelManager;
class ModelTaskController;
class TaskManager;
}

namespace dltool::data {
class DataManager;
}

namespace dltool::feature {

class FEATURE_API FewShotLearningController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(FewShotLearningController)
    QML_UNCREATABLE("Can not create FewShotLearningController directly!")
    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged FINAL)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged FINAL)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged FINAL)
    Q_PROPERTY(QObject *trainDatasetViewModel READ trainDatasetViewModel WRITE setTrainDatasetViewModel NOTIFY
                   trainDatasetViewModelChanged FINAL)
    Q_PROPERTY(QObject *validationDatasetViewModel READ validationDatasetViewModel WRITE setValidationDatasetViewModel
                   NOTIFY validationDatasetViewModelChanged FINAL)
    Q_PROPERTY(QObject *testDatasetViewModel READ testDatasetViewModel WRITE setTestDatasetViewModel NOTIFY
                   testDatasetViewModelChanged FINAL)
    Q_PROPERTY(QObject *labelClassViewModel READ labelClassViewModel WRITE setLabelClassViewModel NOTIFY
                   labelClassViewModelChanged FINAL)

public:
    /**
     * @brief 构造函数
     * @param parent 父对象
     */
    explicit FewShotLearningController(dltool::data::DataManager          *data_manager,
                                       dltool::model::ModelManager        *model_manager,
                                       dltool::model::ModelTaskController *model_task_controller,
                                       dltool::model::TaskManager         *task_manager,
                                       QObject                            *parent = nullptr);
    ~FewShotLearningController() override;

    /**
     * @brief 功能是否启用
     * @return 启用返回 true
     */
    bool enabled() const;

    /**
     * @brief 是否正在运行
     * @return 运行中返回 true
     */
    bool running() const;

    /**
     * @brief 获取最后一次错误信息
     * @return 错误信息文本
     */
    QString lastError() const;

    QObject *trainDatasetViewModel() const;
    void     setTrainDatasetViewModel(QObject *view_model);

    QObject *validationDatasetViewModel() const;
    void     setValidationDatasetViewModel(QObject *view_model);

    QObject *testDatasetViewModel() const;
    void     setTestDatasetViewModel(QObject *view_model);

    QObject *labelClassViewModel() const;
    void     setLabelClassViewModel(QObject *view_model);

    /**
     * @brief 启动小样本学习流程（FS-SAM2），数据集和类别从控制器持有的 ViewModel 读取
     * @return 启动成功返回 true
     */
    Q_INVOKABLE bool startFsSam2();

    /// 清除最后一次错误信息
    Q_INVOKABLE void clearLastError();

    /// 取消当前运行的小样本学习任务
    Q_INVOKABLE void cancel();

signals:
    void enabledChanged();
    void runningChanged();
    void lastErrorChanged();
    void trainDatasetViewModelChanged();
    void validationDatasetViewModelChanged();
    void testDatasetViewModelChanged();
    void labelClassViewModelChanged();

private:
    bool startFsSam2WithIds(const QVariantList &train_dataset_ids, const QVariantList &validation_dataset_ids,
                            const QVariantList &test_dataset_ids, const QVariantList &label_class_ids);

    struct PredictionImportTarget
    {
        qint64  dataset_id{-1};
        QString manifest_path;
    };

    enum class RunStage
    {
        Idle,
        PreparingMask,
        Training,
        Predicting,
    };

    struct RunState
    {
        QString                             model_uuid;
        QString                             output_dir;
        int                                 box_to_mask_task_id{-1};
        int                                 train_task_id{-1};
        int                                 predict_task_id{-1};
        RunStage                            stage{RunStage::Idle};
        bool                                stop_requested{false};
        std::vector<PredictionImportTarget> import_targets;
    };

    void setRunning(bool running);
    void setLastError(const QString &last_error);
    bool startRun(const std::vector<int64_t> &train_dataset_ids,
                  const std::vector<int64_t> &validation_dataset_ids,
                  const std::vector<int64_t> &test_dataset_ids,
                  const std::vector<int64_t> &label_class_ids, QString *err_msg);
    bool configureFsSam2Model(const QString &model_uuid, const std::vector<int64_t> &train_dataset_ids,
                              const std::vector<int64_t> &validation_dataset_ids,
                              const std::vector<int64_t> &test_dataset_ids,
                              const std::vector<int64_t> &label_class_ids, RunState &run, QString *err_msg);
    bool writePredictionImportTargets(const QString &model_uuid, const std::vector<int64_t> &test_dataset_ids,
                                      RunState &run, QString *err_msg) const;
    int  addOrdinaryTask(const QString &model_uuid, dltool::model::ModelTaskType task_type, QString *err_msg) const;
    bool startOrdinaryTask(dltool::model::ModelTaskType task_type, int expected_task_id, QString *err_msg);
    void handleTaskTableRevision();
    void advanceFinishedTask(dltool::model::ModelTaskType next_task_type, int next_task_id, RunStage next_stage);
    void finishRun(bool success, const QString &message = {});
    void stopRunTasks();
    void startPredictionImports(std::vector<PredictionImportTarget> targets, const QString &output_dir);
    void startNextPredictionImport();
    void handlePredictionImportFinished(bool success, const QString &message);
    void disconnectPredictionImport();

    QPointer<dltool::data::DataManager>          data_manager_;
    QPointer<dltool::model::ModelManager>        model_manager_;
    QPointer<dltool::model::ModelTaskController> model_task_controller_;
    QPointer<dltool::model::TaskManager>         task_manager_;

    bool enabled_{true};  ///< 功能是否启用
    bool running_{false}; ///< 是否正在运行

    QString last_error_; ///< 最后一次错误信息

    QPointer<QObject> train_dataset_view_model_;
    QPointer<QObject> validation_dataset_view_model_;
    QPointer<QObject> test_dataset_view_model_;
    QPointer<QObject> label_class_view_model_;

    RunState current_run_;
    std::vector<PredictionImportTarget> pending_import_targets_;
    QString pending_import_output_dir_;
    QMetaObject::Connection prediction_import_connection_;
    int current_import_index_{0};
};

} // namespace dltool::feature
