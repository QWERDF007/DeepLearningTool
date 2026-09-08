#include "model/TaskCommunication.h"

#include <spdlog/spdlog.h>

#include <cmath>
#include <limits>

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

bool validateTaskProtocolJson(const QJsonObject &json, TaskMessage *out_message, QString *error_message)
{
    auto fail = [error_message](const QString &msg) {
        if (error_message != nullptr)
            *error_message = msg;
        return false;
    };

    // 1. project_id
    if (!json.contains(taskProtocolFieldName(TaskProtocolField::ProjectId)))
        return fail(QStringLiteral("缺少 project_id 字段"));
    const QJsonValue project_id_val = json.value(taskProtocolFieldName(TaskProtocolField::ProjectId));
    if (!project_id_val.isString() || project_id_val.toString().trimmed().isEmpty())
        return fail(QStringLiteral("project_id 必须为非空字符串"));

    // 2. task_id
    if (!json.contains(taskProtocolFieldName(TaskProtocolField::TaskId)))
        return fail(QStringLiteral("缺少 task_id 字段"));
    const QJsonValue task_id_val = json.value(taskProtocolFieldName(TaskProtocolField::TaskId));
    if (!task_id_val.isDouble())
        return fail(QStringLiteral("task_id 必须为非负整数"));
    const double task_id_d = task_id_val.toDouble();
    if (task_id_d < 0 || std::floor(task_id_d) != task_id_d || task_id_d > std::numeric_limits<int>::max())
        return fail(QStringLiteral("task_id 必须为非负整数"));

    // 3. run_id
    if (!json.contains(taskProtocolFieldName(TaskProtocolField::RunId)))
        return fail(QStringLiteral("缺少 run_id 字段"));
    const QJsonValue run_id_val = json.value(taskProtocolFieldName(TaskProtocolField::RunId));
    if (!run_id_val.isString() || run_id_val.toString().trimmed().isEmpty())
        return fail(QStringLiteral("run_id 必须为非空字符串"));

    // 4. type
    if (!json.contains(taskProtocolFieldName(TaskProtocolField::Type)))
        return fail(QStringLiteral("缺少 type 字段"));
    const QJsonValue type_val = json.value(taskProtocolFieldName(TaskProtocolField::Type));
    if (!type_val.isString())
        return fail(QStringLiteral("type 必须为字符串"));
    const TaskMessageType type = taskMessageTypeFromName(type_val.toString());
    if (type == TaskMessageType::Unknown)
        return fail(QStringLiteral("未知消息类型: %1").arg(type_val.toString()));

    // 5. status
    TaskProtocolStatus status = TaskProtocolStatus::Unknown;
    if (json.contains(taskProtocolFieldName(TaskProtocolField::Status)))
    {
        const QJsonValue status_val = json.value(taskProtocolFieldName(TaskProtocolField::Status));
        if (!status_val.isString())
            return fail(QStringLiteral("status 必须为字符串"));
        status = taskProtocolStatusFromName(status_val.toString());
        if (status == TaskProtocolStatus::Unknown)
            return fail(QStringLiteral("未知任务状态: %1").arg(status_val.toString()));
    }
    else if (type == TaskMessageType::Status)
    {
        return fail(QStringLiteral("status 类型的消息必须包含 status 字段"));
    }

    // 6. progress
    int progress = -1;
    if (json.contains(taskProtocolFieldName(TaskProtocolField::Progress)))
    {
        const QJsonValue progress_val = json.value(taskProtocolFieldName(TaskProtocolField::Progress));
        if (!progress_val.isDouble())
            return fail(QStringLiteral("progress 必须为整数"));
        const double progress_d = progress_val.toDouble();
        if (std::floor(progress_d) != progress_d || (progress_d < 0 && progress_d != -1) || progress_d > 100)
            return fail(QStringLiteral("progress 必须为 0 到 100 的整数 (或 -1)"));
        progress = static_cast<int>(progress_d);
    }
    else if (type == TaskMessageType::Progress)
    {
        return fail(QStringLiteral("progress 类型的消息必须包含 progress 字段"));
    }

    // 7. eta_seconds
    qint64 eta_seconds = -1;
    if (json.contains(taskProtocolFieldName(TaskProtocolField::EtaSeconds)))
    {
        const QJsonValue eta_val = json.value(taskProtocolFieldName(TaskProtocolField::EtaSeconds));
        if (!eta_val.isDouble())
            return fail(QStringLiteral("eta_seconds 必须为整数"));
        const double eta_d = eta_val.toDouble();
        if (std::floor(eta_d) != eta_d || eta_d < -1)
            return fail(QStringLiteral("eta_seconds 必须为 >= -1 的整数"));
        eta_seconds = static_cast<qint64>(eta_d);
    }

    // 8. message
    QString message_text;
    if (json.contains(taskProtocolFieldName(TaskProtocolField::Message)))
    {
        const QJsonValue msg_val = json.value(taskProtocolFieldName(TaskProtocolField::Message));
        if (!msg_val.isString())
            return fail(QStringLiteral("message 必须为字符串"));
        message_text = msg_val.toString();
    }

    // 9. command
    if (json.contains(taskProtocolFieldName(TaskProtocolField::Command)))
    {
        const QJsonValue cmd_val = json.value(taskProtocolFieldName(TaskProtocolField::Command));
        if (!cmd_val.isString())
            return fail(QStringLiteral("command 必须为字符串"));
        const TaskCommand command = taskCommandFromName(cmd_val.toString());
        if (command == TaskCommand::Unknown)
            return fail(QStringLiteral("未知命令: %1").arg(cmd_val.toString()));
    }
    else if (type == TaskMessageType::Command)
    {
        return fail(QStringLiteral("command 类型的消息必须包含 command 字段"));
    }

    if (out_message != nullptr)
    {
        out_message->identity.project_id = project_id_val.toString().trimmed();
        out_message->identity.task_id    = static_cast<int>(task_id_d);
        out_message->identity.run_id     = run_id_val.toString().trimmed();
        out_message->type                = type;
        out_message->status              = status;
        out_message->progress            = progress;
        out_message->eta_seconds         = eta_seconds;
        out_message->message             = message_text;
        out_message->payload             = json.toVariantMap();
    }

    return true;
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
    if (shutting_down_ || socket == nullptr)
        return;

    const TaskIdentity bound_identity = identity_by_socket_.value(socket);

    QJsonParseError     error;
    const QJsonDocument document = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
    {
        spdlog::warn("无效任务通信 JSON 消息: {}", line.toStdString());
        if (bound_identity.isValid())
        {
            TaskMessage failure_message;
            failure_message.identity = bound_identity;
            failure_message.type     = TaskMessageType::Status;
            failure_message.status   = TaskProtocolStatus::Failed;
            failure_message.message  = QStringLiteral("任务通信协议错误: 无效 JSON 格式");
            emit messageReceived(failure_message);
            socket->abort();
        }
        return;
    }

    TaskMessage message;
    QString     validation_error;
    if (!validateTaskProtocolJson(document.object(), &message, &validation_error))
    {
        spdlog::warn("任务通信消息协议校验失败: {}", validation_error.toUtf8().constData());
        if (bound_identity.isValid())
        {
            TaskMessage failure_message;
            failure_message.identity = bound_identity;
            failure_message.type     = TaskMessageType::Status;
            failure_message.status   = TaskProtocolStatus::Failed;
            failure_message.message  = QStringLiteral("任务通信协议错误: %1").arg(validation_error);
            emit messageReceived(failure_message);
            socket->abort();
        }
        return;
    }

    if (bound_identity.isValid())
    {
        if (bound_identity != message.identity)
        {
            spdlog::warn("忽略任务运行身份与连接不一致的任务通信消息: expected=(task={}, run={}), actual=(task={}, run={})",
                         bound_identity.task_id, bound_identity.run_id.toUtf8().constData(),
                         message.identity.task_id, message.identity.run_id.toUtf8().constData());
            return;
        }
    }
    else
    {
        const QPointer<QTcpSocket> existing_socket = socket_by_identity_.value(message.identity);
        if (existing_socket != nullptr && existing_socket != socket)
        {
            spdlog::warn("忽略重复任务运行连接: project_id={}, task_id={}, run_id={}",
                         message.identity.project_id.toUtf8().constData(), message.identity.task_id,
                         message.identity.run_id.toUtf8().constData());
            socket->abort();
            return;
        }

        identity_by_socket_[socket] = message.identity;
        socket_by_identity_[message.identity] = socket;
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
