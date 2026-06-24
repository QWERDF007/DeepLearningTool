#include "model/TaskCommunication.h"

#include <spdlog/spdlog.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>

namespace dltool::model {

namespace {

QString normalized(const QString &value)
{
    return value.trimmed().toLower();
}

} // namespace

QString taskProtocolFieldName(TaskProtocolField field)
{
    switch (field)
    {
    case TaskProtocolField::TaskId:
        return QStringLiteral("task_id");
    case TaskProtocolField::Type:
        return QStringLiteral("type");
    case TaskProtocolField::Status:
        return QStringLiteral("status");
    case TaskProtocolField::Progress:
        return QStringLiteral("progress");
    case TaskProtocolField::Message:
        return QStringLiteral("message");
    case TaskProtocolField::Command:
        return QStringLiteral("command");
    default:
        return {};
    }
}

QString taskMessageTypeName(TaskMessageType type)
{
    switch (type)
    {
    case TaskMessageType::Event:
        return QStringLiteral("event");
    case TaskMessageType::Status:
        return QStringLiteral("status");
    case TaskMessageType::Progress:
        return QStringLiteral("progress");
    case TaskMessageType::Log:
        return QStringLiteral("log");
    case TaskMessageType::Command:
        return QStringLiteral("command");
    case TaskMessageType::Unknown:
    default:
        return {};
    }
}

QString taskProtocolStatusName(TaskProtocolStatus status)
{
    switch (status)
    {
    case TaskProtocolStatus::Pending:
        return QStringLiteral("pending");
    case TaskProtocolStatus::Running:
        return QStringLiteral("running");
    case TaskProtocolStatus::Paused:
        return QStringLiteral("paused");
    case TaskProtocolStatus::Stopped:
        return QStringLiteral("stopped");
    case TaskProtocolStatus::Finished:
        return QStringLiteral("finished");
    case TaskProtocolStatus::Failed:
        return QStringLiteral("failed");
    case TaskProtocolStatus::Error:
        return QStringLiteral("error");
    case TaskProtocolStatus::Unknown:
    default:
        return {};
    }
}

QString taskCommandName(TaskCommand command)
{
    switch (command)
    {
    case TaskCommand::Stop:
        return QStringLiteral("stop");
    case TaskCommand::Unknown:
    default:
        return {};
    }
}

TaskMessageType taskMessageTypeFromName(const QString &name)
{
    const QString value = normalized(name);
    if (value == taskMessageTypeName(TaskMessageType::Event))
        return TaskMessageType::Event;
    if (value == taskMessageTypeName(TaskMessageType::Status))
        return TaskMessageType::Status;
    if (value == taskMessageTypeName(TaskMessageType::Progress))
        return TaskMessageType::Progress;
    if (value == taskMessageTypeName(TaskMessageType::Log))
        return TaskMessageType::Log;
    if (value == taskMessageTypeName(TaskMessageType::Command))
        return TaskMessageType::Command;
    return TaskMessageType::Unknown;
}

TaskProtocolStatus taskProtocolStatusFromName(const QString &name)
{
    const QString value = normalized(name);
    if (value == taskProtocolStatusName(TaskProtocolStatus::Pending))
        return TaskProtocolStatus::Pending;
    if (value == taskProtocolStatusName(TaskProtocolStatus::Running))
        return TaskProtocolStatus::Running;
    if (value == taskProtocolStatusName(TaskProtocolStatus::Paused))
        return TaskProtocolStatus::Paused;
    if (value == taskProtocolStatusName(TaskProtocolStatus::Stopped))
        return TaskProtocolStatus::Stopped;
    if (value == taskProtocolStatusName(TaskProtocolStatus::Finished))
        return TaskProtocolStatus::Finished;
    if (value == taskProtocolStatusName(TaskProtocolStatus::Failed))
        return TaskProtocolStatus::Failed;
    if (value == taskProtocolStatusName(TaskProtocolStatus::Error))
        return TaskProtocolStatus::Error;
    return TaskProtocolStatus::Unknown;
}

TaskCommand taskCommandFromName(const QString &name)
{
    const QString value = normalized(name);
    if (value == taskCommandName(TaskCommand::Stop))
        return TaskCommand::Stop;
    return TaskCommand::Unknown;
}

TaskCommunicationServer::TaskCommunicationServer(QObject *parent)
    : QObject(parent)
    , server_(new QTcpServer(this))
{
    connect(server_, &QTcpServer::newConnection, this, &TaskCommunicationServer::handleNewConnection);
}

TaskCommunicationServer::~TaskCommunicationServer() = default;

bool TaskCommunicationServer::start(QString *err_msg)
{
    if (server_->isListening())
        return true;

    if (!server_->listen(QHostAddress::LocalHost, 0))
    {
        if (err_msg != nullptr)
            *err_msg = server_->errorString();
        spdlog::error("任务通信服务启动失败: {}", server_->errorString().toStdString());
        return false;
    }

    spdlog::info("任务通信服务已启动: {}:{}", host().toStdString(), port());
    return true;
}

QString TaskCommunicationServer::host() const
{
    return QStringLiteral("127.0.0.1");
}

quint16 TaskCommunicationServer::port() const
{
    return server_->serverPort();
}

bool TaskCommunicationServer::sendCommand(int task_id, TaskCommand command, const QVariantMap &payload)
{
    if (!server_->isListening() && !start())
        return false;

    const QString command_name = taskCommandName(command);
    if (command_name.isEmpty())
        return false;

    QVariantMap message = payload;
    message[taskProtocolFieldName(TaskProtocolField::Type)]    = taskMessageTypeName(TaskMessageType::Command);
    message[taskProtocolFieldName(TaskProtocolField::TaskId)]  = task_id;
    message[taskProtocolFieldName(TaskProtocolField::Command)] = command_name;

    bool sent = false;
    if (QPointer<QTcpSocket> socket = socket_by_task_.value(task_id); socket != nullptr)
    {
        writeJson(socket.data(), message);
        sent = true;
    }
    else
    {
        for (QTcpSocket *socket : buffers_.keys())
        {
            if (socket == nullptr)
                continue;
            writeJson(socket, message);
            sent = true;
        }
    }
    return sent;
}

void TaskCommunicationServer::handleNewConnection()
{
    while (server_->hasPendingConnections())
    {
        QTcpSocket *socket = server_->nextPendingConnection();
        if (socket == nullptr)
            continue;

        buffers_.insert(socket, QByteArray());
        task_by_socket_.insert(socket, -1);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { handleReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() { handleDisconnected(socket); });
    }
}

void TaskCommunicationServer::handleReadyRead(QTcpSocket *socket)
{
    if (socket == nullptr)
        return;

    QByteArray buffer = buffers_.value(socket);
    buffer.append(socket->readAll());

    int newline = buffer.indexOf('\n');
    while (newline >= 0)
    {
        const QByteArray line = buffer.left(newline).trimmed();
        buffer.remove(0, newline + 1);
        if (!line.isEmpty())
            processLine(socket, line);
        newline = buffer.indexOf('\n');
    }

    buffers_[socket] = buffer;
}

void TaskCommunicationServer::handleDisconnected(QTcpSocket *socket)
{
    if (socket == nullptr)
        return;

    const int task_id = task_by_socket_.value(socket, -1);
    if (task_id >= 0 && socket_by_task_.value(task_id) == socket)
    {
        socket_by_task_.remove(task_id);
        emit clientDisconnected(task_id);
    }

    buffers_.remove(socket);
    task_by_socket_.remove(socket);
    socket->deleteLater();
}

void TaskCommunicationServer::processLine(QTcpSocket *socket, const QByteArray &line)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
    {
        spdlog::warn("忽略无效任务通信消息: {}", line.toStdString());
        return;
    }

    const QVariantMap object = document.object().toVariantMap();
    TaskMessage       message;
    message.task_id = object.value(taskProtocolFieldName(TaskProtocolField::TaskId), -1).toInt();
    message.type    = taskMessageTypeFromName(object.value(taskProtocolFieldName(TaskProtocolField::Type)).toString());
    message.status
        = taskProtocolStatusFromName(object.value(taskProtocolFieldName(TaskProtocolField::Status)).toString());
    message.progress = object.value(taskProtocolFieldName(TaskProtocolField::Progress), -1).toInt();
    message.message  = object.value(taskProtocolFieldName(TaskProtocolField::Message)).toString();
    message.payload  = object;

    if (message.task_id >= 0 && socket != nullptr)
    {
        task_by_socket_[socket]             = message.task_id;
        socket_by_task_[message.task_id]    = socket;
    }

    emit messageReceived(message);
}

void TaskCommunicationServer::writeJson(QTcpSocket *socket, const QVariantMap &message)
{
    if (socket == nullptr || socket->state() != QAbstractSocket::ConnectedState)
        return;

    const QByteArray payload = QJsonDocument::fromVariant(message).toJson(QJsonDocument::Compact) + '\n';
    socket->write(payload);
    socket->flush();
}

} // namespace dltool::model
