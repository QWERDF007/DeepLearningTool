#pragma once

#include "dltool/model/Export.h"
#include "model/ExternalProcessSpec.h"

#include <QObject>
#include <QPointer>
#include <unordered_map>
#include <unordered_set>

class QProcess;

namespace dltool::model {

class MODEL_API ExternalModelTaskRunner : public QObject
{
    Q_OBJECT
public:
    explicit ExternalModelTaskRunner(QObject *parent = nullptr);
    ~ExternalModelTaskRunner() override;

    bool hasRunningTask(int task_id) const;
    bool start(const ExternalProcessSpec &process_spec, QString *err_msg = nullptr);
    bool stop(int task_id);
    bool deleteTask(int task_id);

signals:
    void taskFinished(int task_id, int exit_code, bool normal_exit, bool stop_requested);

private:
    std::unordered_map<int, QPointer<QProcess>> external_processes_;
    std::unordered_set<int>                     stop_requested_tasks_;
};

} // namespace dltool::model
