#include "model/ExternalModelTaskRunner.h"

#include "common/Utils.h"

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

/// 日志批量写达到该字节数时立即落盘。
constexpr qint64 kLogFlushThresholdBytes = 64 * 1024;
/// 日志批量写的最长攒积时间（毫秒），定时器触发时落盘一次。
constexpr int    kLogFlushIntervalMs = 200;
/// 停止进程时两次终止信号之间的等待时间（毫秒）。
constexpr int    kStopGracePeriodMs = 5000;

/**
 * @brief 进程日志批量写入器。
 *
 * 高频输出脚本会触发大量 readyRead 事件，逐块 write+flush 会频繁刷盘；
 * 该类攒积待写入数据，达到 64KB 或 200ms 定时器触发时一次写盘，进程
 * 结束时冲刷残余数据并关闭文件。文件生命周期与 QProcess 一致。
 */
class LogSink : public QObject
{
public:
    /**
     * @brief 构造日志写入器。
     * @param file 日志文件，随进程对象销毁。
     * @param parent 父对象（一般为 QProcess）。
     */
    explicit LogSink(QFile *file, QObject *parent = nullptr)
        : QObject(parent)
        , file_(file)
    {
        timer_.setSingleShot(true);
        timer_.setInterval(kLogFlushIntervalMs);
        connect(&timer_, &QTimer::timeout, this, [this]() { flush(); });
    }

    /**
     * @brief 追加进程输出到待写缓冲。
     * @param data 本次读取的输出块。
     */
    void append(const QByteArray &data)
    {
        if (data.isEmpty())
            return;
        pending_.append(data);
        if (pending_.size() >= kLogFlushThresholdBytes)
        {
            timer_.stop();
            flush();
        }
        else if (!timer_.isActive())
        {
            timer_.start();
        }
    }

    /**
     * @brief 立即将待写缓冲一次写入文件并刷新。
     */
    void flush()
    {
        timer_.stop();
        if (file_ != nullptr && !pending_.isEmpty())
        {
            file_->write(pending_);
            file_->flush();
        }
        pending_.clear();
    }

    /**
     * @brief 进程结束时冲刷残余数据并关闭日志文件。
     */
    void finish()
    {
        flush();
        if (file_ != nullptr)
            file_->close();
    }

private:
    QFile     *file_{nullptr}; ///< 日志文件，随进程对象销毁。
    QByteArray pending_;       ///< 待写入日志缓冲。
    QTimer     timer_;         ///< 落盘节流定时器。
};

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
            *err_msg = QString("日志路径为空");
        return nullptr;
    }

    QDir dir(QFileInfo(cleaned).absolutePath());
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
    {
        if (err_msg != nullptr)
            *err_msg = QString("创建日志目录失败: %1").arg(dir.absolutePath());
        return nullptr;
    }

    auto *file = new QFile(cleaned, parent);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (err_msg != nullptr)
            *err_msg = QString("打开日志文件失败: %1, %2").arg(cleaned, file->errorString());
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
            *err_msg = QString("任务 id 无效");
        return false;
    }
    if (hasRunningTask(process_spec.task_id))
        return true;

    if (process_spec.program.isEmpty())
    {
        if (err_msg != nullptr)
            *err_msg = QString("程序路径为空");
        return false;
    }
    if (!QFileInfo::exists(process_spec.program))
    {
        if (err_msg != nullptr)
            *err_msg = QString("程序不存在: %1").arg(process_spec.program);
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

    // 日志批量写：readyRead 只把输出攒进缓冲，由 LogSink 按 64KB/200ms 落盘。
    auto *sink = new LogSink(process_log, process);
    connect(process, &QProcess::readyReadStandardOutput, this,
            [process, sink = QPointer<LogSink>(sink)]()
            {
                const QByteArray output = process->readAllStandardOutput();
                if (sink != nullptr)
                    sink->append(output);
            });
    connect(process, &QProcess::readyReadStandardError, this,
            [process, sink = QPointer<LogSink>(sink)]()
            {
                const QByteArray output = process->readAllStandardError();
                if (sink != nullptr)
                    sink->append(output);
            });
    connect(process, &QProcess::started, this, [this, task_id = process_spec.task_id]() { emit taskStarted(task_id); });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, task_id = process_spec.task_id](QProcess::ProcessError error)
            {
                if (error != QProcess::FailedToStart)
                    return;

                const QString message = process->errorString();
                external_processes_.erase(task_id);
                stop_requested_tasks_.erase(task_id);
                QObject::disconnect(process, nullptr, this, nullptr);
                process->deleteLater();
                emit taskStartFailed(task_id, message);
            });
    connect(process, &QProcess::finished, this,
            [this, process, task_id = process_spec.task_id, sink = QPointer<LogSink>(sink)](
                int exit_code, QProcess::ExitStatus exit_status)
            {
                external_processes_.erase(task_id);
                const bool stop_requested = stop_requested_tasks_.erase(task_id) > 0;
                // 进程结束：冲刷日志残余并关闭文件。
                if (sink != nullptr)
                    sink->finish();
                emit taskFinished(task_id, exit_code, exit_status == QProcess::NormalExit, stop_requested);
                process->deleteLater();
            });

    external_processes_[process_spec.task_id] = process;
    process->start();
    return true;
}

bool ExternalModelTaskRunner::stop(int task_id)
{
    const auto found = external_processes_.find(task_id);
    if (found == external_processes_.end() || !found->second)
        return true;

    stop_requested_tasks_.insert(task_id);
    QProcess *process = found->second;
    if (process->state() == QProcess::NotRunning)
        return true;

    // 分级停止：先 terminate，5s 仍未退出再次 terminate，再 5s 仍不退出
    // 才 kill，避免一次 kill 中断脚本的清理逻辑。
    const auto escalate = [process](const int attempt)
    {
        QTimer::singleShot(kStopGracePeriodMs, process,
                           [process, attempt]()
                           {
                               if (process->state() == QProcess::NotRunning)
                                   return;
                               if (attempt == 1)
                               {
                                   spdlog::warn("任务进程 {} 首次终止后仍在运行, 再次发送终止信号",
                                                process->processId());
                                   process->terminate();
                                   QTimer::singleShot(kStopGracePeriodMs, process,
                                                      [process]()
                                                      {
                                                          if (process->state() == QProcess::NotRunning)
                                                              return;
                                                          spdlog::warn("任务进程 {} 两次终止后仍未退出, 强制结束",
                                                                       process->processId());
                                                          process->kill();
                                                      });
                               }
                           });
    };
    spdlog::info("请求停止任务进程 {}", process->processId());
    process->terminate();
    escalate(1);
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
