#pragma once

#include "dltool/model/Export.h"
#include "model/ExternalProcessSpec.h"
#include "model/ModelDatasetSelection.h"
#include "model/ModelEvaluationProtocol.h"
#include "model/ModelRegistry.h"
#include "model/ModelTaskTypes.h"

#include <QString>
#include <QVariantMap>
#include <QtGlobal>

namespace dltool::data {
class DatasetExportSource;
}

namespace dltool::model {

/**
 * @brief 后台准备模型任务所需的值类型配置结构体。
 *
 * 封装模型元信息、超参数字典及数据集选择，供后台准备线程使用。
 */
struct MODEL_API ModelTaskConfigInput
{
    QString               model_uuid;             ///< 模型 UUID。
    QString               model_name;             ///< 模型名称。
    QString               framework_name;         ///< 算法框架名称。
    QString               method;                 ///< 任务方法类型字符串。
    QString               model_architecture;     ///< 选用的模型网络架构。
    QVariantMap           train_params;           ///< 训练参数字典。
    QVariantMap           test_params;            ///< 测试参数字典。
    QString               scope_uuid;             ///< 任务作用域 UUID（测试任务 UUID 或 "train"）。
    QString               scope_name;             ///< 作用域显示名称。
    QString               task_directory;         ///< 任务相对工作目录。
    ModelDatasetSelection test_dataset_selection; ///< 测试数据集勾选配置。
    qint64                created_at{0};          ///< 创建时间戳。
    qint64                modified_at{0};         ///< 修改时间戳。
};

/**
 * @brief GUI 线程提交给后台任务准备管线的启动请求结构体。
 *
 * 只保存后台准备必需的纯值；IModel 实例、任务表和 DataManager 都不跨线程传递。
 */
struct MODEL_API ModelTaskRequest
{
    int                    task_id{-1};                       ///< 任务在 TaskManager 中的整数 ID。
    ModelTaskType          task_type{ModelTaskType::Unknown}; ///< 模型任务类型（Train, Test, BoxToMask 等）。
    QString                scope_uuid;                        ///< 测试任务 UUID；训练任务为 "train"。
    QString                scope_name;                        ///< 任务显示名称。
    evaluation::Method     evaluation_method{evaluation::Method::Unknown}; ///< 评估协议对应的方法。
    FrameworkDefinition    framework;                                      ///< 框架 Python 脚本入口与虚拟环境定义。
    QString                task_server_host;      ///< Python 进程回连的任务 TCP 服务端主机地址。
    quint16                task_server_port{0};   ///< Python 进程回连的任务 TCP 服务端端口号。
    QString                project_database_path; ///< 项目数据库路径。
    ModelDatasetSelections selections;            ///< 数据集选择配置（训练集/验证集/测试集）。
    ModelTaskConfigInput   model_config;          ///< 模型配置快照。
};

/**
 * @brief 在后台准备模型任务（导出数据集清单、生成配置文件、准备输出目录并构造进程启动规格）。
 * @param method 深度学习方法。
 * @param project_dir 项目根目录。
 * @param request GUI 线程构造的纯值启动请求。
 * @param dataset_source 后台数据导出源；不需要导出数据集时可为 nullptr。
 * @param process_spec 输出的外部 Python 进程启动参数规格。
 * @param err_msg 失败时输出错误信息，可为 nullptr。
 * @return 准备成功返回 true。
 */
MODEL_API bool prepareModelTask(int method, const QString &project_dir, const ModelTaskRequest &request,
                                const dltool::data::DatasetExportSource *dataset_source,
                                ExternalProcessSpec &process_spec, QString *err_msg = nullptr);

} // namespace dltool::model
