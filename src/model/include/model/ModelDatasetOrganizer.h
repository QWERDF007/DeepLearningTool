#pragma once

#include "dltool/model/Export.h"
#include "model/ModelDatasetSelection.h"
#include "model/ModelTaskTypes.h"

#include <QString>
#include <QVariantMap>

namespace dltool::data {
class DatasetExportSource;
}

namespace dltool::model {

/**
 * @brief 模型数据集导出请求，包含模型侧的组织规则和 data 模块提供的数据源。
 */
struct MODEL_API ModelDatasetExportRequest
{
    int                    method{-1};                        ///< 深度学习方法
    QString                framework_name;                    ///< 框架名称
    QString                model_architecture;                ///< 模型架构
    QString                model_uuid;                        ///< 模型 UUID
    ModelTaskType          task_type{ModelTaskType::Unknown}; ///< 任务类型
    QString                dataset_dir;                       ///< 数据集输出目录
    QString                test_file_list_path;                ///< 测试任务专属文件列表路径
    ModelDatasetSelections selections;                        ///< 数据集选择

    const dltool::data::DatasetExportSource *source{nullptr};
};

/**
 * @brief 将选中的数据集按框架规则写成训练或推理所需的文件。
 */
class MODEL_API ModelDatasetOrganizer
{
public:
    /**
     * @brief 执行数据集组织导出
     * @param request 导出请求
     * @param err_msg 错误信息输出
     * @return 数据集配置键值对
     */
    static QVariantMap organize(const ModelDatasetExportRequest &request, QString *err_msg = nullptr);
};

} // namespace dltool::model
