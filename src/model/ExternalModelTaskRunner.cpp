#include "model/ExternalModelTaskRunner.h"

#include "model/TaskManager.h"

#include <spdlog/spdlog.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <QTimer>

namespace dltool::model {

namespace {

QString cleanProcessPath(const QString &path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty())
        return {};
    return QDir::cleanPath(QDir::fromNativeSeparators(trimmed));
}

QFile *openProcessLogFile(const QString &path, QObject *parent, QString *err_msg)
{
    const QString cleaned = cleanProcessPath(path);
    if (cleaned.isEmpty())
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("日志路径为空");
        return nullptr;
    }

    QDir dir(QFileInfo(cleaned).absolutePath());
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("创建日志目录失败: %1").arg(dir.absolutePath());
        return nullptr;
    }

    auto *file = new QFile(cleaned, parent);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("打开日志文件失败: %1, %2").arg(cleaned, file->errorString());
        delete file;
        return nullptr;
    }
    return file;
}

} // namespace

ExternalModelTaskRunner::ExternalModelTaskRunner(QObject *parent)
    : QObject(parent)
{
}

ExternalModelTaskRunner::~ExternalModelTaskRunner() = default;

bool ExternalModelTaskRunner::hasRunningTask(int task_id) const
{
    const auto found = external_processes_.find(task_id);
    return found != external_processes_.end() && found->second && found->second->state() != QProcess::NotRunning;
}

int ExternalModelTaskRunner::start(const PreparedExternalModelTask &task)
{
    auto *task_manager = TaskManager::getInstance();
    if (task_manager == nullptr || task_manager->tasks() == nullptr)
        return -1;

    if (task.task_id < 0)
        return -1;
    if (hasRunningTask(task.task_id))
        return task.task_id;

    if (task.program.isEmpty())
    {
        spdlog::error("启动模型任务失败: 程序路径为空");
        task_manager->tasks()->failTask(task.task_id);
        return task.task_id;
    }
    if (!QFileInfo::exists(task.program))
    {
        spdlog::error("启动模型任务失败: 程序不存在 {}", task.program.toUtf8().constData());
        task_manager->tasks()->failTask(task.task_id);
        return task.task_id;
    }

    auto *process = new QProcess(this);
    QString log_err;
    auto   *process_log = openProcessLogFile(task.log_path, process, &log_err);
    if (process_log == nullptr)
    {
        spdlog::error("启动模型任务失败: {}", log_err.toUtf8().constData());
        task_manager->tasks()->failTask(task.task_id);
        process->deleteLater();
        return task.task_id;
    }

    process->setProgram(task.program);
    process->setArguments(task.arguments);
    process->setWorkingDirectory(task.working_directory);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString       old_python_path = env.value(QStringLiteral("PYTHONPATH"));
    QStringList         python_path_parts = task.python_paths;
    if (!old_python_path.isEmpty())
        python_path_parts.append(old_python_path);
    if (!python_path_parts.isEmpty())
        env.insert(QStringLiteral("PYTHONPATH"), python_path_parts.join(QDir::listSeparator()));
    env.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    env.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    process->setProcessEnvironment(env);
    process->setProcessChannelMode(QProcess::SeparateChannels);

    connect(process, &QProcess::readyReadStandardOutput, this,
            [process, log_file = QPointer<QFile>(process_log)]()
            {
                const QByteArray output = process->readAllStandardOutput();
                if (log_file != nullptr && !output.isEmpty())
                {
                    log_file->write(output);
                    log_file->flush();
                }
            });
    connect(process, &QProcess::readyReadStandardError, this,
            [process, log_file = QPointer<QFile>(process_log)]()
            {
                const QByteArray output = process->readAllStandardError();
                if (log_file != nullptr && !output.isEmpty())
                {
                    log_file->write(output);
                    log_file->flush();
                }
            });
    connect(process, &QProcess::finished, this,
            [this, process, task_id = task.task_id, log_file = QPointer<QFile>(process_log)](
                int exit_code, QProcess::ExitStatus exit_status)
            {
                external_processes_.erase(task_id);
                const bool stop_requested = stop_requested_tasks_.erase(task_id) > 0;
                auto      *task_manager   = TaskManager::getInstance();
                if (task_manager != nullptr && task_manager->tasks() != nullptr)
                {
                    if (stop_requested)
                        task_manager->tasks()->setTaskStatus(task_id, TaskTableModel::Stopped);
                    else if (exit_status == QProcess::NormalExit && exit_code == 0)
                        task_manager->tasks()->setTaskStatus(task_id, TaskTableModel::Finished);
                    else if (exit_status == QProcess::NormalExit && exit_code == 2)
                        task_manager->tasks()->setTaskStatus(task_id, TaskTableModel::Stopped);
                    else
                        task_manager->tasks()->setTaskStatus(task_id, TaskTableModel::Failed);
                }
                if (log_file != nullptr)
                    log_file->close();
                process->deleteLater();
            });

    external_processes_[task.task_id] = process;
    task_manager->tasks()->setTaskStatus(task.task_id, TaskTableModel::Running);
    process->start();
    if (!process->waitForStarted(5000))
    {
        const QString error = process->errorString();
        if (process_log != nullptr)
        {
            process_log->write(error.toUtf8());
            process_log->write("\n");
            process_log->close();
        }
        spdlog::error("启动模型任务失败: {}", error.toUtf8().constData());
        external_processes_.erase(task.task_id);
        task_manager->tasks()->setTaskStatus(task.task_id, TaskTableModel::Failed);
        process->deleteLater();
    }
    return task.task_id;
}

bool ExternalModelTaskRunner::stop(int task_id)
{
    const auto found = external_processes_.find(task_id);
    if (found == external_processes_.end() || !found->second)
        return true;

    stop_requested_tasks_.insert(task_id);
    QProcess *process = found->second;
    if (process->state() != QProcess::NotRunning)
    {
        process->terminate();
        QTimer::singleShot(5000, process,
                           [process]()
                           {
                               if (process->state() != QProcess::NotRunning)
                                   process->kill();
                           });
    }
    return true;
}

bool ExternalModelTaskRunner::deleteTask(int task_id)
{
    stop(task_id);
    external_processes_.erase(task_id);
    stop_requested_tasks_.erase(task_id);
    return true;
}

} // namespace dltool::model
