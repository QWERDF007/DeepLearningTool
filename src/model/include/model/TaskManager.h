#pragma once

#include "common/Singleton.h"
#include "dltool/model/Export.h"
#include "model/ModelTaskTypes.h"

#include <QAbstractTableModel>
#include <QTimer>
#include <QtQml>
#include <vector>

namespace dltool::model {

class TaskCommunicationServer;
class TaskEventRouter;
struct TaskMessage;

/**
 * @brief 任务表格模型，以表格形式管理所有任务记录，提供 QAbstractTableModel 接口
 */
class MODEL_API TaskTableModel : public QAbstractTableModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(TaskTableModel)
    QML_UNCREATABLE("Can not create TaskTableModel directly!")
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged FINAL)
public:
    /**
     * @brief 构造任务表格模型
     * @param parent 父对象
     */
    explicit TaskTableModel(QObject *parent = nullptr);
    ~TaskTableModel() override = default;

    enum Column
    {
        TaskIdColumn = 0,
        ModelNameColumn,
        TaskTypeColumn,
        StatusColumn,
        CreatedAtColumn,
        RunningTimeColumn,
        EtaColumn,
        ProgressColumn,
        ActionsColumn,
        ColumnCount,
    };
    Q_ENUM(Column)

    enum TaskStatus
    {
        Pending = 0, ///< 等待中
        Running,     ///< 运行中
        Paused,      ///< 已暂停
        Stopping,    ///< 停止中
        Stopped,     ///< 已停止
        Finished,    ///< 已结束
        Failed,      ///< 失败
    };
    Q_ENUM(TaskStatus)

    enum Role
    {
        TaskIdRole = Qt::UserRole + 1,
        ModelUuidRole,
        ModelNameRole,
        TaskTypeRole,
        TaskTypeTextRole,
        StatusRole,
        StatusValueRole,
        CreatedAtRole,
        RunningTimeRole,
        EtaRole,
        ProgressRole,
        CanStartRole,
        CanPauseRole,
        CanStopRole,
        CanFinishRole,
    };
    Q_ENUM(Role)

    /**
     * @brief 任务快照，包含任务在某一时刻的完整状态
     */
    struct TaskSnapshot
    {
        int           task_id{-1};                       ///< 任务 ID
        QString       model_uuid;                        ///< 模型 UUID
        QString       model_name;                        ///< 模型名称
        ModelTaskType task_type{ModelTaskType::Unknown}; ///< 任务类型
        TaskStatus    status{Pending};                   ///< 任务状态
        qint64        created_at{0};                     ///< 创建时间戳
        qint64        started_at{0};                     ///< 启动时间戳
        qint64        accumulated_seconds{0};            ///< 累计运行秒数
        qint64        eta_seconds{-1};                   ///< 预计剩余秒数
        int           progress{0};                       ///< 进度（0-100）
        bool          supports_pause{true};              ///< 是否支持暂停
        bool          can_start{false};                  ///< 是否可启动
        bool          can_pause{false};                  ///< 是否可暂停
        bool          can_stop{false};                   ///< 是否可停止
        bool          can_finish{false};                 ///< 是否可结束

        /**
         * @brief 检查快照是否有效
         * @return 有效返回 true
         */
        bool isValid() const
        {
            return task_id >= 0;
        }
    };

    /**
     * @brief 获取行数
     * @param parent 父索引
     * @return 行数
     */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    /**
     * @brief 获取列数
     * @param parent 父索引
     * @return 列数
     */
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

    /**
     * @brief 获取任务数量
     * @return 任务个数
     */
    int count() const;

    /**
     * @brief 获取数据版本号
     * @return 版本号
     */
    int revision() const;

    /**
     * @brief 获取指定单元格数据
     * @param index 模型索引
     * @param role 数据角色
     * @return 数据值
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    /**
     * @brief 获取表头数据
     * @param section 行/列号
     * @param orientation 方向
     * @param role 数据角色
     * @return 表头文本
     */
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    /**
     * @brief 获取角色名称映射
     * @return 角色名称哈希表
     */
    QHash<int, QByteArray> roleNames() const override;

    /**
     * @brief 添加任务
     * @param model_uuid 模型 UUID
     * @param model_name 模型名称
     * @param task_type 任务类型
     * @param supports_pause 是否支持暂停
     * @return 任务 ID，失败返回 -1
     */
    int addTask(const QString &model_uuid, const QString &model_name, ModelTaskTypes::Type task_type,
                bool supports_pause = true);

    /**
     * @brief 启动任务
     * @param task_id 任务 ID
     * @return 操作成功返回 true
     */
    bool startTask(int task_id);

    /**
     * @brief 暂停任务
     * @param task_id 任务 ID
     * @return 操作成功返回 true
     */
    bool pauseTask(int task_id);

    /**
     * @brief 停止任务
     * @param task_id 任务 ID
     * @return 操作成功返回 true
     */
    bool stopTask(int task_id);

    /**
     * @brief 完成任务
     * @param task_id 任务 ID
     * @return 操作成功返回 true
     */
    bool finishTask(int task_id);

    /**
     * @brief 标记任务失败
     * @param task_id 任务 ID
     * @return 操作成功返回 true
     */
    bool failTask(int task_id);

    /**
     * @brief 设置任务状态
     * @param task_id 任务 ID
     * @param status 新状态
     * @return 操作成功返回 true
     */
    bool setTaskStatus(int task_id, TaskStatus status);

    /**
     * @brief 删除任务
     * @param task_id 任务 ID
     * @return 操作成功返回 true
     */
    bool deleteTask(int task_id);

    /**
     * @brief 更新任务进度
     * @param task_id 任务 ID
     * @param progress 进度值（0-100）
     * @return 操作成功返回 true
     */
    bool updateTaskProgress(int task_id, int progress);

    /**
     * @brief 更新任务预计剩余时间
     * @param task_id 任务 ID
     * @param eta_seconds 预计剩余秒数
     * @return 操作成功返回 true
     */
    bool updateTaskEta(int task_id, qint64 eta_seconds);

    /**
     * @brief 清除所有任务
     */
    void clearTasks();

    /**
     * @brief 查找指定模型的任务 ID
     * @param model_uuid 模型 UUID
     * @param task_type 任务类型
     * @param include_finished 是否包含已完成任务
     * @return 任务 ID，未找到返回 -1
     */
    int findModelTask(const QString &model_uuid, ModelTaskTypes::Type task_type, bool include_finished = false) const;

    /**
     * @brief 获取指定任务 ID 的快照
     * @param task_id 任务 ID
     * @return 任务快照
     */
    TaskSnapshot taskSnapshotForId(int task_id) const;

    /**
     * @brief 获取指定模型任务的快照
     * @param model_uuid 模型 UUID
     * @param task_type 任务类型
     * @param include_finished 是否包含已完成任务
     * @return 任务快照
     */
    TaskSnapshot taskSnapshotForModel(const QString &model_uuid, ModelTaskTypes::Type task_type,
                                      bool include_finished = false) const;

    /**
     * @brief 获取指定行的任务数据
     * @param row 行号
     * @return 任务数据键值对
     */
    Q_INVOKABLE QVariantMap taskAt(int row) const;

    /**
     * @brief 获取指定任务 ID 的数据
     * @param task_id 任务 ID
     * @return 任务数据键值对
     */
    Q_INVOKABLE QVariantMap taskForId(int task_id) const;

    /**
     * @brief 获取指定模型任务的数据
     * @param model_uuid 模型 UUID
     * @param task_type 任务类型
     * @param include_finished 是否包含已完成任务
     * @return 任务数据键值对
     */
    Q_INVOKABLE QVariantMap taskForModel(const QString &model_uuid, ModelTaskTypes::Type task_type,
                                         bool include_finished = false) const;

signals:
    void countChanged();
    void revisionChanged();

private:
    struct TaskRecord
    {
        int           task_id{-1};
        QString       model_uuid;
        QString       model_name;
        ModelTaskType task_type{ModelTaskType::Unknown};
        TaskStatus    status{Pending};
        qint64        created_at{0};
        qint64        started_at{0};
        qint64        accumulated_seconds{0};
        qint64        eta_seconds{-1};
        int           progress{0};
        bool          supports_pause{true};
    };

    /**
     * @brief 根据任务 ID 查找行索引
     * @param task_id 任务 ID
     * @return 行索引，未找到返回 -1
     */
    int indexOfTask(int task_id) const;

    /**
     * @brief 根据模型 UUID 和任务类型查找行索引
     * @param model_uuid 模型 UUID
     * @param task_type 任务类型
     * @param include_finished 是否包含已完成任务
     * @return 行索引
     */
    int indexOfModelTask(const QString &model_uuid, ModelTaskTypes::Type task_type, bool include_finished) const;

    /**
     * @brief 生成指定行的任务快照
     * @param row 行号
     * @return 任务快照
     */
    TaskSnapshot snapshotForRow(int row) const;

    /**
     * @brief 发送任务变更信号
     * @param row 行号
     * @param roles 变更的角色列表
     */
    void emitTaskChanged(int row, const QList<int> &roles = {});

    /**
     * @brief 递增数据版本号
     */
    void bumpRevision();

    /**
     * @brief 刷新所有运行中任务的显示时间
     */
    void refreshRunningTasks();

    /**
     * @brief 获取指定列的数据
     * @param task 任务记录
     * @param column 列号
     * @return 显示数据
     */
    QVariant dataForColumn(const TaskRecord &task, int column) const;

    /**
     * @brief 获取状态文本
     * @param task 任务记录
     * @return 状态文本
     */
    QString statusText(const TaskRecord &task) const;

    /**
     * @brief 获取创建时间文本
     * @param task 任务记录
     * @return 时间文本
     */
    QString createdAtText(const TaskRecord &task) const;

    /**
     * @brief 获取运行时间文本
     * @param task 任务记录
     * @return 时间文本
     */
    QString runningTimeText(const TaskRecord &task) const;

    /**
     * @brief 获取 ETA 文本
     * @param task 任务记录
     * @return ETA 文本
     */
    QString etaText(const TaskRecord &task) const;

    /**
     * @brief 计算运行时间秒数
     * @param task 任务记录
     * @return 运行秒数
     */
    qint64 runningTimeSeconds(const TaskRecord &task) const;

    /**
     * @brief 计算预计剩余秒数
     * @param task 任务记录
     * @return 预计秒数
     */
    qint64 etaSeconds(const TaskRecord &task) const;

    /**
     * @brief 检查任务是否可启动
     * @param task 任务记录
     * @return 可启动返回 true
     */
    bool canStart(const TaskRecord &task) const;

    /**
     * @brief 检查任务是否可暂停
     * @param task 任务记录
     * @return 可暂停返回 true
     */
    bool canPause(const TaskRecord &task) const;

    /**
     * @brief 检查任务是否可停止
     * @param task 任务记录
     * @return 可停止返回 true
     */
    bool canStop(const TaskRecord &task) const;

    /**
     * @brief 检查任务是否可完成
     * @param task 任务记录
     * @return 可完成返回 true
     */
    bool canFinish(const TaskRecord &task) const;

    /**
     * @brief 检查任务是否处于终止状态
     * @param task 任务记录
     * @return 已终止返回 true
     */
    bool isTerminal(const TaskRecord &task) const;

    std::vector<TaskRecord> tasks_;                  ///< 任务记录列表
    int                     next_task_id_{1};        ///< 下一个任务 ID
    int                     revision_{0};            ///< 数据版本号
    QTimer                 *runtime_timer_{nullptr}; ///< 运行时间刷新定时器
};

/**
 * @brief 任务管理器（单例），组合 TaskTableModel、TaskCommunicationServer 和 TaskEventRouter
 */
class MODEL_API TaskManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(TaskManager)
    QT_QML_SINGLETON(TaskManager)
    Q_PROPERTY(TaskTableModel *tasks READ tasks CONSTANT FINAL)
public:
    ~TaskManager() override = default;

    /**
     * @brief 获取任务表格模型
     * @return 任务表格模型指针
     */
    TaskTableModel *tasks() const
    {
        return tasks_;
    }

    /**
     * @brief 添加任务
     * @param model_uuid 模型 UUID
     * @param model_name 模型名称
     * @param task_type 任务类型
     * @return 任务 ID
     */
    Q_INVOKABLE int addTask(const QString &model_uuid, const QString &model_name, ModelTaskTypes::Type task_type);

    /**
     * @brief 添加任务（可选暂停支持）
     * @param model_uuid 模型 UUID
     * @param model_name 模型名称
     * @param task_type 任务类型
     * @param supports_pause 是否支持暂停
     * @return 任务 ID
     */
    int addTask(const QString &model_uuid, const QString &model_name, ModelTaskTypes::Type task_type,
                bool supports_pause);

    /**
     * @brief 启动任务
     * @param task_id 任务 ID
     * @return 操作成功返回 true
     */
    Q_INVOKABLE bool startTask(int task_id);

    /**
     * @brief 暂停任务
     * @param task_id 任务 ID
     * @return 操作成功返回 true
     */
    Q_INVOKABLE bool pauseTask(int task_id);

    /**
     * @brief 停止任务
     * @param task_id 任务 ID
     * @return 操作成功返回 true
     */
    Q_INVOKABLE bool stopTask(int task_id);

    /**
     * @brief 完成任务
     * @param task_id 任务 ID
     * @return 操作成功返回 true
     */
    Q_INVOKABLE bool finishTask(int task_id);

    /**
     * @brief 标记任务失败
     * @param task_id 任务 ID
     * @return 操作成功返回 true
     */
    bool failTask(int task_id);

    /**
     * @brief 标记任务为已停止
     * @param task_id 任务 ID
     * @return 操作成功返回 true
     */
    bool markTaskStopped(int task_id);

    /**
     * @brief 删除任务
     * @param task_id 任务 ID
     * @return 操作成功返回 true
     */
    Q_INVOKABLE bool deleteTask(int task_id);

    /**
     * @brief 更新任务进度
     * @param task_id 任务 ID
     * @param progress 进度值
     * @return 操作成功返回 true
     */
    Q_INVOKABLE bool updateTaskProgress(int task_id, int progress);

    /**
     * @brief 更新任务预计剩余时间
     * @param task_id 任务 ID
     * @param eta_seconds 预计剩余秒数
     * @return 操作成功返回 true
     */
    bool updateTaskEta(int task_id, qint64 eta_seconds);

    /**
     * @brief 清除所有任务
     */
    void clearTasks();

    /**
     * @brief 确保任务通信服务端已启动
     * @param err_msg 错误信息输出
     * @return 启动成功返回 true
     */
    bool ensureTaskServer(QString *err_msg = nullptr);

    /**
     * @brief 获取任务通信服务端主机地址
     * @return 主机地址
     */
    QString taskServerHost() const;

    /**
     * @brief 获取任务通信服务端端口号
     * @return 端口号
     */
    quint16 taskServerPort() const;

    /**
     * @brief 请求停止任务
     * @param task_id 任务 ID
     * @return 命令发送成功返回 true
     */
    bool requestStopTask(int task_id);

signals:
    void taskStopRequested(int task_id);
    void taskMessageReceived(const dltool::model::TaskMessage &message);

private:
    /**
     * @brief 构造任务管理器
     * @param parent 父对象
     */
    explicit TaskManager(QObject *parent = nullptr);

    TaskTableModel          *tasks_{nullptr};                ///< 任务表格模型
    TaskCommunicationServer *communication_server_{nullptr}; ///< 任务通信服务端
    TaskEventRouter         *event_router_{nullptr};         ///< 任务事件路由器
};

} // namespace dltool::model
