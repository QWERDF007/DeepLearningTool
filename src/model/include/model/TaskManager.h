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

class MODEL_API TaskTableModel : public QAbstractTableModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(TaskTableModel)
    QML_UNCREATABLE("Can not create TaskTableModel directly!")
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged FINAL)
public:
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
        Pending = 0,
        Running,
        Paused,
        Stopping,
        Stopped,
        Finished,
        Failed,
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

    struct TaskSnapshot
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
        bool          can_start{false};
        bool          can_pause{false};
        bool          can_stop{false};
        bool          can_finish{false};

        bool isValid() const
        {
            return task_id >= 0;
        }
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    int count() const;
    int revision() const;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    QHash<int, QByteArray> roleNames() const override;

    int  addTask(const QString &model_uuid, const QString &model_name, ModelTaskTypes::Type task_type,
                 bool supports_pause = true);
    bool startTask(int task_id);
    bool pauseTask(int task_id);
    bool stopTask(int task_id);
    bool finishTask(int task_id);
    bool failTask(int task_id);
    bool setTaskStatus(int task_id, TaskStatus status);
    bool deleteTask(int task_id);
    bool updateTaskProgress(int task_id, int progress);
    bool updateTaskEta(int task_id, qint64 eta_seconds);
    void clearTasks();
    int  findModelTask(const QString &model_uuid, ModelTaskTypes::Type task_type, bool include_finished = false) const;
    TaskSnapshot taskSnapshotForId(int task_id) const;
    TaskSnapshot taskSnapshotForModel(const QString &model_uuid, ModelTaskTypes::Type task_type,
                                      bool include_finished = false) const;

    Q_INVOKABLE QVariantMap taskAt(int row) const;
    Q_INVOKABLE QVariantMap taskForId(int task_id) const;
    Q_INVOKABLE QVariantMap taskForModel(const QString &model_uuid, ModelTaskTypes::Type task_type,
                                         bool include_finished = false) const;

signals:
    void countChanged();
    void revisionChanged();

private:
    struct TaskRecord
    {
        int        task_id{-1};
        QString    model_uuid;
        QString    model_name;
        ModelTaskType task_type{ModelTaskType::Unknown};
        TaskStatus status{Pending};
        qint64     created_at{0};
        qint64     started_at{0};
        qint64     accumulated_seconds{0};
        qint64     eta_seconds{-1};
        int        progress{0};
        bool       supports_pause{true};
    };

    int  indexOfTask(int task_id) const;
    int  indexOfModelTask(const QString &model_uuid, ModelTaskTypes::Type task_type, bool include_finished) const;
    TaskSnapshot snapshotForRow(int row) const;
    void emitTaskChanged(int row, const QList<int> &roles = {});
    void bumpRevision();
    void refreshRunningTasks();

    QVariant dataForColumn(const TaskRecord &task, int column) const;
    QString  statusText(const TaskRecord &task) const;
    QString  createdAtText(const TaskRecord &task) const;
    QString  runningTimeText(const TaskRecord &task) const;
    QString  etaText(const TaskRecord &task) const;
    qint64   runningTimeSeconds(const TaskRecord &task) const;
    qint64   etaSeconds(const TaskRecord &task) const;
    bool     canStart(const TaskRecord &task) const;
    bool     canPause(const TaskRecord &task) const;
    bool     canStop(const TaskRecord &task) const;
    bool     canFinish(const TaskRecord &task) const;
    bool     isTerminal(const TaskRecord &task) const;

    std::vector<TaskRecord> tasks_;
    int                     next_task_id_{1};
    int                     revision_{0};
    QTimer                 *runtime_timer_{nullptr};
};

class MODEL_API TaskManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(TaskManager)
    QT_QML_SINGLETON(TaskManager)
    Q_PROPERTY(TaskTableModel *tasks READ tasks CONSTANT FINAL)
public:
    ~TaskManager() override = default;

    TaskTableModel *tasks() const
    {
        return tasks_;
    }

    Q_INVOKABLE int addTask(const QString &model_uuid, const QString &model_name, ModelTaskTypes::Type task_type);
    int              addTask(const QString &model_uuid, const QString &model_name, ModelTaskTypes::Type task_type,
                             bool supports_pause);
    Q_INVOKABLE bool startTask(int task_id);
    Q_INVOKABLE bool pauseTask(int task_id);
    Q_INVOKABLE bool stopTask(int task_id);
    Q_INVOKABLE bool finishTask(int task_id);
    bool             failTask(int task_id);
    bool             markTaskStopped(int task_id);
    Q_INVOKABLE bool deleteTask(int task_id);
    Q_INVOKABLE bool updateTaskProgress(int task_id, int progress);
    bool             updateTaskEta(int task_id, qint64 eta_seconds);
    void             clearTasks();
    bool             ensureTaskServer(QString *err_msg = nullptr);
    QString          taskServerHost() const;
    quint16          taskServerPort() const;
    bool             requestStopTask(int task_id);

signals:
    void taskStopRequested(int task_id);

private:
    explicit TaskManager(QObject *parent = nullptr);

    TaskTableModel          *tasks_{nullptr};
    TaskCommunicationServer *communication_server_{nullptr};
    TaskEventRouter         *event_router_{nullptr};

};

} // namespace dltool::model
