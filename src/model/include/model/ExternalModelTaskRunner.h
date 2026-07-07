#pragma once

#include "dltool/model/Export.h"
#include "model/PreparedExternalModelTask.h"

#include <QObject>
#include <QPointer>
#include <unordered_map>
#include <unordered_set>

class QProcess;

namespace dltool::model {

class MODEL_API ExternalModelTaskRunner : public QObject
{
public:
    explicit ExternalModelTaskRunner(QObject *parent = nullptr);
    ~ExternalModelTaskRunner() override;

    bool hasRunningTask(int task_id) const;
    int  start(const PreparedExternalModelTask &task);
    bool stop(int task_id);
    bool deleteTask(int task_id);

private:
    std::unordered_map<int, QPointer<QProcess>> external_processes_;
    std::unordered_set<int>                     stop_requested_tasks_;
};

} // namespace dltool::model
