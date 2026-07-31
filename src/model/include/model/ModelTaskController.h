#pragma once

#include "dltool/model/Export.h"
#include "model/ModelTaskPreparation.h"
#include "model/ModelTaskTypes.h"
#include "model/TaskCommunication.h"

#include <QObject>
#include <QString>
#include <QtQml>
#include <memory>

namespace dltool::data {
class DataManager;
}

namespace dltool::model {

class ExternalModelTaskRunner;
class ModelManager;
class TaskManager;

/**
 * @brief 模型任务的唯一执行入口。
 *
 * 它负责把 UI/任务中心的开始请求串成一条链：创建任务记录、后台准备、启动 Python、
 * 接收任务事件并刷新模型结果。TaskManager 只保存任务状态，不启动模型进程；数据导出和
 * 配置写入始终在后台线程完成。
 */
class MODEL_API ModelTaskController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ModelTaskController)
    QML_UNCREATABLE("ModelTaskController is owned by Project.")

public:
    /**
     * @brief 构造模型任务控制器。
     * @param method 深度学习方法。
     * @param project_dir 项目目录。
     * @param model_manager 模型管理器。
     * @param data_manager 数据管理器。
     * @param task_manager 任务管理器。
     * @param parent 父对象。
     */
    ModelTaskController(int method, QString project_dir, ModelManager *model_manager,
                        dltool::data::DataManager *data_manager, TaskManager *task_manager, QObject *parent = nullptr);

    /**
     * @brief 析构模型任务控制器。
     */
    ~ModelTaskController() override;

    /**
     * @brief 为指定模型创建或复用 Pending 任务记录。
     * @param model_uuid 模型 UUID。
     * @param task_type 模型任务类型。
     * @return 任务 ID；失败时返回 -1。
     */
    Q_INVOKABLE int  addModelTask(const QString &model_uuid, ModelTaskTypes::Type task_type);

    /**
     * @brief 启动指定模型任务。
     *
     * 该函数只负责创建/复用任务记录并委托 TaskManager 进入 Preparing；随后由
     * taskStartRequested 驱动后台准备和 Python 启动，避免模型页面与任务中心走不同流程。
     * @param model_uuid 模型 UUID。
     * @param task_type 模型任务类型。
     * @return 任务 ID；无法提交启动请求时返回 -1。
     */
    Q_INVOKABLE int  startModelTask(const QString &model_uuid, ModelTaskTypes::Type task_type);

    /**
     * @brief 停止指定模型任务。
     * @param model_uuid 模型 UUID。
     * @param task_type 模型任务类型。
     * @return 成功提交停止请求返回 true。
     */
    Q_INVOKABLE bool stopModelTask(const QString &model_uuid, ModelTaskTypes::Type task_type);

    /**
     * @brief 删除指定模型任务记录。
     * @param model_uuid 模型 UUID。
     * @param task_type 模型任务类型。
     * @return 删除成功返回 true。
     */
    Q_INVOKABLE bool deleteModelTask(const QString &model_uuid, ModelTaskTypes::Type task_type);

private:
    /**
     * @brief 创建或查找模型任务记录。
     * @param model_uuid 模型 UUID。
     * @param task_type 模型任务类型。
     * @param err_msg 失败时输出错误信息，可为 nullptr。
     * @return 任务 ID；失败时返回 -1。
     */
    int  ensureTaskRecord(const QString &model_uuid, ModelTaskType task_type, QString *err_msg = nullptr);

    /**
     * @brief 提交处于 Preparing 状态的任务到后台准备流程。
     * @param task_id 任务 ID。
     * @return 成功提交后台工作返回 true。
     */
    bool prepareTask(int task_id);

    /**
     * @brief 请求停止任务。
     * @param task_id 任务 ID。
     * @return 成功提交停止请求返回 true。
     */
    bool stopTask(int task_id);

    /**
     * @brief 删除任务记录及其关联的外部进程。
     * @param task_id 任务 ID。
     * @return 删除成功返回 true。
     */
    bool deleteTask(int task_id);

    /**
     * @brief 在 GUI 线程读取模型当前值，构造唯一的后台启动输入。
     * @param task_id 任务 ID。
     * @param request 输出的纯值后台请求。
     * @param err_msg 失败时输出错误信息，可为 nullptr。
     * @return 构造成功返回 true。
     */
    bool buildTaskRequest(int task_id, ModelTaskRequest &request, QString *err_msg = nullptr) const;

    /**
     * @brief 处理后台准备完成事件，并启动外部 Python 进程。
     * @param task_id 任务 ID。
     * @param process_spec 后台生成的进程启动规格。
     * @param success 后台准备是否成功。
     * @param error 后台准备失败信息。
     */
    void handlePreparedTask(int task_id, const std::shared_ptr<ExternalProcessSpec> &process_spec, bool success,
                            const QString &error);

    /**
     * @brief 判断任务是否属于当前项目的模型管理器。
     * @param task_id 任务 ID。
     * @return 属于当前项目返回 true。
     */
    bool taskBelongsToCurrentModelManager(int task_id) const;

    /**
     * @brief 标记任务失败并向界面报告错误。
     * @param task_id 任务 ID。
     * @param message 错误信息。
     */
    void failTask(int task_id, const QString &message) const;

    /**
     * @brief 更新任务所属模型的修改时间。
     * @param task_id 任务 ID。
     */
    void touchTaskModelModifiedTime(int task_id) const;

    /**
     * @brief 将任务中心的总体进度和终态同步到模型页面使用的任务数据。
     * @param task_id 任务 ID。
     */
    void syncTaskModelState(int task_id) const;

private slots:
    /**
     * @brief 响应 TaskManager 的开始请求，提交完整后台准备流程。
     * @param task_id 任务 ID。
     */
    void handleTaskStartRequested(int task_id);

    /**
     * @brief 根据已被 TaskManager 接受的 Python 事件刷新模型结果数据。
     * @param message Python 上报的任务事件。
     */
    void handleTaskMessage(const dltool::model::TaskMessage &message);

    /**
     * @brief 响应停止请求，停止 Python 进程或收敛后台准备。
     * @param task_id 任务 ID。
     */
    void handleTaskStopRequested(int task_id);

    /**
     * @brief Python 进程实际启动后，将任务置为 Running。
     * @param task_id 任务 ID。
     */
    void handleExternalTaskStarted(int task_id);

    /**
     * @brief 处理 Python 进程启动失败。
     * @param task_id 任务 ID。
     * @param error 错误信息。
     */
    void handleExternalTaskStartFailed(int task_id, const QString &error);

    /**
     * @brief 根据 Python 进程退出结果收敛任务状态。
     * @param task_id 任务 ID。
     * @param exit_code 进程退出码。
     * @param normal_exit 是否正常退出。
     * @param stop_requested 是否由用户请求停止。
     */
    void handleExternalTaskFinished(int task_id, int exit_code, bool normal_exit, bool stop_requested);

private:
    int     method_{-1};       ///< 当前项目的深度学习方法。
    QString project_dir_;      ///< 当前项目目录。

    ModelManager              *model_manager_{nullptr}; ///< 当前项目模型管理器。
    dltool::data::DataManager *data_manager_{nullptr};  ///< 当前项目数据管理器。
    TaskManager               *task_manager_{nullptr};  ///< 应用级任务状态中心。
    std::unique_ptr<ExternalModelTaskRunner> external_task_runner_; ///< 当前项目 Python 进程运行器。
};

} // namespace dltool::model
