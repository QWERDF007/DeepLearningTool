#pragma once

#include "dltool/model/Export.h"
#include "data/DataSelectionTreeModel.h"
#include "model/ModelTestTaskRepository.h"
#include "model/ModelEvaluationViewModel.h"
#include "model/TaskManager.h"

#include <QAbstractListModel>
#include <QPointer>
#include <QTimer>
#include <QtQml>
#include <memory>

namespace dltool::data {
class DataManager;
class DataSelectionTreeModel;
}

namespace dltool::model {
class ITestParams;
class ModelManager;
class ModelEvaluationViewModel;

/**
 * @brief 当前模型的测试任务列表和编辑上下文。
 *
 * QML 只通过本类取得当前任务的参数、单一测试数据集选择和评估对象；任务定义、
 * 参数和选择的持久化由 ModelTestTaskRepository 完成。
 */
class MODEL_API ModelTestTaskManager final : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ModelTestTaskManager)
    QML_UNCREATABLE("ModelTestTaskManager is owned by Project.")

    Q_PROPERTY(QString modelUuid READ modelUuid WRITE setModelUuid NOTIFY modelUuidChanged FINAL)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged FINAL)
    Q_PROPERTY(QString currentTaskUuid READ currentTaskUuid NOTIFY currentTaskChanged FINAL)
    Q_PROPERTY(QString currentTaskName READ currentTaskName NOTIFY currentTaskChanged FINAL)
    Q_PROPERTY(QString currentTaskDirectory READ currentTaskDirectory NOTIFY currentTaskChanged FINAL)
    Q_PROPERTY(ITestParams *currentTestParams READ currentTestParams NOTIFY currentTaskChanged FINAL)
    Q_PROPERTY(dltool::data::DataSelectionTreeModel *currentDatasetViewModel READ currentDatasetViewModel
               NOTIFY currentTaskChanged FINAL)
    Q_PROPERTY(ModelEvaluationViewModel *currentEvaluation READ currentEvaluation NOTIFY currentTaskChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    Q_PROPERTY(bool currentTaskRunning READ currentTaskRunning NOTIFY taskStateChanged FINAL)
    Q_PROPERTY(int currentTaskProgress READ currentTaskProgress NOTIFY taskStateChanged FINAL)
    Q_PROPERTY(QString currentTaskStatus READ currentTaskStatus NOTIFY taskStateChanged FINAL)

public:
    enum Role
    {
        UuidRole = Qt::UserRole + 1,
        NameRole,
        DirectoryNameRole,
        CreatedAtRole,
        ModifiedAtRole,
        RunningRole,
        ProgressRole,
        StatusRole,
    };
    Q_ENUM(Role)

    explicit ModelTestTaskManager(QString project_dir, ModelManager *model_manager,
                                  dltool::data::DataManager *data_manager, TaskManager *task_manager,
                                  QObject *parent = nullptr);
    ~ModelTestTaskManager() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString modelUuid() const;
    void setModelUuid(const QString &uuid);
    int currentIndex() const;
    int count() const;
    QString currentTaskUuid() const;
    QString currentTaskName() const;
    QString currentTaskDirectory() const;
    ITestParams *currentTestParams() const;
    dltool::data::DataSelectionTreeModel *currentDatasetViewModel() const;
    ModelEvaluationViewModel *currentEvaluation() const;
    bool currentTaskRunning() const;
    int currentTaskProgress() const;
    QString currentTaskStatus() const;

    Q_INVOKABLE QString validateTaskName(const QString &name) const;
    Q_INVOKABLE QString createTask(const QString &name);
    Q_INVOKABLE bool switchTask(const QString &uuid);
    Q_INVOKABLE bool renameTask(const QString &uuid, const QString &name);
    Q_INVOKABLE bool deleteTask(const QString &uuid);
    Q_INVOKABLE bool saveCurrentTask();
    Q_INVOKABLE bool flush();
    Q_INVOKABLE int taskId(const QString &uuid = {}) const;

signals:
    void modelUuidChanged();
    void currentIndexChanged();
    void currentTaskChanged();
    void countChanged();
    void taskStateChanged();
    void errorOccurred(const QString &message);

private slots:
    void scheduleSave();
    void handleTaskRevisionChanged();

private:
    void reload();
    void clearCurrentObjects();
    bool selectIndex(int index, bool save_before);
    bool saveDefinition(ModelTestTaskDefinition &task);
    void bindCurrentObjects();
    void emitTaskRowChanged(int row);
    const TaskManager::Task *currentTaskRecord() const;

    QString project_dir_;
    QPointer<ModelManager> model_manager_;
    QPointer<dltool::data::DataManager> data_manager_;
    QPointer<TaskManager> task_manager_;
    ModelTestTaskRepository repository_;
    QString model_uuid_;
    QList<ModelTestTaskDefinition> tasks_;
    int current_index_{-1};
    std::unique_ptr<ITestParams> current_test_params_;
    QPointer<dltool::data::DataSelectionTreeModel> current_dataset_view_model_;
    QPointer<ModelEvaluationViewModel> current_evaluation_;
    QTimer save_timer_;
};

} // namespace dltool::model
