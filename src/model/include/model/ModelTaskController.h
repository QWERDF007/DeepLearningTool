#pragma once

#include "dltool/model/Export.h"
#include "model/ModelTaskPreparationService.h"
#include "model/ModelTaskTypes.h"
#include "model/TaskCommunication.h"

#include <QMetaObject>
#include <QObject>
#include <QString>
#include <QtQml>
#include <memory>
#include <unordered_set>

namespace dltool::data {
class DataManager;
} // namespace dltool::data

namespace dltool::model {

class ExternalModelTaskRunner;
class IModel;
class ModelManager;
class TaskManager;

/**
 * @brief 模型任务控制器，管理模型训练/测试任务的完整生命周期（添加、启动、停止、删除）
 */
class MODEL_API ModelTaskController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ModelTaskController)
    QML_UNCREATABLE("ModelTaskController is owned by Project.")

public:
    /**
     * @brief 构造模型任务控制器
     * @param method 深度学习方法
     * @param project_dir 项目目录
     * @param model_manager 模型管理器
     * @param data_manager 数据管理器
     * @param task_manager 任务管理器
     * @param parent 父对象
     */
    ModelTaskController(int method, QString project_dir, ModelManager *model_manager,
                        dltool::data::DataManager *data_manager, TaskManager *task_manager, QObject *parent = nullptr);
    ~ModelTaskController() override;

    /**
     * @brief 添加模型任务
     * @param model_uuid 模型 UUID
     * @param task_type 任务类型
     * @return 任务 ID，失败返回 -1
     */
    Q_INVOKABLE int addModelTask(const QString &model_uuid, ModelTaskTypes::Type task_type);

    /**
     * @brief 启动模型任务
     * @param model_uuid 模型 UUID
     * @param task_type 任务类型
     * @return 任务 ID，失败返回 -1
     */
    Q_INVOKABLE int startModelTask(const QString &model_uuid, ModelTaskTypes::Type task_type);

    /**
     * @brief 停止模型任务
     * @param model_uuid 模型 UUID
     * @param task_type 任务类型
     * @return 操作成功返回 true
     */
    Q_INVOKABLE bool stopModelTask(const QString &model_uuid, ModelTaskTypes::Type task_type);

    /**
     * @brief 删除模型任务
     * @param model_uuid 模型 UUID
     * @param task_type 任务类型
     * @return 操作成功返回 true
     */
    Q_INVOKABLE bool deleteModelTask(const QString &model_uuid, ModelTaskTypes::Type task_type);

private:
    /**
     * @brief 构建任务上下文
     * @param model_uuid 模型 UUID
     * @param task_type 任务类型
     * @param task_id 任务 ID
     * @param context 输出：任务上下文
     * @param err_msg 错误信息输出
     * @return 构建成功返回 true
     */
    bool buildContext(const QString &model_uuid, ModelTaskType task_type, int task_id, ModelTaskContext &context,
                      QString *err_msg = nullptr) const;

    /**
     * @brief 确保任务记录存在
     * @param context 任务上下文
     * @return 任务 ID
     */
    int ensureTaskRecord(const ModelTaskContext &context);

    /**
     * @brief 启动任务
     * @param task_id 任务 ID
     * @return 启动成功返回 true
     */
    bool startTask(int task_id);

    /**
     * @brief 停止任务
     * @param task_id 任务 ID
     * @return 停止成功返回 true
     */
    bool stopTask(int task_id);

    /**
     * @brief 删除任务
     * @param task_id 任务 ID
     * @return 删除成功返回 true
     */
    bool deleteTask(int task_id);

    /**
     * @brief 启动外部任务进程
     * @param context 任务上下文
     * @return 启动成功返回 true
     */
    bool startExternalTask(const ModelTaskContext &context);

    /**
     * @brief 检查任务是否属于当前模型管理器
     * @param task_id 任务 ID
     * @return 属于返回 true
     */
    bool taskBelongsToCurrentModelManager(int task_id) const;

    /**
     * @brief 标记任务失败
     * @param task_id 任务 ID
     * @param message 失败信息
     */
    void failTask(int task_id, const QString &message) const;

    /**
     * @brief 更新任务对应模型的修改时间
     * @param task_id 任务 ID
     */
    void touchTaskModelModifiedTime(int task_id) const;

private slots:
    /**
     * @brief 处理外部任务状态消息并保存模型状态
     * @param message 任务消息
     */
    void handleTaskMessage(const dltool::model::TaskMessage &message);

    /**
     * @brief 处理任务停止请求
     * @param task_id 任务 ID
     */
    void handleTaskStopRequested(int task_id);

    /**
     * @brief 处理外部任务完成事件
     * @param task_id 任务 ID
     * @param exit_code 退出码
     * @param normal_exit 是否正常退出
     * @param stop_requested 是否由停止请求触发
     */
    void handleExternalTaskFinished(int task_id, int exit_code, bool normal_exit, bool stop_requested);

private:
    int method_{-1}; ///< 深度学习方法

    QString project_dir_; ///< 项目目录

    ModelManager              *model_manager_{nullptr}; ///< 模型管理器
    dltool::data::DataManager *data_manager_{nullptr};  ///< 数据管理器
    TaskManager               *task_manager_{nullptr};  ///< 任务管理器

    std::unique_ptr<ExternalModelTaskRunner> external_task_runner_; ///< 外部进程运行器

    std::unordered_set<int> owned_task_ids_; ///< 本控制器管理的任务 ID 集合
};

} // namespace dltool::model
