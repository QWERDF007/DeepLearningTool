#pragma once

#include "data/DataSelectionTreeModel.h"
#include "dltool/model/Export.h"
#include "model/ModelEvaluationOptions.h"
#include "model/ModelEvaluationViewModel.h"
#include "model/ModelTestTaskRepository.h"
#include "model/TaskManager.h"

#include <QAbstractListModel>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QtQml>
#include <memory>

namespace dltool::data {
class DataManager;
class DataSelectionTreeModel;
} // namespace dltool::data

namespace dltool::model {
class ITestParams;
class ModelManager;
class ModelEvaluationViewModel;

/**
 * @brief 当前模型测试任务列表管理器与编辑上下文。
 *
 * 管理属于指定模型的所有测试任务生命周期，提供列表展示、任务增删重命名、
 * 当前选中任务的参数与数据集绑定、异步测试运行控制及评估结果缓存。
 * 任务定义、参数和数据集选择的持久化委托给 ModelTestTaskRepository。
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
    Q_PROPERTY(dltool::data::DataSelectionTreeModel *currentDatasetViewModel READ currentDatasetViewModel NOTIFY
                   currentTaskChanged FINAL)
    Q_PROPERTY(ModelEvaluationViewModel *currentEvaluation READ currentEvaluation NOTIFY currentTaskChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    Q_PROPERTY(bool currentTaskRunning READ currentTaskRunning NOTIFY taskStateChanged FINAL)
    Q_PROPERTY(bool currentModelBusy READ currentModelBusy NOTIFY taskStateChanged FINAL)
    Q_PROPERTY(int currentTaskProgress READ currentTaskProgress NOTIFY taskStateChanged FINAL)
    Q_PROPERTY(QString currentTaskStatus READ currentTaskStatus NOTIFY taskStateChanged FINAL)

public:
    /**
     * @brief 列表模型数据角色枚举。
     */
    enum Role
    {
        UuidRole = Qt::UserRole + 1, ///< 测试任务 UUID。
        NameRole,                    ///< 测试任务名称。
        DirectoryNameRole,           ///< 任务工作目录名。
        CreatedAtRole,               ///< 创建时间。
        ModifiedAtRole,              ///< 修改时间。
        RunningRole,                 ///< 是否正在运行。
        ProgressRole,                ///< 运行进度（0 ~ 100）。
        StatusRole,                  ///< 运行状态描述文本。
    };
    Q_ENUM(Role)

    explicit ModelTestTaskManager(QString project_dir, ModelManager *model_manager,
                                  dltool::data::DataManager *data_manager, TaskManager *task_manager,
                                  QObject *parent = nullptr);
    ~ModelTestTaskManager() override;

    int                    rowCount(const QModelIndex &parent = {}) const override;
    QVariant               data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    /** @brief 获取当前关联的模型 UUID。 */
    QString                               modelUuid() const;
    /** @brief 切换关联的模型 UUID 并重载其下所有测试任务。 */
    void                                  setModelUuid(const QString &uuid);
    /** @brief 获取当前选中的测试任务索引。 */
    int                                   currentIndex() const;
    /** @brief 获取测试任务总数。 */
    int                                   count() const;
    /** @brief 获取当前任务 UUID。 */
    QString                               currentTaskUuid() const;
    /** @brief 获取当前任务名称。 */
    QString                               currentTaskName() const;
    /** @brief 获取当前任务的工作目录路径。 */
    QString                               currentTaskDirectory() const;
    /** @brief 获取当前任务的测试参数对象。 */
    ITestParams                          *currentTestParams() const;
    /** @brief 获取当前任务对应的数据集树形勾选视图模型。 */
    dltool::data::DataSelectionTreeModel *currentDatasetViewModel() const;
    /** @brief 获取当前任务对应的评估视图模型实例。 */
    ModelEvaluationViewModel             *currentEvaluation() const;
    /** @brief 当前任务是否处于运行中状态。 */
    bool                                  currentTaskRunning() const;
    /** @brief 当前模型是否有训练、推理或评估任务正在运行。 */
    bool                                  currentModelBusy() const;
    /** @brief 当前任务运行进度（0 ~ 100）。 */
    int                                   currentTaskProgress() const;
    /** @brief 当前任务状态文本。 */
    QString                               currentTaskStatus() const;

    /**
     * @brief 校验测试任务名称合法性（检查重名及非法字符）。
     * @param name 待校验名称。
     * @return 错误信息，合法返回空字符串。
     */
    Q_INVOKABLE QString validateTaskName(const QString &name) const;

    /**
     * @brief 为当前模型新建一个测试任务。
     * @param name 任务名称。
     * @return 新任务的 UUID，失败返回空。
     */
    Q_INVOKABLE QString createTask(const QString &name);

    /**
     * @brief 切换当前激活的测试任务。
     * @param uuid 目标任务 UUID。
     * @return 切换成功返回 true。
     */
    Q_INVOKABLE bool switchTask(const QString &uuid);

    /**
     * @brief 重命名指定测试任务。
     * @param uuid 目标任务 UUID。
     * @param name 新名称。
     * @return 成功返回 true。
     */
    Q_INVOKABLE bool renameTask(const QString &uuid, const QString &name);

    /**
     * @brief 删除指定测试任务（清理数据库与磁盘目录）。
     * @param uuid 目标任务 UUID。
     * @return 成功返回 true。
     */
    Q_INVOKABLE bool deleteTask(const QString &uuid);

    /**
     * @brief 将当前任务的参数变更立即落库保存。
     * @return 成功返回 true。
     */
    Q_INVOKABLE bool saveCurrentTask();

    /**
     * @brief 强制立即保存所有挂起的延迟写入操作。
     * @return 成功返回 true。
     */
    Q_INVOKABLE bool flush();

    /**
     * @brief 获取指定任务在全局 TaskManager 中的整数任务 ID。
     * @param uuid 任务 UUID（为空时取当前任务）。
     * @return 整数任务 ID，未找到返回 -1。
     */
    Q_INVOKABLE int taskId(const QString &uuid = {}) const;

    /**
     * @brief 将当前数据集选择连同参数提交落库。
     *
     * 数据集选择在编辑期间只保存在内存（更新界面，不写库），仅当用户手动
     * 运行测试任务时调用本函数把当前选择与参数一起持久化，供本次运行使用。
     * @return 提交成功返回 true。
     */
    Q_INVOKABLE bool commitCurrentDatasetSelection();

signals:
    /** @brief 关联的模型 UUID 发生改变。 */
    void modelUuidChanged();
    /** @brief 当前选中的任务索引改变。 */
    void currentIndexChanged();
    /** @brief 当前选中的任务对象（参数/数据集/评估）改变。 */
    void currentTaskChanged();
    /** @brief 任务列表项数量改变。 */
    void countChanged();
    /** @brief 任务运行状态或进度发生改变。 */
    void taskStateChanged();
    /** @brief 发生业务错误信号。 */
    void errorOccurred(const QString &message);

private slots:
    void scheduleSave();
    void handleTaskStartRequested(int task_id);
    void handleTaskRevisionChanged();

private:
    void    reload();
    void    clearCurrentObjects();
    bool    selectIndex(int index, bool save_before);
    bool    saveDefinition(ModelTestTaskDefinition &task, bool persist_selection);
    /** 将当前数据集选择视图快照到内存任务记录（不落库）。 */
    void    snapshotCurrentDatasetSelection();
    void    bindCurrentObjects();
    bool    buildEvaluationOptions(const ModelTestTaskDefinition &task, ModelEvaluationOptions &options,
                                   QString *err_msg = nullptr) const;
    void    handleParameterChanged(const QString &group_name, const QString &parameter_name);
    QString evaluationCacheKey(const QString &task_uuid) const;
    void    emitTaskRowChanged(int row);
    const TaskManager::Task *currentTaskRecord() const;

    QString                                        project_dir_;
    QPointer<ModelManager>                         model_manager_;
    QPointer<dltool::data::DataManager>            data_manager_;
    QPointer<TaskManager>                          task_manager_;
    ModelTestTaskRepository                        repository_;
    QString                                        model_uuid_;
    QList<ModelTestTaskDefinition>                 tasks_;
    int                                            current_index_{-1};
    std::unique_ptr<ITestParams>                   current_test_params_;
    QPointer<dltool::data::DataSelectionTreeModel> current_dataset_view_model_;
    QPointer<ModelEvaluationViewModel>             current_evaluation_;
    QHash<QString, ModelEvaluationViewModel *>     evaluation_cache_;
    QSet<QString>                                  pending_evaluation_notifications_;
    QTimer                                         save_timer_;
};

} // namespace dltool::model
