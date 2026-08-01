#pragma once

#include "dltool/model/Export.h"
#include "model/ExternalProcessSpec.h"
#include "model/ModelDatasetSelection.h"
#include "model/ModelRegistry.h"
#include "model/ModelTaskConfigService.h"
#include "model/ModelTaskTypes.h"

#include <QString>
#include <QtGlobal>

namespace dltool::data {
class DatasetExportSource;
}

namespace dltool::model {

/**
 * @brief GUI 线程交给后台任务的唯一启动输入。
 *
 * 只保存后台准备必需的值；IModel、任务表和 DataManager 都不跨线程传递。
 */
struct MODEL_API ModelTaskRequest
{
    int           task_id{-1};                       ///< 任务 ID。
    ModelTaskType task_type{ModelTaskType::Unknown};  ///< 模型任务类型。
    QString       scope_uuid;                        ///< 测试任务 UUID；训练为 train。
    QString       scope_name;                        ///< 测试任务显示名称。
    QString       evaluation_method;                 ///< 规范化评估方法名，例如 object_detection。
    FrameworkDefinition framework;                    ///< 框架脚本和环境定义。
    QString             task_server_host;             ///< Python 连接的任务 TCP 主机地址。
    quint16             task_server_port{0};          ///< Python 连接的任务 TCP 端口。
    ModelDatasetSelections selections;                ///< 当前模型的数据集选择。
    ModelTaskConfigInput   model_config;              ///< 当前模型名称、架构和参数。
    bool reuse_prediction{false};                     ///< 测试仅重新评估时保留当前 pred/。
    QString inference_digest;                         ///< 当前推理输入摘要。
    QString input_data_digest;                        ///< 当前测试图像 ID、路径、大小和 mtime 摘要。
};

/**
 * @brief 在后台生成数据集、配置文件和 Python 进程规格。
 * @param method 深度学习方法。
 * @param project_dir 项目目录。
 * @param request GUI 线程构造的纯值启动输入。
 * @param dataset_source 后台数据导出源；不需要导出数据集时可为 nullptr。
 * @param process_spec 输出的 Python 进程启动规格。
 * @param err_msg 失败时输出错误信息，可为 nullptr。
 * @return 准备成功返回 true。
 */
MODEL_API bool prepareModelTask(int method, const QString &project_dir, const ModelTaskRequest &request,
                                const dltool::data::DatasetExportSource *dataset_source,
                                ExternalProcessSpec &process_spec, QString *err_msg = nullptr);

} // namespace dltool::model
