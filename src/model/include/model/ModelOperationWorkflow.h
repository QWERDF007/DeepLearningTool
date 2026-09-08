#pragma once

#include "dltool/model/Export.h"
#include "model/ModelLifecycle.h"

#include <QList>
#include <QObject>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>

namespace dltool::model {

/**
 * @brief 统一的模型操作异步流水线。
 *
 * 所有耗时的模型操作（如模型复制、恢复扫描等）遵循流水线：
 *   提交纯值快照 -> 工作线程自有数据库与文件服务 -> GUI 线程一次性提交结果 -> 进度收尾
 *
 * 工作线程禁止回读 ModelManager 或 GUI 对象。
 */
class MODEL_API ModelOperationWorkflow final
{
public:
    class MODEL_API Handle final
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

        friend class ModelOperationWorkflow;
    };

    using HandlePtr = std::shared_ptr<Handle>;

    struct Result
    {
        bool                 success{false};
        bool                 cancelled{false};
        QString              error;
        qint64               elapsed_ms{0};
        ModelLifecycleResult lifecycle_result;
        qint64               model_id{-1};
        QString              uuid;
        QString              name;

        /** 工作函数可用该方法在合适的批次边界响应取消请求。 */
        bool cancellationRequested() const noexcept
        {
            return cancel_token_ != nullptr && cancel_token_->load(std::memory_order_relaxed);
        }

    private:
        std::shared_ptr<std::atomic_bool> cancel_token_;

        friend class ModelOperationWorkflow;
    };

    struct Options
    {
        QString title;
        QString start_message;
        int     initial_progress{5};
        bool    manage_progress{true};
        QString task_id;
    };

    using Work          = std::function<void(Result &)>;
    using LifecycleWork = std::function<void(ModelLifecycle &lifecycle, Result &result)>;
    using Completion    = std::function<void(const Result &)>;

    /**
     * @brief 在后台线程执行任意工作，并在 context 线程回调完成阶段。
     */
    static HandlePtr start(QObject *context, Options options, Work work, Completion completion = {});

    /**
     * @brief 在后台线程创建独立的数据库连接与存储服务，执行 ModelLifecycle 相关操作。
     */
    static HandlePtr startLifecycle(QObject *context, const QString &project_database_path,
                                    const QString &project_dir, Options options,
                                    LifecycleWork work, Completion completion = {});

    /**
     * @brief 异步复制模型。
     */
    static HandlePtr startCopy(QObject *context, const QString &project_database_path,
                               const QString &project_dir, const ModelLifecycleRecord &source,
                               const ModelLifecycleRecord &target, bool copy_train_weights,
                               Options options, Completion completion = {});

    /**
     * @brief 异步恢复未完成的模型操作。
     */
    static HandlePtr startRecovery(QObject *context, const QString &project_database_path,
                                   const QString &project_dir, Options options,
                                   Completion completion = {});

    /**
     * @brief 在当前 context 线程排空一组模型操作的完成回调。
     */
    static bool waitForCompletions(const QList<HandlePtr> &handles, int timeout_ms = -1);

private:
    static void beginProgress(const Options &options);
    static void finishProgress(const Options &options, const Result &result);
};

} // namespace dltool::model
