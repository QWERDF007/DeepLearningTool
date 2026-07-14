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

/**
 * @brief 获取协议字段名称
 * @param field 协议字段枚举
 * @return 字段名称
 */
MODEL_API QString taskProtocolFieldName(TaskProtocolField field);

/**
 * @brief 获取消息类型名称
 * @param type 消息类型枚举
 * @return 类型名称
 */
MODEL_API QString taskMessageTypeName(TaskMessageType type);

/**
 * @brief 获取协议状态名称
 * @param status 协议状态枚举
 * @return 状态名称
 */
MODEL_API QString taskProtocolStatusName(TaskProtocolStatus status);

/**
 * @brief 获取命令名称
 * @param command 命令枚举
 * @return 命令名称
 */
MODEL_API QString taskCommandName(TaskCommand command);

/**
 * @brief 从名称解析消息类型
 * @param name 名称字符串
 * @return 消息类型枚举
 */
MODEL_API TaskMessageType taskMessageTypeFromName(const QString &name);

/**
 * @brief 从名称解析协议状态
 * @param name 名称字符串
 * @return 协议状态枚举
 */
MODEL_API TaskProtocolStatus taskProtocolStatusFromName(const QString &name);

/**
 * @brief 从名称解析命令
 * @param name 名称字符串
 * @return 命令枚举
 */
MODEL_API TaskCommand taskCommandFromName(const QString &name);

/**
 * @brief 任务消息，描述外部进程通过 TCP 发送的任务状态更新
 */
struct MODEL_API TaskMessage
{
    int                task_id{-1};                         ///< 任务 ID
    TaskMessageType    type{TaskMessageType::Unknown};      ///< 消息类型
    TaskProtocolStatus status{TaskProtocolStatus::Unknown}; ///< 任务状态
    int                progress{-1};                        ///< 进度（0-100）
    qint64             eta_seconds{-1};                     ///< 预计剩余秒数
    QString            message;                             ///< 消息文本
    QVariantMap        payload;                             ///< 附加数据
};

/**
 * @brief 任务通信服务端，监听本地 TCP 端口接收外部训练进程的状态上报并转发命令
 */
class MODEL_API TaskCommunicationServer : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造通信服务端
     * @param parent 父对象
     */
    explicit TaskCommunicationServer(QObject *parent = nullptr);
    ~TaskCommunicationServer() override;

    /**
     * @brief 启动服务端监听
     * @param err_msg 错误信息输出
     * @return 启动成功返回 true
     */
    bool start(QString *err_msg = nullptr);

    /**
     * @brief 获取服务端主机地址
     * @return 主机地址
     */
    QString host() const;

    /**
     * @brief 获取服务端端口号
     * @return 端口号
     */
    quint16 port() const;

    /**
     * @brief 向指定任务发送命令
     * @param task_id 任务 ID
     * @param command 命令类型
     * @param payload 附加数据
     * @return 发送成功返回 true
     */
    bool sendCommand(int task_id, TaskCommand command, const QVariantMap &payload = {});

signals:
    void messageReceived(const dltool::model::TaskMessage &message);
    void clientDisconnected(int task_id);

private:
    /**
     * @brief 处理新的 TCP 连接
     */
    void handleNewConnection();

    /**
     * @brief 处理 socket 可读事件
     * @param socket TCP socket
     */
    void handleReadyRead(QTcpSocket *socket);

    /**
     * @brief 处理 socket 断开事件
     * @param socket TCP socket
     */
    void handleDisconnected(QTcpSocket *socket);

    /**
     * @brief 解析一行 JSON 消息
     * @param socket TCP socket
     * @param line 单行数据
     */
    void processLine(QTcpSocket *socket, const QByteArray &line);

    /**
     * @brief 向 socket 写入 JSON 消息
     * @param socket TCP socket
     * @param message 消息键值对
     */
    void writeJson(QTcpSocket *socket, const QVariantMap &message);

    QTcpServer *server_{nullptr}; ///< TCP 服务端

    QHash<QTcpSocket *, QByteArray> buffers_; ///< 接收缓冲区映射

    QHash<QTcpSocket *, int> task_by_socket_; ///< socket 到 task_id 的映射

    QHash<int, QPointer<QTcpSocket>> socket_by_task_; ///< task_id 到 socket 的映射
};

} // namespace dltool::model
