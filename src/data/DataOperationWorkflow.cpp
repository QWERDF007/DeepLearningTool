#include "data/DataOperationWorkflow.h"

#include "database/DataBase.h"
#include "ui/ProgressManager.h"

#include <spdlog/spdlog.h>

#include <QElapsedTimer>
#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <exception>
#include <utility>

namespace dltool::data {

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

void DataOperationWorkflow::start(QObject *context, Options options, Work work, Completion completion)
{
    if (context == nullptr || !work)
    {
        return;
    }

    beginProgress(options);

    QPointer<QObject> callback_context(context);
    QThread *worker_thread = QThread::create(
        [callback_context, options = std::move(options), work = std::move(work), completion = std::move(completion)]()
        mutable
        {
            Result result;
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
            if (!callback_context)
            {
                return;
            }

            QMetaObject::invokeMethod(
                callback_context.data(),
                [callback_context, options = std::move(options), result = std::move(result),
                 completion = std::move(completion)]() mutable
                {
                    if (!callback_context)
                    {
                        return;
                    }

                    if (completion)
                    {
                        completion(result);
                    }
                    finishProgress(options, result);
                },
                Qt::QueuedConnection);
        });

    // context 可能在项目关闭时先销毁。等待工作线程结束，保证后台工作不会继续访问
    // context 所属的 DataManager、DataIO 或内存模型。
    QObject::connect(context, &QObject::destroyed, worker_thread,
                     [worker_thread]()
                     {
                         if (worker_thread->isRunning())
                         {
                             worker_thread->wait();
                         }
                     });
    QObject::connect(worker_thread, &QThread::finished, worker_thread, &QObject::deleteLater);
    worker_thread->start();
}

void DataOperationWorkflow::startDatabase(QObject *context, const QString &database_path, Options options,
                                          DatabaseWork work, Completion completion)
{
    if (!work)
    {
        return;
    }

    start(
        context, std::move(options),
        [database_path, work = std::move(work)](Result &result) mutable
        {
            dltool::database::ProjectDataBase database(database_path);
            work(database, result);
        },
        std::move(completion));
}

} // namespace dltool::data
