#pragma once

#include "dltool/model/Export.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

class QTcpServer;
class QTcpSocket;

namespace dltool::model {

enum class TaskProtocolField
{
    TaskId,
    Type,
    Status,
    Progress,
    EtaSeconds,
    Message,
    Command,
};

enum class TaskMessageType
{
    Unknown,
    Event,
    Status,
    Progress,
    Log,
    Command,
};

enum class TaskProtocolStatus
{
    Unknown,
    Pending,
    Running,
    Paused,
    Stopped,
    Finished,
    Failed,
    Error,
};

enum class TaskCommand
{
    Unknown,
    Stop,
};

MODEL_API QString            taskProtocolFieldName(TaskProtocolField field);
MODEL_API QString            taskMessageTypeName(TaskMessageType type);
MODEL_API QString            taskProtocolStatusName(TaskProtocolStatus status);
MODEL_API QString            taskCommandName(TaskCommand command);
MODEL_API TaskMessageType    taskMessageTypeFromName(const QString &name);
MODEL_API TaskProtocolStatus taskProtocolStatusFromName(const QString &name);
MODEL_API TaskCommand        taskCommandFromName(const QString &name);

struct MODEL_API TaskMessage
{
    int                task_id{-1};
    TaskMessageType    type{TaskMessageType::Unknown};
    TaskProtocolStatus status{TaskProtocolStatus::Unknown};
    int                progress{-1};
    qint64             eta_seconds{-1};
    QString            message;
    QVariantMap        payload;
};

class MODEL_API TaskCommunicationServer : public QObject
{
    Q_OBJECT

public:
    explicit TaskCommunicationServer(QObject *parent = nullptr);
    ~TaskCommunicationServer() override;

    bool    start(QString *err_msg = nullptr);
    QString host() const;
    quint16 port() const;

    bool sendCommand(int task_id, TaskCommand command, const QVariantMap &payload = {});

signals:
    void messageReceived(const dltool::model::TaskMessage &message);
    void clientDisconnected(int task_id);

private:
    void handleNewConnection();
    void handleReadyRead(QTcpSocket *socket);
    void handleDisconnected(QTcpSocket *socket);
    void processLine(QTcpSocket *socket, const QByteArray &line);
    void writeJson(QTcpSocket *socket, const QVariantMap &message);

    QTcpServer                      *server_{nullptr};
    QHash<QTcpSocket *, QByteArray>  buffers_;
    QHash<QTcpSocket *, int>         task_by_socket_;
    QHash<int, QPointer<QTcpSocket>> socket_by_task_;
};

} // namespace dltool::model
