#pragma once

#include "dltool/model/Export.h"

#include <QObject>
#include <QProcessEnvironment>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QtGlobal>

class QProcess;

namespace dltool::model {

/**
 * @brief TensorBoard 外部进程的完整启动规格。
 */
struct MODEL_API TensorBoardLaunchSpec
{
    QString             model_uuid;
    QString             program;
    QStringList         arguments;
    QProcessEnvironment environment;
    quint16             port{0};
};

/**
 * @brief 项目作用域内 TensorBoard 进程的深生命周期模块。
 *
 * 该模块只负责外部进程的启动、切换、停止和关闭，不负责模型记录、
 * Python 环境解析或 QML URL 投影。
 */
class MODEL_API TensorBoardRunner final : public QObject
{
    Q_OBJECT

public:
    explicit TensorBoardRunner(QObject *parent = nullptr);
    ~TensorBoardRunner() override;

    /**
     * @brief 获取可供本地 TensorBoard 使用的端口。
     * @return 端口不可用时返回 0。
     */
    static quint16 availableLocalPort();

    /**
     * @brief 启动或切换 TensorBoard 进程。
     * @param spec 启动规格。
     * @param err_msg 启动失败时写入错误。
     * @return 已提交启动返回 true。
     */
    bool start(const TensorBoardLaunchSpec &spec, QString *err_msg = nullptr);

    /**
     * @brief 停止进程并关闭运行器。
     *
     * 关闭后拒绝新的启动请求；调用幂等。
     */
    void shutdown();

    bool    isRunning() const;
    QString modelUuid() const;
    quint16 port() const;

private:
    bool stopCurrent();
    void clearCurrent();

    QPointer<QProcess> process_;
    QString            model_uuid_;
    quint16            port_{0};
    bool               shutting_down_{false};
};

} // namespace dltool::model
