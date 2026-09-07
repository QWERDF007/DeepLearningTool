#include "model/TensorBoardRunner.h"

#include <spdlog/spdlog.h>

#include <QHostAddress>
#include <QProcess>
#include <QTcpServer>

namespace dltool::model {

namespace {

constexpr int kGracefulStopTimeoutMs = 1000;
constexpr int kForcedStopTimeoutMs   = 1000;

bool setError(QString *err_msg, const QString &message)
{
    if (err_msg != nullptr)
        *err_msg = message;
    return false;
}

} // namespace

TensorBoardRunner::TensorBoardRunner(QObject *parent)
    : QObject(parent)
{
}

TensorBoardRunner::~TensorBoardRunner()
{
    shutdown();
}

quint16 TensorBoardRunner::availableLocalPort()
{
    QTcpServer server;
    if (server.listen(QHostAddress::LocalHost, 6006))
    {
        server.close();
        return 6006;
    }

    if (server.listen(QHostAddress::LocalHost, 0))
    {
        const quint16 port = server.serverPort();
        server.close();
        return port;
    }

    return 0;
}

bool TensorBoardRunner::start(const TensorBoardLaunchSpec &spec, QString *err_msg)
{
    if (shutting_down_)
        return setError(err_msg, QStringLiteral("TensorBoard 运行器正在关闭"));

    if (spec.model_uuid.trimmed().isEmpty())
        return setError(err_msg, QStringLiteral("TensorBoard 模型身份为空"));
    if (spec.program.trimmed().isEmpty())
        return setError(err_msg, QStringLiteral("TensorBoard 程序路径为空"));
    if (spec.port == 0)
        return setError(err_msg, QStringLiteral("TensorBoard 端口无效"));

    if (isRunning() && model_uuid_ == spec.model_uuid.trimmed())
        return true;

    if (process_ != nullptr && !stopCurrent())
        return setError(err_msg, QStringLiteral("停止旧 TensorBoard 进程失败"));

    auto *process = new QProcess(this);
    process->setProgram(spec.program);
    process->setArguments(spec.arguments);
    if (!spec.environment.isEmpty())
        process->setProcessEnvironment(spec.environment);

    connect(process, &QProcess::readyReadStandardError, this,
            [process]()
            {
                const QByteArray output = process->readAllStandardError();
                if (!output.isEmpty())
                    spdlog::error("TensorBoard: {}", QString::fromLocal8Bit(output).trimmed().toUtf8().constData());
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process](const QProcess::ProcessError error)
            {
                if (error != QProcess::FailedToStart)
                    return;

                spdlog::error("TensorBoard 进程启动失败: {}",
                              process->errorString().toUtf8().constData());
                if (process_ == process)
                    clearCurrent();
                process->deleteLater();
            });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process](const int exit_code, const QProcess::ExitStatus exit_status)
            {
                if (exit_code != 0 || exit_status != QProcess::NormalExit)
                {
                    spdlog::error("TensorBoard 异常退出, 退出码: {}, 状态: {}", exit_code,
                                  exit_status == QProcess::NormalExit ? "normal" : "crashed");
                }
                if (process_ == process)
                    clearCurrent();
                process->deleteLater();
            });

    process_   = process;
    model_uuid_ = spec.model_uuid.trimmed();
    port_        = spec.port;
    process->start();
    return true;
}

void TensorBoardRunner::shutdown()
{
    if (shutting_down_)
        return;

    shutting_down_ = true;
    stopCurrent();
}

bool TensorBoardRunner::isRunning() const
{
    return process_ != nullptr && process_->state() != QProcess::NotRunning;
}

QString TensorBoardRunner::modelUuid() const
{
    return model_uuid_;
}

quint16 TensorBoardRunner::port() const
{
    return port_;
}

bool TensorBoardRunner::stopCurrent()
{
    QProcess *process = process_.data();
    if (process == nullptr)
    {
        clearCurrent();
        return true;
    }

    QObject::disconnect(process, nullptr, this, nullptr);
    if (process->state() != QProcess::NotRunning)
    {
        process->terminate();
        if (!process->waitForFinished(kGracefulStopTimeoutMs))
        {
            process->kill();
            if (!process->waitForFinished(kForcedStopTimeoutMs))
            {
                spdlog::error("TensorBoard 进程无法在关闭窗口内结束, pid: {}", process->processId());
                return false;
            }
        }
    }

    process_ = nullptr;
    clearCurrent();
    delete process;
    return true;
}

void TensorBoardRunner::clearCurrent()
{
    model_uuid_.clear();
    port_ = 0;
}

} // namespace dltool::model
