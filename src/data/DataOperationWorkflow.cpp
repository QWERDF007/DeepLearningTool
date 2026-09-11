#include "data/DataOperationWorkflow.h"

#include "database/DataBase.h"
#include "ui/ProgressManager.h"

#include <QUuid>
#include <spdlog/spdlog.h>

#include <utility>

namespace dltool::data {

void DataOperationWorkflow::beginProgress(const Options &options)
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

void DataOperationWorkflow::finishProgress(const Options &options, const common::AsyncOperationResult &result)
{
    if (!options.manage_progress || options.title.isEmpty())
        return;

    const bool success = result.success && !result.cancelled;
    ui::ProgressManager::getInstance()->finishTask(options.task_id, success);
}

DataOperationWorkflow::HandlePtr DataOperationWorkflow::start(QObject *context, Options options, Work work,
                                                             Completion completion)
{
    if (context == nullptr || !work)
        return {};

    if (options.manage_progress && options.task_id.isEmpty())
    {
        options.task_id = QStringLiteral("data_workflow_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    }

    beginProgress(options);

    return common::AsyncOperationWorkflow::start<Result>(
        context, std::move(work), std::move(completion),
        [options](const common::AsyncOperationResult &result) { finishProgress(options, result); });
}

DataOperationWorkflow::HandlePtr DataOperationWorkflow::startDatabase(QObject *context, const QString &database_path,
                                                                      Options options, DatabaseWork work,
                                                                      Completion completion)
{
    if (!work)
        return {};

    return start(
        context, std::move(options),
        [database_path, work = std::move(work)](Result &result)
        {
            dltool::database::ProjectDataBase database(database_path);
            work(database, result);
        },
        std::move(completion));
}

bool DataOperationWorkflow::waitForCompletions(const QList<HandlePtr> &handles, const int timeout_ms)
{
    return common::AsyncOperationWorkflow::waitForCompletions(handles, timeout_ms);
}

} // namespace dltool::data
