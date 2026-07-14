#pragma once

#include "dltool/model/Export.h"
#include "model/ExternalProcessSpec.h"
#include "model/ModelRegistry.h"
#include "model/ModelTaskTypes.h"

#include <QString>
#include <QtGlobal>

namespace dltool::data {
class DataManager;
} // namespace dltool::data

namespace dltool::model {

class IModel;

/**
 * @brief 模型任务上下文，聚合启动外部任务所需的全部信息
 */
struct MODEL_API ModelTaskContext
{
    int                 task_id{-1};                       ///< 任务 ID
    QString             model_uuid;                        ///< 模型 UUID
    QString             model_name;                        ///< 模型名称
    ModelTaskType       task_type{ModelTaskType::Unknown}; ///< 任务类型
    IModel             *model{nullptr};                    ///< 模型实例
    FrameworkDefinition framework;                         ///< 框架定义
    QString             task_server_host;                  ///< 任务通信服务主机
    quint16             task_server_port{0};               ///< 任务通信服务端口

    /**
     * @brief 检查上下文是否有效
     * @return 有效返回 true
     */
    bool isValid() const
    {
        return task_id >= 0 && !model_uuid.trimmed().isEmpty() && !model_name.trimmed().isEmpty()
            && isKnownModelTask(task_type) && model != nullptr;
    }
};

/**
 * @brief 模型任务准备服务，负责导出数据集、生成配置、构建进程启动参数
 */
class MODEL_API ModelTaskPreparationService
{
public:
    /**
     * @brief 构造任务准备服务
     * @param method 深度学习方法
     * @param project_dir 项目目录
     * @param data_manager 数据管理器
     */
    ModelTaskPreparationService(int method, QString project_dir, dltool::data::DataManager *data_manager);

    /**
     * @brief 准备外部任务启动所需的全部资源
     * @param context 任务上下文
     * @param process_spec 输出：进程启动规格
     * @param err_msg 错误信息输出
     * @return 准备成功返回 true
     */
    bool prepare(const ModelTaskContext &context, ExternalProcessSpec &process_spec, QString *err_msg = nullptr) const;

private:
    int                        method_{-1};            ///< 深度学习方法
    QString                    project_dir_;           ///< 项目目录
    dltool::data::DataManager *data_manager_{nullptr}; ///< 数据管理器
};

} // namespace dltool::model
