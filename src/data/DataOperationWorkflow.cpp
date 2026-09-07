#include "data/DataOperationWorkflow.h"

#include "database/DataBase.h"
#include "ui/ProgressManager.h"

#include <spdlog/spdlog.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <algorithm>
#include <exception>
#include <utility>

namespace dltool::data {

DataOperationWorkflow::Handle::Handle(std::shared_ptr<State> state)
    : state_(std::move(state))
{
}

bool DataOperationWorkflow::Handle::requestCancel()
{
    if (state_ == nullptr)
        return false;

    std::lock_guard lock(state_->mutex);
    if (state_->finished)
        return false;
    state_->cancel_requested->store(true, std::memory_order_relaxed);
    return true;
}

bool DataOperationWorkflow::Handle::isCancellationRequested() const
{
    return state_ != nullptr && state_->cancel_requested->load(std::memory_order_relaxed);
}

bool DataOperationWorkflow::Handle::isFinished() const
{
    if (state_ == nullptr)
        return true;

    std::lock_guard lock(state_->mutex);
    return state_->finished;
}

bool DataOperationWorkflow::Handle::isCompletionFinished() const
{
    if (state_ == nullptr)
        return true;

    std::lock_guard lock(state_->mutex);
    return state_->completion_finished;
}

bool DataOperationWorkflow::Handle::waitForDone(const int timeout_ms) const
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

void DataOperationWorkflow::beginProgress(const Options &options)
{
    if (!options.manage_progress || options.title.isEmpty())
    {
        return;
    }

    auto *progress = ui::ProgressManager::getInstance();
    progress->startTask(options.title);
    if (options.initial_progress >= 0)
    {
        progress->updateProgress(options.initial_progress);
    }
    if (!options.start_message.isEmpty())
    {
        progress->addMessage(spdlog::level::info, options.start_message);
    }
}

void DataOperationWorkflow::finishProgress(const Options &options, const Result &result)
{
    if (!options.manage_progress || options.title.isEmpty())
    {
        return;
    }

    if (result.success)
    {
        ui::ProgressManager::getInstance()->updateProgress(100);
    }
    ui::ProgressManager::getInstance()->completeTask();
}

DataOperationWorkflow::HandlePtr DataOperationWorkflow::start(QObject *context, Options options, Work work,
                                                              Completion completion)
{
    if (context == nullptr || !work)
        return {};

    beginProgress(options);

    const auto state  = std::make_shared<Handle::State>();
    const auto handle = HandlePtr(new Handle(state));
    QPointer<QObject> callback_context(context);
    QThread *worker_thread = QThread::create(
        [callback_context, state, options = std::move(options), work = std::move(work),
         completion = std::move(completion)]() mutable
        {
            Result result;
            result.cancel_token_ = state->cancel_requested;
            QElapsedTimer timer;
            timer.start();

            try
            {
                work(result);
            }
            catch (const std::exception &e)
            {
                result.success = false;
                result.error   = QString(e.what());
            }
            catch (...)
            {
                result.success = false;
                result.error   = QString("后台数据操作发生未知异常");
            }

            result.elapsed_ms = timer.elapsed();
            result.cancelled   = result.cancellationRequested();
            if (result.cancelled)
                result.success = false;

            bool callback_scheduled = false;
            if (callback_context)
            {
                bool context_destroyed = false;
                {
                    std::lock_guard lock(state->mutex);
                    context_destroyed = state->context_destroyed;
                }

                callback_scheduled = !context_destroyed && QMetaObject::invokeMethod(
                    callback_context.data(),
                    [callback_context, state, options = std::move(options), result = std::move(result),
                     completion = std::move(completion)]() mutable
                    {
                        bool context_destroyed = false;
                        {
                            std::lock_guard lock(state->mutex);
                            context_destroyed = state->context_destroyed;
                        }
                        if (!callback_context || context_destroyed)
                        {
                            std::lock_guard lock(state->mutex);
                            state->completion_finished = true;
                            state->condition.notify_all();
                            return;
                        }

                        try
                        {
                            if (completion)
                                completion(result);
                            finishProgress(options, result);
                        }
                        catch (const std::exception &e)
                        {
                            spdlog::error("数据操作完成回调异常: {}", e.what());
                        }
                        catch (...)
                        {
                            spdlog::error("数据操作完成回调发生未知异常");
                        }

                        {
                            std::lock_guard lock(state->mutex);
                            state->completion_finished = true;
                        }
                        state->condition.notify_all();
                    },
                    Qt::QueuedConnection);
            }

            if (!callback_scheduled)
            {
                std::lock_guard lock(state->mutex);
                state->completion_finished = true;
            }

            {
                std::lock_guard lock(state->mutex);
                state->finished = true;
            }
            state->condition.notify_all();
        });

    // context 可能在项目关闭时先销毁。先发出协作式取消，再等待工作函数结束，
    // 保证后台工作不会继续访问 context 所属的 DataManager、DataIO 或内存模型。
    QObject::connect(context, &QObject::destroyed, worker_thread,
                     [state]()
                     {
                         state->cancel_requested->store(true, std::memory_order_relaxed);
                         {
                             std::lock_guard lock(state->mutex);
                             state->context_destroyed    = true;
                             state->completion_finished = true;
                         }
                         state->condition.notify_all();

                         // The caller that owns the destroyed context is often
                         // the project close barrier and will wait on the handle.
                         // Do not block QObject destruction here; marking the
                         // callback as discarded is sufficient to let that
                         // barrier wait for the worker without a GUI deadlock.
                     });
    QObject::connect(worker_thread, &QThread::finished, worker_thread, &QObject::deleteLater);
    worker_thread->start();
    return handle;
}

DataOperationWorkflow::HandlePtr DataOperationWorkflow::startDatabase(QObject *context, const QString &database_path,
                                                                       Options options, DatabaseWork work,
                                                                       Completion completion)
{
    if (!work)
        return {};

    return start(
        context, std::move(options),
        [database_path, work = std::move(work)](Result &result) mutable
        {
            dltool::database::ProjectDataBase database(database_path);
            work(database, result);
        },
        std::move(completion));
}

bool DataOperationWorkflow::waitForCompletions(const QList<HandlePtr> &handles, const int timeout_ms)
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

        // Without a Qt event loop a queued completion cannot execute. Return
        // once workers have stopped instead of sleeping forever on the callback.
        if (!waited_for_worker)
            return false;
    }
}

} // namespace dltool::data
