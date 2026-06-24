#pragma once

#include "dltool/model/Export.h"

#include <QAbstractTableModel>
#include <QTimer>
#include <QtQml>
#include <vector>

namespace dltool::model {

class TaskCommunicationServer;
struct TaskMessage;

class MODEL_API TaskTableModel : public QAbstractTableModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(TaskTableModel)
    QML_UNCREATABLE("Can not create TaskTableModel directly!")
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
public:
    explicit TaskTableModel(QObject *parent = nullptr);
    ~TaskTableModel() override = default;

    enum Column
    {
        ModelNameColumn = 0,
        StatusColumn,
        CreatedAtColumn,
        RunningTimeColumn,
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
        StatusRole,
        StatusValueRole,
        CreatedAtRole,
        RunningTimeRole,
        ProgressRole,
        CanStartRole,
        CanPauseRole,
        CanStopRole,
        CanFinishRole,
    };
    Q_ENUM(Role)

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    int count() const;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    QHash<int, QByteArray> roleNames() const override;

    int  addTask(const QString &model_uuid, const QString &model_name, const QString &task_type = QString(),
                 bool external_process = false, bool supports_pause = true);
    bool startTask(int task_id);
    bool pauseTask(int task_id);
    bool stopTask(int task_id);
    bool finishTask(int task_id);
    bool failTask(int task_id);
    bool setTaskStatus(int task_id, TaskStatus status);
    bool deleteTask(int task_id);
    bool updateTaskProgress(int task_id, int progress);
    int  startModelTask(const QString &model_uuid, const QString &model_name, const QString &task_type = QString());
    bool stopModelTask(const QString &model_uuid, const QString &task_type = QString());

    Q_INVOKABLE QVariantMap taskAt(int row) const;

signals:
    void countChanged();

private:
    struct TaskRecord
    {
        int        task_id{-1};
        QString    model_uuid;
        QString    model_name;
        QString    task_type;
        TaskStatus status{Pending};
        qint64     created_at{0};
        qint64     started_at{0};
        qint64     accumulated_seconds{0};
        int        progress{0};
        bool       external_process{false};
        bool       supports_pause{true};
    };

    int  indexOfTask(int task_id) const;
    int  indexOfModelTask(const QString &model_uuid, const QString &task_type, bool include_finished) const;
    void emitTaskChanged(int row, const QList<int> &roles = {});
    void refreshRunningTasks();

    QVariant dataForColumn(const TaskRecord &task, int column) const;
    QString  statusText(const TaskRecord &task) const;
    QString  createdAtText(const TaskRecord &task) const;
    QString  runningTimeText(const TaskRecord &task) const;
    qint64   runningTimeSeconds(const TaskRecord &task) const;
    bool     canStart(const TaskRecord &task) const;
    bool     canPause(const TaskRecord &task) const;
    bool     canStop(const TaskRecord &task) const;
    bool     canFinish(const TaskRecord &task) const;
    bool     isTerminal(const TaskRecord &task) const;

    std::vector<TaskRecord> tasks_;
    int                     next_task_id_{1};
    QTimer                 *runtime_timer_{nullptr};
};

class MODEL_API TaskManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(TaskManager)
    QML_UNCREATABLE("Can not create TaskManager directly!")
    Q_PROPERTY(TaskTableModel *tasks READ tasks CONSTANT FINAL)
public:
    explicit TaskManager(QObject *parent = nullptr);
    ~TaskManager() override = default;

    TaskTableModel *tasks() const
    {
        return tasks_;
    }

    Q_INVOKABLE int addTask(const QString &model_uuid, const QString &model_name, const QString &task_type = QString());
    int addExternalTask(const QString &model_uuid, const QString &model_name, const QString &task_type = QString());
    Q_INVOKABLE bool startTask(int task_id);
    Q_INVOKABLE bool pauseTask(int task_id);
    Q_INVOKABLE bool stopTask(int task_id);
    Q_INVOKABLE bool finishTask(int task_id);
    bool             failTask(int task_id);
    Q_INVOKABLE bool deleteTask(int task_id);
    Q_INVOKABLE bool updateTaskProgress(int task_id, int progress);
    Q_INVOKABLE int  startModelTask(const QString &model_uuid, const QString &model_name,
                                    const QString &task_type = QString());
    Q_INVOKABLE bool stopModelTask(const QString &model_uuid, const QString &task_type = QString());

    bool    ensureTaskServer(QString *err_msg = nullptr);
    QString taskServerHost() const;
    quint16 taskServerPort() const;

signals:
    void taskStopRequested(int task_id);

private:
    TaskTableModel          *tasks_{nullptr};
    TaskCommunicationServer *communication_server_{nullptr};

    void handleTaskMessage(const dltool::model::TaskMessage &message);
};

} // namespace dltool::model
