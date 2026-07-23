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
 * @brief 模型任务进入后台前冻结的启动输入。
 *
 * 只保存模型配置、任务通信端点和数据集选择等轻量值；数据由 data 模块在
 * 工作线程内提供。
 */
struct MODEL_API ModelTaskPreparationRequest
{
    int           task_id{-1};                       ///< 任务 ID
    ModelTaskType task_type{ModelTaskType::Unknown}; ///< 任务类型

    FrameworkDefinition framework;           ///< 框架定义
    QString             task_server_host;    ///< 任务通信服务主机
    quint16             task_server_port{0}; ///< 任务通信服务端口

    ModelDatasetSelections selections;
    ModelTaskConfigInput   model_config;
};

/**
 * @brief 负责生成模型目录内容、任务配置和外部进程规格。
 */
class MODEL_API ModelTaskPreparationService
{
public:
    ModelTaskPreparationService(int method, QString project_dir);

    /**
     * @brief 在工作线程准备外部模型任务。
     * @param request 已冻结的启动输入
     * @param process_spec 输出的进程启动规格
     */
    bool prepare(const ModelTaskPreparationRequest &request, const dltool::data::DatasetExportSource *dataset_source,
                 ExternalProcessSpec &process_spec, QString *err_msg = nullptr) const;

private:
    int     method_{-1};
    QString project_dir_;
};

} // namespace dltool::model
