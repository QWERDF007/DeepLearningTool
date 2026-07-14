#pragma once

#include "dltool/model/Export.h"
#include "model/ModelParamDefs.h"

#include <QString>
#include <vector>

namespace dltool::model {

/**
 * @brief 模型参数模式，描述一个模型框架下某模型架构的训练/测试参数结构与元信息
 */
struct MODEL_API ModelParamsSchema
{
    QString                           framework_name;     ///< 框架名称
    QString                           model_architecture; ///< 模型架构名称
    QString                           model_name;         ///< 模型名称
    QString                           method;             ///< 方法
    QString                           config_path;        ///< 配置文件路径
    std::vector<ParamGroupDefinition> train_groups;       ///< 训练参数分组列表
    std::vector<ParamGroupDefinition> test_groups;        ///< 测试参数分组列表
};

/**
 * @brief 加载指定框架和模型架构的参数模式
 * @param framework_name 框架名称
 * @param model_architecture 模型架构名称
 * @return 解析后的模式，文件未找到或解析失败时返回空模式
 */
MODEL_API ModelParamsSchema loadModelParamsSchema(const QString &framework_name, const QString &model_architecture);

} // namespace dltool::model
