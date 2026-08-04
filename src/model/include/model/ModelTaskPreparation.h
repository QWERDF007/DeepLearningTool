#pragma once

#include "dltool/model/Export.h"
#include "model/ExternalProcessSpec.h"
#include "model/ModelDatasetSelection.h"
#include "model/ModelRegistry.h"
#include "model/ModelTaskTypes.h"
#include "model/ModelEvaluationProtocol.h"

#include <QString>
#include <QtGlobal>
#include <QVariantMap>

namespace dltool::data {
class DatasetExportSource;
}

namespace dltool::model {

/**
 * @brief 后台准备模型任务所需的值类型配置。
 */
struct MODEL_API ModelTaskConfigInput
{
    QString              model_uuid;
    QString              model_name;
    QString              framework_name;
    QString              method;
    QString              model_architecture;
    QVariantMap          train_params;
    QVariantMap          test_params;
    QString               scope_uuid;
    QString               scope_name;
    QString               task_directory;
    ModelDatasetSelection test_dataset_selection;
    qint64                created_at{0};
    qint64                modified_at{0};
};

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
    evaluation::Method evaluation_method{evaluation::Method::Unknown}; ///< 评估协议方法。
    FrameworkDefinition framework;                    ///< 框架脚本和环境定义。
    QString             task_server_host;             ///< Python 连接的任务 TCP 主机地址。
    quint16             task_server_port{0};          ///< Python 连接的任务 TCP 端口。
    QString             project_database_path;        ///< 项目数据库路径。
    ModelDatasetSelections selections;                ///< 当前模型的数据集选择。
    ModelTaskConfigInput   model_config;              ///< 当前模型名称、架构和参数。
};

/**
 * @brief 在后台生成数据集文件列表、更新数据库并创建 Python 进程规格。
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
