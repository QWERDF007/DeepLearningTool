#include "model/ModelOperationWorkflow.h"

#include "database/DataBase.h"
#include "model/ModelLifecycle.h"
#include "model/ModelStorageService.h"
#include "ui/ProgressManager.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QPointer>
#include <QThread>
#include <QUuid>

#include <spdlog/spdlog.h>

#include <algorithm>

namespace dltool::model {

ModelOperationWorkflow::Handle::Handle(std::shared_ptr<State> state)
    : state_(std::move(state))
{
}

bool ModelOperationWorkflow::Handle::requestCancel()
{
    if (!state_)
        return false;

    std::lock_guard lock(state_->mutex);
    if (state_->finished)
        return false;

    state_->cancel_requested->store(true, std::memory_order_relaxed);
    return true;
}

bool ModelOperationWorkflow::Handle::isCancellationRequested() const
{
    return state_ && state_->cancel_requested->load(std::memory_order_relaxed);
}

bool ModelOperationWorkflow::Handle::isFinished() const
{
    if (!state_)
        return true;

    std::lock_guard lock(state_->mutex);
    return state_->finished;
}

bool ModelOperationWorkflow::Handle::isCompletionFinished() const
{
    if (!state_)
        return true;

    std::lock_guard lock(state_->mutex);
    return state_->completion_finished;
}

bool ModelOperationWorkflow::Handle::waitForDone(const int timeout_ms) const
{
    if (!state_)
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

void ModelOperationWorkflow::beginProgress(const Options &options)
{
    if (!options.manage_progress || options.title.isEmpty())
        return;

    auto *progress = ui::ProgressManager::getInstance();
    if (progress == nullptr)
        return;

    progress->startTask(options.title, options.task_id);
    if (options.initial_progress >= 0)
        progress->updateProgress(options.initial_progress, options.task_id);
    if (!options.start_message.isEmpty())
        progress->addMessage(spdlog::level::info, options.start_message, options.task_id);
}

void ModelOperationWorkflow::finishProgress(const Options &options, const Result &result)
{
    if (!options.manage_progress || options.title.isEmpty())
        return;

    auto *progress = ui::ProgressManager::getInstance();
    if (progress == nullptr)
        return;

    const bool success = result.success && !result.cancelled;
    progress->finishTask(options.task_id, success);
}

ModelOperationWorkflow::HandlePtr ModelOperationWorkflow::start(QObject *context, Options options, Work work,
                                                               Completion completion)
{
    if (context == nullptr || !work)
        return {};

    if (options.manage_progress && options.task_id.isEmpty())
    {
        options.task_id = QStringLiteral("model_workflow_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    }

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
                result.error   = QString::fromUtf8(e.what());
            }
            catch (...)
            {
                result.success = false;
                result.error   = QStringLiteral("后台模型操作发生未知异常");
            }

            result.elapsed_ms = timer.elapsed();
            if (result.cancellationRequested() && !result.success)
            {
                result.cancelled = true;
            }

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
                            finishProgress(options, result);
                            std::lock_guard lock(state->mutex);
                            state->completion_finished = true;
                            state->condition.notify_all();
                            return;
                        }

                        try
                        {
                            if (completion)
                                completion(result);
                        }
                        catch (...)
                        {
                        }

                        finishProgress(options, result);

                        std::lock_guard lock(state->mutex);
                        state->completion_finished = true;
                        state->condition.notify_all();
                    },
                    Qt::QueuedConnection);
            }

            std::lock_guard lock(state->mutex);
            state->finished = true;
            if (!callback_scheduled)
            {
                state->completion_finished = true;
                finishProgress(options, result);
            }
            state->condition.notify_all();
        });

    QObject::connect(context, &QObject::destroyed,
                     [state]()
                     {
                         std::lock_guard lock(state->mutex);
                         state->context_destroyed = true;
                         state->cancel_requested->store(true, std::memory_order_relaxed);
                         state->completion_finished = true;
                         state->condition.notify_all();
                     });

    QObject::connect(worker_thread, &QThread::finished, worker_thread, &QObject::deleteLater);
    worker_thread->start();
    return handle;
}

ModelOperationWorkflow::HandlePtr ModelOperationWorkflow::startLifecycle(
    QObject *context, const QString &project_database_path, const QString &project_dir,
    Options options, LifecycleWork work, Completion completion)
{
    if (!work)
        return {};

    return start(
        context, std::move(options),
        [project_database_path, project_dir, work = std::move(work)](Result &result)
        {
            dltool::database::ProjectDataBase worker_db(project_database_path);
            ProjectModelRecordStore           worker_record_store(&worker_db);
            ModelStorageService               worker_storage(project_dir);
            ModelLifecycle                    worker_lifecycle(worker_record_store, worker_storage);

            work(worker_lifecycle, result);
        },
        std::move(completion));
}

ModelOperationWorkflow::HandlePtr ModelOperationWorkflow::startCopy(
    QObject *context, const QString &project_database_path, const QString &project_dir,
    const ModelLifecycleRecord &source, const ModelLifecycleRecord &target,
    const bool copy_train_weights, Options options, Completion completion)
{
    return startLifecycle(
        context, project_database_path, project_dir, std::move(options),
        [source, target, copy_train_weights](ModelLifecycle &lifecycle, Result &result)
        {
            result.uuid = target.uuid;
            result.name = target.name;
            result.lifecycle_result = lifecycle.copy(
                source, target, copy_train_weights,
                [&result]() { return result.cancellationRequested(); });
            result.model_id = result.lifecycle_result.model_id;
            result.success  = result.lifecycle_result.succeeded();
            result.cancelled = result.lifecycle_result.isCancelled();
            if (!result.success && !result.cancelled)
            {
                result.error = result.lifecycle_result.error;
            }
        },
        std::move(completion));
}

ModelOperationWorkflow::HandlePtr ModelOperationWorkflow::startRecovery(
    QObject *context, const QString &project_database_path, const QString &project_dir,
    Options options, Completion completion)
{
    return startLifecycle(
        context, project_database_path, project_dir, std::move(options),
        [](ModelLifecycle &lifecycle, Result &result)
        {
            result.lifecycle_result = lifecycle.recoverPending(
                [&result]() { return result.cancellationRequested(); });
            result.success   = result.lifecycle_result.succeeded();
            result.cancelled = result.lifecycle_result.isCancelled();
            if (!result.success && !result.cancelled)
            {
                result.error = result.lifecycle_result.error;
            }
        },
        std::move(completion));
}

bool ModelOperationWorkflow::waitForCompletions(const QList<HandlePtr> &handles, const int timeout_ms)
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

        if (!waited_for_worker)
            return false;
    }
}

} // namespace dltool::model
