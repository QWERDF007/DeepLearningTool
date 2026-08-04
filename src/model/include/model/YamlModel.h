#pragma once

#include "model/IModel.h"

#include <memory>

namespace dltool::model {

/**
 * @brief 根据 YAML 配置创建模型实例
 * @param method 深度学习方法枚举值
 * @param framework_name 框架名称
 * @param model_architecture 模型架构名称
 * @return 创建的模型实例，参数无效时返回 nullptr
 */
std::unique_ptr<IModel> createYamlModel(int method, const QString &framework_name, const QString &model_architecture);

} // namespace dltool::model
