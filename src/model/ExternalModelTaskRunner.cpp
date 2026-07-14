#include "model/ExternalModelTaskRunner.h"

#include "common/Utils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <QTimer>

namespace dltool::model {

namespace {

/**
 * @brief 打开进程日志文件
 * @param path 日志文件路径
 * @param parent 父对象
 * @param err_msg 错误信息输出
 * @return 文件指针，失败返回 nullptr
 */
QFile *openProcessLogFile(const QString &path, QObject *parent, QString *err_msg)
{
    const QString cleaned = dltool::common::cleanPath(path);
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

bool ExternalModelTaskRunner::start(const ExternalProcessSpec &process_spec, QString *err_msg)
{
    if (process_spec.task_id < 0)
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("任务 id 无效");
        return false;
    }
    if (hasRunningTask(process_spec.task_id))
        return true;

    if (process_spec.program.isEmpty())
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("程序路径为空");
        return false;
    }
    if (!QFileInfo::exists(process_spec.program))
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("程序不存在: %1").arg(process_spec.program);
        return false;
    }

    auto   *process = new QProcess(this);
    QString log_err;
    auto   *process_log = openProcessLogFile(process_spec.log_path, process, &log_err);
    if (process_log == nullptr)
    {
        if (err_msg != nullptr)
            *err_msg = log_err;
        process->deleteLater();
        return false;
    }

    process->setProgram(process_spec.program);
    process->setArguments(process_spec.arguments);
    process->setWorkingDirectory(process_spec.working_directory);

    QProcessEnvironment env               = QProcessEnvironment::systemEnvironment();
    const QString       old_python_path   = env.value(QStringLiteral("PYTHONPATH"));
    QStringList         python_path_parts = process_spec.python_paths;
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
            [this, process, task_id = process_spec.task_id, log_file = QPointer<QFile>(process_log)](
                int exit_code, QProcess::ExitStatus exit_status)
            {
                external_processes_.erase(task_id);
                const bool stop_requested = stop_requested_tasks_.erase(task_id) > 0;
                if (log_file != nullptr)
                    log_file->close();
                emit taskFinished(task_id, exit_code, exit_status == QProcess::NormalExit, stop_requested);
                process->deleteLater();
            });

    external_processes_[process_spec.task_id] = process;
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
        if (err_msg != nullptr)
            *err_msg = error;
        external_processes_.erase(process_spec.task_id);
        process->deleteLater();
        return false;
    }
    return true;
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
