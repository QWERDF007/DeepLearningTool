#pragma once

#include "dltool/feature/Export.h"

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QVariantList>
#include <QtQml>

#include <memory>
#include <vector>

namespace dltool::model {
class TaskManager;
}

namespace dltool::feature {

class FewShotLearningDataProvider;

class FEATURE_API FewShotLearningController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(FewShotLearningController)
    QML_UNCREATABLE("Can not create FewShotLearningController directly!")
    Q_PROPERTY(bool running READ running NOTIFY runningChanged FINAL)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged FINAL)

public:
    explicit FewShotLearningController(FewShotLearningDataProvider *data_provider, QObject *parent = nullptr);
    ~FewShotLearningController() override;

    bool running() const;
    QString lastError() const;

    void setTaskManager(dltool::model::TaskManager *task_manager);

    Q_INVOKABLE bool startFsSam2(const QVariantList &train_dataset_ids, const QVariantList &test_dataset_ids,
                                 const QVariantList &label_class_ids);
    Q_INVOKABLE void cancel();

signals:
    void runningChanged();
    void lastErrorChanged();

private:
    enum class RunStage
    {
        Idle,
        Training,
        Predicting,
    };

    struct RunContext;

    bool prepareRun(const std::vector<int64_t> &train_dataset_ids, const std::vector<int64_t> &test_dataset_ids,
                    const std::vector<int64_t> &label_class_ids, RunContext &context, QString &err_msg) const;
    bool startTraining(const RunContext &context, QString &err_msg);
    bool startPrediction(const RunContext &context, int class_index, QString &err_msg);
    bool startProcess(const RunContext &context, const QStringList &arguments, QString &err_msg);
    void startPredictionImports();
    void startNextPredictionImport();
    void handlePredictionImportFinished(bool success, const QString &message);
    void finishRun();
    void handleProcessFinished(int exit_code, QProcess::ExitStatus exit_status);
    void handleTaskStopRequested(int task_id);

    void setRunning(bool running);
    void setLastError(const QString &last_error);

    FewShotLearningDataProvider *data_provider_{nullptr};
    QPointer<dltool::model::TaskManager> task_manager_;
    QProcess *process_{nullptr};
    QMetaObject::Connection import_finished_connection_;

    int train_task_id_{-1};
    int predict_task_id_{-1};
    QString prediction_output_dir_;
    QString checkpoint_path_;
    std::unique_ptr<RunContext> active_context_;
    RunStage stage_{RunStage::Idle};
    int current_predict_class_index_{0};
    int current_import_index_{0};
    bool importing_predictions_{false};
    bool running_{false};
    bool stop_requested_{false};
    QString last_error_;
};

} // namespace dltool::feature
