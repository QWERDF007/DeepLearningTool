#pragma once

#include "dltool/model/Export.h"

#include <QObject>

namespace dltool::model {

class TaskManager;
struct TaskMessage;

class MODEL_API TaskEventRouter : public QObject
{
    Q_OBJECT

public:
    explicit TaskEventRouter(TaskManager *task_manager, QObject *parent = nullptr);

public slots:
    void handleTaskMessage(const dltool::model::TaskMessage &message);

private:
    TaskManager *task_manager_{nullptr};
};

} // namespace dltool::model
