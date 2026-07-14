#pragma once

#include "dltool/model/Export.h"

#include <QObject>

namespace dltool::model {

class TaskManager;
struct TaskMessage;

/**
 * @brief 任务事件路由器，将 TaskCommunicationServer 收到的消息转发到 TaskManager 更新任务状态
 */
class MODEL_API TaskEventRouter : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造任务事件路由器
     * @param task_manager 任务管理器
     * @param parent 父对象
     */
    explicit TaskEventRouter(TaskManager *task_manager, QObject *parent = nullptr);

public slots:
    /**
     * @brief 处理任务消息
     * @param message 任务消息
     */
    void handleTaskMessage(const dltool::model::TaskMessage &message);

private:
    TaskManager *task_manager_{nullptr}; ///< 任务管理器
};

} // namespace dltool::model
