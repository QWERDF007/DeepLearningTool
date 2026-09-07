#pragma once

#include "dltool/model/Export.h"
#include "model/ExternalProcessSpec.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>

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

    /** @brief 停止全部外部进程并等待收敛，关闭后拒绝新的进程。 */
    void shutdown();

    /**
     * @brief 检查指定任务是否正在运行
     * @param identity 逻辑任务与本次执行身份
     * @return 正在运行返回 true
     */
    bool hasRunningTask(const TaskIdentity &identity) const;

    /**
     * @brief 启动外部进程
     * @param process_spec 进程启动规格
     * @param err_msg 错误信息输出
     * @return 启动成功返回 true
     */
    bool start(const ExternalProcessSpec &process_spec, QString *err_msg = nullptr);

    /**
     * @brief 停止指定任务
     * @param identity 逻辑任务与本次执行身份
     * @return 操作成功返回 true
     */
    bool stop(const TaskIdentity &identity);

    /**
     * @brief 等待所有外部进程退出。
     *
     * 调用前通常先通过 stop() 请求停止。返回时不再有运行中的外部进程；
     * 默认最多等待 5 秒优雅停止；超时后会强制结束残留进程。
     * 传入负数时使用相同的默认优雅等待上限，避免项目关闭无限等待。
     */
    bool waitForDone(int timeout_ms = 5000);

    /**
     * @brief 删除指定任务（先停止再清理）
     * @param identity 逻辑任务与本次执行身份
     * @return 操作成功返回 true
     */
    bool deleteTask(const TaskIdentity &identity);

signals:
    /**
     * @brief 外部 Python 进程已实际启动。
     * @param identity 逻辑任务与本次执行身份。
     */
    void taskStarted(const dltool::model::TaskIdentity &identity);

    /**
     * @brief 外部 Python 进程无法启动。
     * @param identity 逻辑任务与本次执行身份。
     * @param error 启动错误信息。
     */
    void taskStartFailed(const dltool::model::TaskIdentity &identity, const QString &error);

    /**
     * @brief 外部 Python 进程已退出。
     * @param identity 逻辑任务与本次执行身份。
     * @param exit_code 进程退出码。
     * @param normal_exit 是否正常退出。
     * @param stop_requested 是否由用户请求停止。
     */
    void taskFinished(const dltool::model::TaskIdentity &identity, int exit_code, bool normal_exit,
                      bool stop_requested);

private:
    struct RunningProcess
    {
        TaskIdentity       identity;
        QPointer<QProcess> process;
    };

    QHash<TaskIdentity, RunningProcess> external_processes_; ///< 完整任务身份到外部进程的映射。
    QSet<TaskIdentity>                  stop_requested_tasks_; ///< 已请求停止的完整任务身份集合。
    bool                                        shutting_down_{false};
};

} // namespace dltool::model
