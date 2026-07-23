#pragma once

#include "dltool/model/Export.h"
#include "model/ExternalProcessSpec.h"

#include <QObject>
#include <QPointer>
#include <unordered_map>
#include <unordered_set>

class QProcess;

namespace dltool::model {

/**
 * @brief 外部模型任务运行器，管理外部 Python 训练/测试进程的生命周期
 */
class MODEL_API ExternalModelTaskRunner : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief 构造外部任务运行器
     * @param parent 父对象
     */
    explicit ExternalModelTaskRunner(QObject *parent = nullptr);

    ~ExternalModelTaskRunner() override;

    /**
     * @brief 检查指定任务是否正在运行
     * @param task_id 任务 ID
     * @return 正在运行返回 true
     */
    bool hasRunningTask(int task_id) const;

    /**
     * @brief 启动外部进程
     * @param process_spec 进程启动规格
     * @param err_msg 错误信息输出
     * @return 启动成功返回 true
     */
    bool start(const ExternalProcessSpec &process_spec, QString *err_msg = nullptr);

    /**
     * @brief 停止指定任务
     * @param task_id 任务 ID
     * @return 操作成功返回 true
     */
    bool stop(int task_id);

    /**
     * @brief 删除指定任务（先停止再清理）
     * @param task_id 任务 ID
     * @return 操作成功返回 true
     */
    bool deleteTask(int task_id);

signals:
    /**
     * @brief 外部 Python 进程已实际启动。
     * @param task_id 任务 ID。
     */
    void taskStarted(int task_id);

    /**
     * @brief 外部 Python 进程无法启动。
     * @param task_id 任务 ID。
     * @param error 启动错误信息。
     */
    void taskStartFailed(int task_id, const QString &error);

    /**
     * @brief 外部 Python 进程已退出。
     * @param task_id 任务 ID。
     * @param exit_code 进程退出码。
     * @param normal_exit 是否正常退出。
     * @param stop_requested 是否由用户请求停止。
     */
    void taskFinished(int task_id, int exit_code, bool normal_exit, bool stop_requested);

private:
    std::unordered_map<int, QPointer<QProcess>> external_processes_;   ///< task_id 到进程对象的映射。
    std::unordered_set<int>                     stop_requested_tasks_; ///< 已请求停止的任务集合。
};

} // namespace dltool::model
