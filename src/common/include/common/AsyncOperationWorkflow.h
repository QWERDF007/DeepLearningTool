#pragma once

#include "dltool/common/Export.h"

#include <QList>
#include <QObject>
#include <QString>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>

namespace dltool::common {

/**
 * @brief 异步操作的统一生命周期结果。
 *
 * 领域适配层继承本类型扩展专属字段；公共实现只读写这里声明的核心字段，并在 worker
 * 退出后按“取消请求不覆盖已提交成功”的口径收敛 cancelled：只有 worker 未提交成功时，
 * 取消请求才成为取消结果。
 */
class COMMON_API AsyncOperationResult
{
public:
    bool    success{false};   ///< worker 是否提交成功。
    bool    cancelled{false}; ///< 是否以取消结束。
    QString error;            ///< 失败原因；成功或取消时为空。
    qint64  elapsed_ms{0};    ///< worker 实际执行耗时。

    /**
     * @brief worker 内查询是否已收到取消请求。
     * @return 已请求取消返回 true；未绑定取消令牌时恒为 false。
     */
    bool cancellationRequested() const noexcept;

protected:
    /**
     * @brief 绑定本次执行的取消令牌，由 AsyncOperationWorkflow 在调度时调用。
     * @param cancel_token 本次操作的取消令牌。
     */
    void bindCancellationToken(const std::shared_ptr<std::atomic_bool> &cancel_token);

private:
    std::shared_ptr<std::atomic_bool> cancel_token_;

    friend class AsyncOperationWorkflow;
};

/**
 * @brief 异步操作的生命周期阶段。
 */
enum class AsyncOperationState
{
    Created,    ///< 已创建，worker 尚未开始。
    Running,    ///< worker 正在执行。
    Cancelling, ///< 已收到取消请求，worker 尚未退出。
    Completed,  ///< worker 提交成功。
    Failed,     ///< worker 失败或抛出异常。
    Cancelled,  ///< worker 以取消结束。
};

/**
 * @brief 异步操作的展示选项。
 *
 * 由调用方解释，公共生命周期不读取也不校验；data 与 model 适配层共用同一份字段定义。
 */
struct COMMON_API AsyncOperationOptions
{
    QString title;                 ///< 进度条标题；为空时不展示进度。
    QString start_message;         ///< 启动日志文本。
    int     initial_progress{5};   ///< 启动进度；负数表示不设置。
    bool    manage_progress{true}; ///< 是否展示进度。
    QString task_id;               ///< 进度归属标识，由调用方保证唯一。
};

/**
 * @brief 全应用唯一的异步操作生命周期实现。
 *
 * 只负责操作句柄与共享状态、取消请求、worker 执行与异常传播、完成回调单次投递、
 * 有限与无期限等待，以及关闭期间的收敛。不依赖 data、model、database、feature 或
 * project 的业务类型。
 *
 * 进度展示不属于本组件：它由调用方在 settled 钩子中挂接，避免 common 反向依赖 ui。
 */
class COMMON_API AsyncOperationWorkflow final
{
public:
    /**
     * @brief 后台操作的生命周期句柄。
     *
     * 句柄只观察执行状态并发起协作式取消；waitForDone() 返回时 worker 已退出。
     */
    class COMMON_API Handle final
    {
    public:
        /** @brief 请求 worker 尽快取消。已完成的操作返回 false。 */
        bool requestCancel();
        /** @brief 返回是否已收到取消请求。 */
        bool isCancellationRequested() const;
        /** @brief 返回 worker 是否已退出。 */
        bool isFinished() const;
        /** @brief 返回完成通知是否已投递，或因 context 销毁而被丢弃。 */
        bool isCompletionFinished() const;
        /** @brief 返回当前生命周期阶段；无效句柄视为已收敛。 */
        AsyncOperationState state() const;
        /** @brief 等待 worker 退出；timeout_ms 小于 0 表示无期限等待。 */
        bool waitForDone(int timeout_ms = -1) const;

    private:
        struct State
        {
            std::shared_ptr<std::atomic_bool> cancel_requested = std::make_shared<std::atomic_bool>(false);
            std::atomic<AsyncOperationState>  state{AsyncOperationState::Created};
            mutable std::mutex                mutex;
            mutable std::condition_variable   condition;
            bool                              finished{false};
            bool                              completion_finished{false};
            bool                              context_destroyed{false};
        };

        explicit Handle(std::shared_ptr<State> state);

        std::shared_ptr<State> state_;

        friend class AsyncOperationWorkflow;
    };

    using HandlePtr = std::shared_ptr<Handle>;

    /**
     * @brief 在后台线程执行 work，并在 context 线程单次投递完成通知。
     *
     * work 必须使用冻结的纯值输入和自身连接/文件资源，不得回读 GUI 对象或 manager
     * 的当前值。completion 与 settled 均在 context 所属线程投递；context 已销毁或
     * 不存在时二者都不投递，只保证一次收敛通知。
     *
     * @param context 完成通知所属线程对象；为空时不执行操作。
     * @param work 后台工作函数，异常由公共实现捕获并转成失败结果。
     * @param completion 领域完成回调，可为空。
     * @param settled 完成收敛钩子，可为空；无论 completion 是否投递都恰好调用一次。
     * @return 操作句柄；参数无效时返回空。
     */
    template <typename Result>
    static HandlePtr start(QObject *context, std::function<void(Result &)> work,
                           std::function<void(const Result &)> completion = {},
                           std::function<void(const AsyncOperationResult &)> settled = {})
    {
        static_assert(std::is_base_of_v<AsyncOperationResult, Result>,
                      "异步操作结果类型必须继承 common::AsyncOperationResult");

        Callbacks callbacks;
        callbacks.create_result = []() -> std::unique_ptr<AsyncOperationResult> { return std::make_unique<Result>(); };
        callbacks.work          = [work = std::move(work)](AsyncOperationResult &result,
                                                           const std::shared_ptr<std::atomic_bool> &cancel_token)
        {
            auto &typed_result = static_cast<Result &>(result);
            typed_result.bindCancellationToken(cancel_token);
            if (work)
                work(typed_result);
        };
        if (completion)
        {
            callbacks.completion = [completion = std::move(completion)](const AsyncOperationResult &result)
            { completion(static_cast<const Result &>(result)); };
        }
        callbacks.settled = std::move(settled);

        return startCore(context, std::move(callbacks));
    }

    /**
     * @brief 在当前 context 线程排空一组操作的完成回调。
     *
     * 调用方必须运行在这些句柄 context 所属的 Qt 线程中。等待期间处理排除用户输入的
     * Qt 事件，因此 queued completion 可以更新其所属模型；worker 已退出但完成回调尚未
     * 执行不视为完成。timeout_ms 小于 0 表示无期限等待。
     *
     * @param handles 待等待的句柄列表。
     * @param timeout_ms 等待上限；负数表示无期限。
     * @return 全部 worker 退出且完成通知已收敛返回 true。
     */
    static bool waitForCompletions(const QList<HandlePtr> &handles, int timeout_ms = -1);

private:
    /**
     * @brief 类型擦除后的执行契约，使模板入口与状态机实现分离。
     */
    struct Callbacks
    {
        std::function<std::unique_ptr<AsyncOperationResult>()>                                 create_result;
        std::function<void(AsyncOperationResult &, const std::shared_ptr<std::atomic_bool> &)> work;
        std::function<void(const AsyncOperationResult &)>                                      completion;
        std::function<void(const AsyncOperationResult &)>                                      settled;
    };

    /**
     * @brief 唯一的生命周期状态机实现。
     * @param context 完成通知所属线程对象。
     * @param callbacks 类型擦除后的执行契约。
     * @return 操作句柄；参数无效时返回空。
     */
    static HandlePtr startCore(QObject *context, Callbacks callbacks);
};

} // namespace dltool::common
