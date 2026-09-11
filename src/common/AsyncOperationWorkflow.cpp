#include "common/AsyncOperationWorkflow.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMetaObject>
#include <QPointer>
#include <QThread>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <exception>
#include <utility>

namespace dltool::common {

namespace {

/**
 * @brief 由结果字段推导终态。
 * @param result 已收敛的领域结果。
 * @return 取消、成功或失败对应的终态。
 */
AsyncOperationState terminalStateOf(const AsyncOperationResult &result)
{
    if (result.cancelled)
        return AsyncOperationState::Cancelled;
    return result.success ? AsyncOperationState::Completed : AsyncOperationState::Failed;
}

} // namespace

bool AsyncOperationResult::cancellationRequested() const noexcept
{
    return cancel_token_ != nullptr && cancel_token_->load(std::memory_order_relaxed);
}

void AsyncOperationResult::bindCancellationToken(const std::shared_ptr<std::atomic_bool> &cancel_token)
{
    cancel_token_ = cancel_token;
}

AsyncOperationWorkflow::Handle::Handle(std::shared_ptr<State> state)
    : state_(std::move(state))
{
}

bool AsyncOperationWorkflow::Handle::requestCancel()
{
    if (state_ == nullptr)
        return false;

    std::lock_guard lock(state_->mutex);
    if (state_->finished)
        return false;

    state_->cancel_requested->store(true, std::memory_order_relaxed);
    const AsyncOperationState current = state_->state.load(std::memory_order_relaxed);
    if (current == AsyncOperationState::Created || current == AsyncOperationState::Running)
        state_->state.store(AsyncOperationState::Cancelling, std::memory_order_relaxed);
    return true;
}

bool AsyncOperationWorkflow::Handle::isCancellationRequested() const
{
    return state_ != nullptr && state_->cancel_requested->load(std::memory_order_relaxed);
}

bool AsyncOperationWorkflow::Handle::isFinished() const
{
    if (state_ == nullptr)
        return true;

    std::lock_guard lock(state_->mutex);
    return state_->finished;
}

bool AsyncOperationWorkflow::Handle::isCompletionFinished() const
{
    if (state_ == nullptr)
        return true;

    std::lock_guard lock(state_->mutex);
    return state_->completion_finished;
}

AsyncOperationState AsyncOperationWorkflow::Handle::state() const
{
    if (state_ == nullptr)
        return AsyncOperationState::Completed;

    return state_->state.load(std::memory_order_relaxed);
}

bool AsyncOperationWorkflow::Handle::waitForDone(const int timeout_ms) const
{
    if (state_ == nullptr)
        return true;

    std::unique_lock lock(state_->mutex);
    if (timeout_ms < 0)
    {
        state_->condition.wait(lock, [this]() { return state_->finished; });
        return true;
    }

    return state_->condition.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                      [this]() { return state_->finished; });
}

AsyncOperationWorkflow::HandlePtr AsyncOperationWorkflow::startCore(QObject *context, Callbacks callbacks)
{
    if (context == nullptr || !callbacks.work || !callbacks.create_result)
        return {};

    const auto state  = std::make_shared<Handle::State>();
    const auto handle = HandlePtr(new Handle(state));

    QPointer<QObject> callback_context(context);

    // 完成收敛：领域回调只在 context 仍存活时投递；收敛钩子无论领域回调是否投递都恰好调用一次。
    const auto settle = [](const Callbacks &callbacks, const AsyncOperationResult &result,
                           const bool deliver_completion)
    {
        if (deliver_completion && callbacks.completion)
        {
            try
            {
                callbacks.completion(result);
            }
            catch (const std::exception &e)
            {
                spdlog::error("异步操作完成回调异常: {}", e.what());
            }
            catch (...)
            {
                spdlog::error("异步操作完成回调发生未知异常");
            }
        }

        if (callbacks.settled)
        {
            try
            {
                callbacks.settled(result);
            }
            catch (const std::exception &e)
            {
                spdlog::error("异步操作收敛钩子异常: {}", e.what());
            }
            catch (...)
            {
                spdlog::error("异步操作收敛钩子发生未知异常");
            }
        }
    };

    QThread *worker_thread = QThread::create(
        [callback_context, state, callbacks = std::move(callbacks), settle]() mutable
        {
            const std::shared_ptr<AsyncOperationResult> result = callbacks.create_result();
            state->state.store(AsyncOperationState::Running, std::memory_order_relaxed);

            QElapsedTimer timer;
            timer.start();

            try
            {
                callbacks.work(*result, state->cancel_requested);
            }
            catch (const std::exception &e)
            {
                result->success = false;
                result->error   = QString::fromUtf8(e.what());
            }
            catch (...)
            {
                result->success = false;
                result->error   = QStringLiteral("后台操作发生未知异常");
            }

            result->elapsed_ms = timer.elapsed();

            // 取消由实际提交者裁决：提交前取消回滚，提交后必须发布真实结果；
            // 禁止仅根据收到取消请求就把已提交成功改成失败。
            result->cancelled = result->cancellationRequested() && !result->success;

            bool callback_scheduled = false;
            if (callback_context)
            {
                bool context_destroyed = false;
                {
                    std::lock_guard lock(state->mutex);
                    context_destroyed = state->context_destroyed;
                }

                if (!context_destroyed)
                {
                    callback_scheduled = QMetaObject::invokeMethod(
                        callback_context.data(),
                        [callback_context, state, result, callbacks, settle]() mutable
                        {
                            bool context_destroyed_now = false;
                            {
                                std::lock_guard lock(state->mutex);
                                context_destroyed_now = state->context_destroyed;
                            }

                            settle(callbacks, *result, callback_context != nullptr && !context_destroyed_now);

                            std::lock_guard lock(state->mutex);
                            state->completion_finished = true;
                            state->condition.notify_all();
                        },
                        Qt::QueuedConnection);
                }
            }

            if (!callback_scheduled)
            {
                // 没有可用的 context：领域回调无法安全投递，只做一次收敛。
                settle(callbacks, *result, false);

                std::lock_guard lock(state->mutex);
                state->completion_finished = true;
            }

            {
                std::lock_guard lock(state->mutex);
                state->state.store(terminalStateOf(*result), std::memory_order_relaxed);
                state->finished = true;
            }
            state->condition.notify_all();
        });

    // context 可能在项目关闭时先销毁。先发出协作式取消并标记完成通知已丢弃，再由持有者
    // 等待 worker 退出；这里不阻塞销毁路径，避免关闭栅栏与 GUI 线程互相等待。
    QObject::connect(context, &QObject::destroyed, worker_thread,
                     [state]()
                     {
                         state->cancel_requested->store(true, std::memory_order_relaxed);

                         std::lock_guard lock(state->mutex);
                         state->context_destroyed   = true;
                         state->completion_finished = true;
                         const AsyncOperationState current = state->state.load(std::memory_order_relaxed);
                         if (current == AsyncOperationState::Created || current == AsyncOperationState::Running)
                             state->state.store(AsyncOperationState::Cancelling, std::memory_order_relaxed);
                         state->condition.notify_all();
                     });

    QObject::connect(worker_thread, &QThread::finished, worker_thread, &QObject::deleteLater);
    worker_thread->start();
    return handle;
}

bool AsyncOperationWorkflow::waitForCompletions(const QList<HandlePtr> &handles, const int timeout_ms)
{
    QElapsedTimer timer;
    timer.start();

    const auto remainingMilliseconds = [&timer, timeout_ms]()
    {
        if (timeout_ms < 0)
            return -1;
        return std::max(0, timeout_ms - static_cast<int>(timer.elapsed()));
    };

    for (;;)
    {
        bool pending = false;
        for (const auto &handle : handles)
        {
            if (handle != nullptr && (!handle->isFinished() || !handle->isCompletionFinished()))
            {
                pending = true;
                break;
            }
        }

        if (!pending)
            return true;

        const int remaining = remainingMilliseconds();
        if (remaining == 0)
            return false;

        if (QCoreApplication::instance() != nullptr)
        {
            const int event_slice = remaining < 0 ? 10 : std::min(10, remaining);
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, event_slice);
            continue;
        }

        bool waited_for_worker = false;
        for (const auto &handle : handles)
        {
            if (handle != nullptr && !handle->isFinished())
            {
                handle->waitForDone(remaining < 0 ? 10 : std::min(10, remaining));
                waited_for_worker = true;
                break;
            }
        }

        // 没有 Qt 事件循环时 queued completion 无法执行，worker 退出即可返回。
        if (!waited_for_worker)
            return false;
    }
}

} // namespace dltool::common
