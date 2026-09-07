#include "model/TaskCommunication.h"

#include <spdlog/spdlog.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>

namespace dltool::model {

namespace {

/**
 * @brief 规范化字符串（去除首尾空白并转小写）
 * @param value 原始字符串
 * @return 规范化后的字符串
 */
QString normalized(const QString &value)
{
    return value.trimmed().toLower();
}

} // namespace

QString taskProtocolFieldName(TaskProtocolField field)
{
    switch (field)
    {
    case TaskProtocolField::ProjectId:
        return QStringLiteral("project_id");
    case TaskProtocolField::TaskId:
        return QStringLiteral("task_id");
    case TaskProtocolField::RunId:
        return QStringLiteral("run_id");
    case TaskProtocolField::Type:
        return QStringLiteral("type");
    case TaskProtocolField::Status:
        return QStringLiteral("status");
    case TaskProtocolField::Progress:
        return QStringLiteral("progress");
    case TaskProtocolField::EtaSeconds:
        return QStringLiteral("eta_seconds");
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
    qRegisterMetaType<TaskIdentity>();
    qRegisterMetaType<TaskMessage>();
    connect(server_, &QTcpServer::newConnection, this, &TaskCommunicationServer::handleNewConnection);
}

TaskCommunicationServer::~TaskCommunicationServer() = default;

void TaskCommunicationServer::shutdown()
{
    if (shutting_down_)
        return;
    shutting_down_ = true;

    if (server_ != nullptr)
        server_->close();

    const QList<QTcpSocket *> sockets = buffers_.keys();
    for (QTcpSocket *socket : sockets)
    {
        if (socket == nullptr)
            continue;
        socket->abort();
        socket->deleteLater();
    }
    buffers_.clear();
    identity_by_socket_.clear();
    socket_by_identity_.clear();
}

bool TaskCommunicationServer::start(QString *err_msg)
{
    if (shutting_down_)
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("任务通信服务正在关闭");
        return false;
    }

    if (server_->isListening())
        return true;

    if (!server_->listen(QHostAddress::LocalHost, 0))
    {
        if (err_msg != nullptr)
            *err_msg = server_->errorString();
        spdlog::error("任务通信服务启动失败: {}", server_->errorString().toUtf8().constData());
        return false;
    }

    spdlog::info("任务通信服务已启动: {}:{}", host().toUtf8().constData(), port());
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

bool TaskCommunicationServer::sendCommand(const TaskIdentity &identity, TaskCommand command,
                                          const QVariantMap &payload)
{
    if (shutting_down_ || !identity.isValid())
        return false;

    if (!server_->isListening() && !start())
        return false;

    const QString command_name = taskCommandName(command);
    if (command_name.isEmpty())
        return false;

    QVariantMap message                                        = payload;
    message[taskProtocolFieldName(TaskProtocolField::Type)]    = taskMessageTypeName(TaskMessageType::Command);
    message[taskProtocolFieldName(TaskProtocolField::ProjectId)] = identity.project_id;
    message[taskProtocolFieldName(TaskProtocolField::TaskId)]  = identity.task_id;
    message[taskProtocolFieldName(TaskProtocolField::RunId)]   = identity.run_id;
    message[taskProtocolFieldName(TaskProtocolField::Command)] = command_name;

    const QPointer<QTcpSocket> socket = socket_by_identity_.value(identity);
    if (socket == nullptr || identity_by_socket_.value(socket.data()) != identity)
        return false;
    writeJson(socket.data(), message);
    return true;
}

void TaskCommunicationServer::handleNewConnection()
{
    if (shutting_down_)
        return;

    while (server_->hasPendingConnections())
    {
        QTcpSocket *socket = server_->nextPendingConnection();
        if (socket == nullptr)
            continue;

        buffers_.insert(socket, QByteArray());
        identity_by_socket_.insert(socket, {});
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { handleReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() { handleDisconnected(socket); });
    }
}

void TaskCommunicationServer::handleReadyRead(QTcpSocket *socket)
{
    if (shutting_down_ || socket == nullptr)
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

    const TaskIdentity identity = identity_by_socket_.value(socket);
    if (identity.isValid() && socket_by_identity_.value(identity) == socket)
    {
        socket_by_identity_.remove(identity);
        emit clientDisconnected(identity);
    }

    buffers_.remove(socket);
    identity_by_socket_.remove(socket);
    socket->deleteLater();
}

void TaskCommunicationServer::processLine(QTcpSocket *socket, const QByteArray &line)
{
    if (shutting_down_)
        return;

    QJsonParseError     error;
    const QJsonDocument document = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
    {
        spdlog::warn("忽略无效任务通信消息: {}", line.toStdString());
        return;
    }

    const QVariantMap object = document.object().toVariantMap();
    TaskMessage message;
    message.identity.project_id
        = object.value(taskProtocolFieldName(TaskProtocolField::ProjectId)).toString().trimmed();
    message.identity.task_id
        = object.value(taskProtocolFieldName(TaskProtocolField::TaskId), -1).toInt();
    message.identity.run_id
        = object.value(taskProtocolFieldName(TaskProtocolField::RunId)).toString().trimmed();
    message.type = taskMessageTypeFromName(object.value(taskProtocolFieldName(TaskProtocolField::Type)).toString());
    message.status
        = taskProtocolStatusFromName(object.value(taskProtocolFieldName(TaskProtocolField::Status)).toString());
    message.progress    = object.value(taskProtocolFieldName(TaskProtocolField::Progress), -1).toInt();
    message.eta_seconds = object.value(taskProtocolFieldName(TaskProtocolField::EtaSeconds), -1).toLongLong();
    message.message     = object.value(taskProtocolFieldName(TaskProtocolField::Message)).toString();
    message.payload     = object;

    if (!message.identity.isValid() || message.type == TaskMessageType::Unknown || socket == nullptr)
    {
        spdlog::warn("忽略缺少有效任务运行身份的任务通信消息");
        return;
    }

    const TaskIdentity bound_identity = identity_by_socket_.value(socket);
    if (bound_identity.isValid() && bound_identity != message.identity)
    {
        spdlog::warn("忽略任务运行身份与连接不一致的任务通信消息: task_id={}", message.identity.task_id);
        return;
    }

    const QPointer<QTcpSocket> existing_socket = socket_by_identity_.value(message.identity);
    if (existing_socket != nullptr && existing_socket != socket)
    {
        spdlog::warn("忽略重复任务运行连接: project_id={}, task_id={}, run_id={}",
                     message.identity.project_id.toUtf8().constData(), message.identity.task_id,
                     message.identity.run_id.toUtf8().constData());
        return;
    }

    identity_by_socket_[socket] = message.identity;
    socket_by_identity_[message.identity] = socket;

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
