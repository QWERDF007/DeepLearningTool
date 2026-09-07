#pragma once

#include "dltool/data/Export.h"

#include <QObject>
#include <QString>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>

namespace dltool::database {
class ProjectDataBase;
} // namespace dltool::database

namespace dltool::data {

/**
 * @brief 统一的数据操作生命周期。
 *
 * 所有跨线程的数据操作都遵循同一条流水线：
 *
 *   提交纯值请求 -> 工作线程消费快照并执行 -> GUI 一次提交结果 -> 完成进度
 *
 * 工作函数捕获的数据必须在提交前完成快照，不能在工作线程回读 DataManager、Qt Model
 * 或 GUI 对象。需要访问数据库时应在工作线程创建归属正确的独立连接；新增实体等操作
 * 增量由 Work 返回，Completion 一定在 context 所在线程执行，因此可以安全地更新
 * QAbstractItemModel 和 QML 状态。
 */
class DATA_API DataOperationWorkflow final
{
public:
    /**
     * @brief 后台数据操作的生命周期句柄。
     *
     * 句柄只观察后台执行状态并发出协作式取消请求；waitForDone() 返回时，
     * 工作函数已经退出，并且完成回调已经入队，或回调上下文已经销毁。
     */
    class DATA_API Handle final
    {
    public:
        /** 请求工作函数尽快取消。已完成操作返回 false。 */
        bool requestCancel();
        /** 返回是否已经发出取消请求。 */
        bool isCancellationRequested() const;
        /** 返回后台工作函数是否已经退出。 */
        bool isFinished() const;
        /** 返回完成回调是否已经执行，或因 context 销毁而被丢弃。 */
        bool isCompletionFinished() const;
        /** 等待工作函数退出并完成通知入队；timeout_ms 小于 0 表示无限等待。 */
        bool waitForDone(int timeout_ms = -1) const;

    private:
        struct State
        {
            std::shared_ptr<std::atomic_bool> cancel_requested = std::make_shared<std::atomic_bool>(false);
            mutable std::mutex                mutex;
            mutable std::condition_variable   condition;
            bool                              finished{false};
            bool                              completion_finished{false};
            bool                              context_destroyed{false};
        };

        explicit Handle(std::shared_ptr<State> state);
        std::shared_ptr<State> state_;

        friend class DataOperationWorkflow;
    };

    using HandlePtr = std::shared_ptr<Handle>;

    struct Result
    {
        bool    success{false};
        bool    cancelled{false};
        QString error;
        qint64  elapsed_ms{0};

        /** 工作函数可用该方法在合适的批次边界响应取消请求。 */
        bool cancellationRequested() const noexcept
        {
            return cancel_token_ != nullptr && cancel_token_->load(std::memory_order_relaxed);
        }

    private:
        std::shared_ptr<std::atomic_bool> cancel_token_;

        friend class DataOperationWorkflow;
    };

    struct Options
    {
        QString title;
        QString start_message;
        int     initial_progress{5};
        bool    manage_progress{true};
    };

    using Work       = std::function<void(Result &)>;
    using DatabaseWork = std::function<void(dltool::database::ProjectDataBase &, Result &)>;
    using Completion = std::function<void(const Result &)>;

    /**
     * @brief 在后台线程执行任意工作，并在 context 线程回调完成阶段。
     */
    static HandlePtr start(QObject *context, Options options, Work work, Completion completion = {});

    /**
     * @brief 在后台线程创建独立数据库连接后执行数据库工作。
     */
    static HandlePtr startDatabase(QObject *context, const QString &database_path, Options options,
                                   DatabaseWork work, Completion completion = {});

private:
    static void beginProgress(const Options &options);
    static void finishProgress(const Options &options, const Result &result);
};

} // namespace dltool::data
