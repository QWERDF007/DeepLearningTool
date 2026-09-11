#include "model/ModelOperationWorkflow.h"

#include "database/DataBase.h"
#include "model/ModelStorageService.h"
#include "ui/ProgressManager.h"

#include <QUuid>
#include <spdlog/spdlog.h>

#include <utility>

namespace dltool::model {

void ModelOperationWorkflow::beginProgress(const Options &options)
{
    if (!options.manage_progress || options.title.isEmpty())
        return;

    auto *progress = ui::ProgressManager::getInstance();
    progress->startTask(options.title, options.task_id);
    if (options.initial_progress >= 0)
    {
        progress->updateProgress(options.initial_progress, options.task_id);
    }
    if (!options.start_message.isEmpty())
    {
        progress->addMessage(spdlog::level::info, options.start_message, options.task_id);
    }
}

void ModelOperationWorkflow::finishProgress(const Options &options, const common::AsyncOperationResult &result)
{
    if (!options.manage_progress || options.title.isEmpty())
        return;

    const bool success = result.success && !result.cancelled;
    ui::ProgressManager::getInstance()->finishTask(options.task_id, success);
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

    return common::AsyncOperationWorkflow::start<Result>(
        context, std::move(work), std::move(completion),
        [options](const common::AsyncOperationResult &result) { finishProgress(options, result); });
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
    return common::AsyncOperationWorkflow::waitForCompletions(handles, timeout_ms);
}

} // namespace dltool::model
