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
struct TaskMessage;

/**
 * @brief 应用唯一的任务状态中心，同时也是任务中心的表格模型。
 *
 * TaskManager 直接保存任务状态、处理外部任务消息，并向模型控制器发出开始/停止请求。
 * 不维护 TaskSnapshot、QVariantMap 任务副本或额外的事件路由层。
 *
 * 模型任务的状态链为：Pending -> Preparing -> Running -> Stopping -> 终态。
 * Preparing 表示数据集导出和配置写入正在后台执行；只有 Python 进程实际启动后
 * 才会进入 Running。TaskManager 不关心模型、数据集或 Python，只负责记录和分发事件。
 */
class MODEL_API TaskManager final : public QAbstractTableModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(TaskManager)
    QT_QML_SINGLETON(TaskManager)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged FINAL)

public:
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
        Pending = 0, ///< 已创建，尚未开始。
        Preparing,   ///< 正在后台导出数据集、写入配置并准备进程。
        Running,     ///< Python 进程已成功启动，或内部任务正在运行。
        Paused,      ///< 内部任务已暂停。
        Stopping,    ///< 已请求停止，等待后台准备或进程收敛。
        Stopped,     ///< 已停止。
        Finished,    ///< 正常结束。
        Failed,      ///< 准备、启动或执行失败。
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
     * @brief 任务中心持有的唯一任务记录。
     *
     * findTask() 返回的指针仅可在当前 GUI 调用栈内使用，下一次任务中心变更后不可保留。
     */
    struct Task
    {
        int           id{-1};                              ///< 当前项目内递增的任务 ID。
        QString       model_uuid;                           ///< 所属模型 UUID。
        QString       model_name;                           ///< 用于表格显示的模型名称。
        ModelTaskType type{ModelTaskType::Unknown};         ///< 任务类型。
        TaskStatus    status{Pending};                      ///< 当前生命周期状态。
        qint64        created_at{0};                        ///< 创建时间（秒级 Unix 时间戳）。
        qint64        started_at{0};                        ///< 进入 Running 的时间；非运行态为 0。
        qint64        elapsed_seconds{0};                   ///< 已累计的运行时长。
        qint64        eta_seconds{-1};                      ///< 剩余秒数，-1 表示未知。
        int           progress{0};                          ///< 进度（0-100）。
        bool          supports_pause{true};                 ///< 当前任务是否支持暂停。
    };

    /**
     * @brief 析构任务管理器。
     */
    ~TaskManager() override = default;

    /**
     * @brief 返回任务记录数。
     * @param parent 父索引。
     * @return 顶层任务记录数；子索引始终返回 0。
     */
    int rowCount(const QModelIndex &parent = {}) const override;
    /**
     * @brief 返回任务中心列数。
     * @param parent 父索引。
     * @return 顶层列数；子索引始终返回 0。
     */
    int columnCount(const QModelIndex &parent = {}) const override;
    /**
     * @brief 返回任务中心显示值和 QML role 数据。
     * @param index 模型索引。
     * @param role 请求的数据 role。
     * @return 对应的显示值或 role 值；索引无效时返回空值。
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    /**
     * @brief 返回任务中心列标题。
     * @param section 列序号。
     * @param orientation 表头方向。
     * @param role 请求的数据 role。
     * @return 水平显示标题；其他情况返回空值。
     */
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    /**
     * @brief 返回 QML 使用的 role 名称。
     * @return role 到名称的映射。
     */
    QHash<int, QByteArray> roleNames() const override;

    /**
     * @brief 获取当前任务记录数量。
     * @return 任务数量。
     */
    int count() const;
    /**
     * @brief 获取任务状态版本号。
     * @return 每次任务记录或状态变化后递增的版本号，供非表格 QML 绑定刷新。
     */
    int revision() const;

    /**
     * @brief 创建 Pending 任务记录。
     * @param model_uuid 所属模型 UUID。
     * @param model_name 用于任务中心显示的模型名称。
     * @param task_type 模型任务类型。
     * @return 新任务 ID；参数无效时返回 -1。
     */
    Q_INVOKABLE int addTask(const QString &model_uuid, const QString &model_name, ModelTaskTypes::Type task_type);
    /**
     * @brief 创建 Pending 任务记录，并指定其是否支持暂停。
     * @param model_uuid 所属模型 UUID。
     * @param model_name 用于任务中心显示的模型名称。
     * @param task_type 模型任务类型。
     * @param supports_pause 是否允许暂停。
     * @return 新任务 ID；参数无效时返回 -1。
     */
    int addTask(const QString &model_uuid, const QString &model_name, ModelTaskTypes::Type task_type,
                bool supports_pause);

    /**
     * @brief 将可启动任务置为 Preparing，并请求所属控制器继续完整启动链。
     *
     * TaskCenter 和模型页面都调用此函数，因此两处必定走同一条后台准备、Python
     * 启动和状态回写流程。
     * @param task_id 任务 ID。
     * @return 成功提交开始请求返回 true。
     */
    Q_INVOKABLE bool startTask(int task_id);
    /**
     * @brief 暂停支持暂停的运行中内部任务。
     * @param task_id 任务 ID。
     * @return 状态转换成功返回 true。
     */
    Q_INVOKABLE bool pauseTask(int task_id);
    /**
     * @brief 请求停止运行中或正在准备的任务。
     * @param task_id 任务 ID。
     * @return 成功进入 Stopping 并通知所属控制器返回 true。
     */
    Q_INVOKABLE bool stopTask(int task_id);
    /**
     * @brief 将运行中的任务标记为正常完成。
     * @param task_id 任务 ID。
     * @return 状态转换成功返回 true。
     */
    Q_INVOKABLE bool finishTask(int task_id);
    /**
     * @brief 将仍在活动状态的任务标记为失败。
     * @param task_id 任务 ID。
     * @return 状态转换成功返回 true。
     */
    bool              failTask(int task_id);
    /**
     * @brief 将准备完成且 Python 已实际启动的任务置为 Running。
     * @param task_id 任务 ID。
     * @return 状态转换成功返回 true。
     */
    bool              markTaskRunning(int task_id);
    /**
     * @brief 将已收敛的停止请求置为 Stopped。
     * @param task_id 任务 ID。
     * @return 状态转换成功返回 true。
     */
    bool              markTaskStopped(int task_id);
    /**
     * @brief 删除任务记录。
     * @param task_id 任务 ID。
     * @return 记录删除成功返回 true；活动任务会先发出停止请求。
     */
    Q_INVOKABLE bool deleteTask(int task_id);
    /**
     * @brief 更新任务进度。
     * @param task_id 任务 ID。
     * @param progress 进度值，自动限制在 0-100。
     * @return 更新成功返回 true。
     */
    Q_INVOKABLE bool updateTaskProgress(int task_id, int progress);
    /**
     * @brief 更新任务预计剩余时间。
     * @param task_id 任务 ID。
     * @param eta_seconds 剩余秒数；负数表示未知。
     * @return 更新成功返回 true。
     */
    bool              updateTaskEta(int task_id, qint64 eta_seconds);
    /**
     * @brief 清空当前项目的全部任务记录。
     */
    void              clearTasks();

    /**
     * @brief 按模型 UUID 和任务类型查找最新任务。
     * @param model_uuid 模型 UUID。
     * @param task_type 模型任务类型。
     * @param include_finished 是否包含 Finished 任务。
     * @return 任务 ID；未找到时返回 -1。
     */
    Q_INVOKABLE int findModelTask(const QString &model_uuid, ModelTaskTypes::Type task_type,
                                  bool include_finished = false) const;
    /**
     * @brief 获取任务中心保存的唯一任务记录。
     * @param task_id 任务 ID。
     * @return 任务记录指针；不存在时返回 nullptr。指针不可跨事件循环或任务表修改保存。
     */
    const Task      *findTask(int task_id) const;
    /**
     * @brief 获取指定模型的最新任务记录。
     * @param model_uuid 模型 UUID。
     * @param task_type 模型任务类型。
     * @param include_finished 是否包含 Finished 任务。
     * @return 任务记录指针；不存在时返回 nullptr。指针不可跨事件循环或任务表修改保存。
     */
    const Task      *findModelTaskRecord(const QString &model_uuid, ModelTaskTypes::Type task_type,
                                         bool include_finished = false) const;

    /**
     * @brief 查询任务是否可以开始。
     * @param task_id 任务 ID。
     * @return 可开始返回 true。
     */
    Q_INVOKABLE bool canStartTask(int task_id) const;
    /**
     * @brief 查询任务是否可以暂停。
     * @param task_id 任务 ID。
     * @return 可暂停返回 true。
     */
    Q_INVOKABLE bool canPauseTask(int task_id) const;
    /**
     * @brief 查询任务是否可以停止。
     * @param task_id 任务 ID。
     * @return Preparing、Running 或 Paused 状态返回 true。
     */
    Q_INVOKABLE bool canStopTask(int task_id) const;
    /**
     * @brief 判断任务状态是否为终态。
     * @param status 任务状态。
     * @return Stopped、Finished 或 Failed 返回 true。
     */
    static bool       isTerminal(TaskStatus status);

    /**
     * @brief 确保接收 Python 任务事件的 TCP 服务已启动。
     * @param err_msg 启动失败时输出错误信息，可为 nullptr。
     * @return 服务已启动或成功启动返回 true。
     */
    bool    ensureTaskServer(QString *err_msg = nullptr);
    /**
     * @brief 获取 TCP 服务绑定的主机地址。
     * @return 主机地址。
     */
    QString taskServerHost() const;
    /**
     * @brief 获取 TCP 服务绑定的端口。
     * @return 服务未启动时返回 0。
     */
    quint16 taskServerPort() const;

signals:
    void countChanged();
    void revisionChanged();
    /**
     * @brief 任务已经进入 Preparing，请所属控制器提交后台准备工作。
     * @param task_id 任务 ID。
     */
    void taskStartRequested(int task_id);
    /**
     * @brief 用户请求停止，所属控制器应停止进程或收敛仍在执行的后台准备。
     * @param task_id 任务 ID。
     */
    void taskStopRequested(int task_id);
    /**
     * @brief 已处理状态表更新的 Python 任务事件，控制器可据此刷新模型结果。
     * @param message Python 上报的任务事件。
     */
    void taskMessageReceived(const dltool::model::TaskMessage &message);

private slots:
    /**
     * @brief 接收 Python 上报的任务事件并更新任务表。
     * @param message Python 上报的任务消息。
     */
    void handleTaskMessage(const dltool::model::TaskMessage &message);

private:
    /**
     * @brief 构造任务管理器。
     * @param parent 父对象。
     */
    explicit TaskManager(QObject *parent = nullptr);

    /**
     * @brief 根据任务 ID 查找任务表行号。
     * @param task_id 任务 ID。
     * @return 行号；未找到时返回 -1。
     */
    int  rowForTask(int task_id) const;
    /**
     * @brief 根据模型和任务类型查找最新任务表行号。
     * @param model_uuid 模型 UUID。
     * @param task_type 模型任务类型。
     * @param include_finished 是否包含 Finished 任务。
     * @return 行号；未找到时返回 -1。
     */
    int  rowForModelTask(const QString &model_uuid, ModelTaskType task_type, bool include_finished) const;
    /**
     * @brief 更新任务状态并发出局部模型更新信号。
     * @param task_id 任务 ID。
     * @param status 新状态。
     * @return 状态更新成功返回 true。
     */
    bool setTaskStatus(int task_id, TaskStatus status);

    /**
     * @brief 发出指定任务行的数据变更和版本变更信号。
     * @param row 任务表行号。
     * @param roles 已变化的 role 列表。
     */
    void emitTaskChanged(int row, const QList<int> &roles = {});
    /**
     * @brief 刷新运行中任务的显示时长。
     */
    void refreshRunningTasks();

    /**
     * @brief 获取指定任务列的显示数据。
     * @param task 任务记录。
     * @param column 列序号。
     * @return 显示数据。
     */
    QVariant dataForColumn(const Task &task, int column) const;
    /**
     * @brief 获取任务状态显示文本。
     * @param task 任务记录。
     * @return 状态文本。
     */
    QString  statusText(const Task &task) const;
    /**
     * @brief 获取任务创建时间显示文本。
     * @param task 任务记录。
     * @return 格式化时间文本。
     */
    QString  createdAtText(const Task &task) const;
    /**
     * @brief 获取任务运行时长显示文本。
     * @param task 任务记录。
     * @return 格式化时长文本。
     */
    QString  runningTimeText(const Task &task) const;
    /**
     * @brief 获取任务 ETA 显示文本。
     * @param task 任务记录。
     * @return 格式化 ETA 文本。
     */
    QString  etaText(const Task &task) const;
    /**
     * @brief 计算任务累计运行秒数。
     * @param task 任务记录。
     * @return 运行秒数。
     */
    qint64   runningTimeSeconds(const Task &task) const;
    /**
     * @brief 获取任务剩余秒数。
     * @param task 任务记录。
     * @return 剩余秒数；未知时返回 -1。
     */
    qint64   etaSeconds(const Task &task) const;
    /**
     * @brief 判断任务是否可开始。
     * @param task 任务记录。
     * @return 可开始返回 true。
     */
    bool     canStart(const Task &task) const;
    /**
     * @brief 判断任务是否可暂停。
     * @param task 任务记录。
     * @return 可暂停返回 true。
     */
    bool     canPause(const Task &task) const;
    /**
     * @brief 判断任务是否可停止。
     * @param task 任务记录。
     * @return 可停止返回 true。
     */
    bool     canStop(const Task &task) const;
    /**
     * @brief 判断任务是否可标记完成。
     * @param task 任务记录。
     * @return 可标记完成返回 true。
     */
    bool     canFinish(const Task &task) const;

    std::vector<Task>       tasks_;                 ///< 任务中心保存的唯一任务记录。
    int                     next_task_id_{1};       ///< 下一个递增任务 ID。
    int                     revision_{0};           ///< 非表格 QML 刷新版本号。
    QTimer                 *runtime_timer_{nullptr}; ///< 运行时长刷新定时器。
    TaskCommunicationServer *communication_server_{nullptr}; ///< Python 任务通信服务。
};

} // namespace dltool::model
