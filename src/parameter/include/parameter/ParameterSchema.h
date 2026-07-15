#pragma once

#include "parameter/ParameterTypes.h"

#include <yaml-cpp/yaml.h>

#include <QVector>

namespace dltool::parameter {

/** 从 YAML 节点解析公共参数字段。 */
PARAMETER_API ParameterSpec parseParameterSpec(const YAML::Node &node);

/** 解析参数的动态选项，并更新显示值、实际值映射和默认值。 */
PARAMETER_API bool resolveParameterOptions(ParameterSpec &parameter, const QVariantMap &context = {});

/** 根据参数类型和选项规则规范化输入值。 */
PARAMETER_API QVariant normalizeParameterValue(const ParameterSpec &parameter, const QVariant &value,
                                               const QVariantMap &context = {});

/** 将 provider 返回的选项转换为 QML 兼容的显示值列表。 */
PARAMETER_API QVariantList parameterOptionLabels(const QVector<ParameterOption> &options);

/** 将 provider 返回的选项转换为显示值到实际值的映射。 */
PARAMETER_API QVariantMap parameterOptionValueMap(const QVector<ParameterOption> &options);

/** 根据已有的显示值和映射构造选项列表。 */
PARAMETER_API QVector<ParameterOption> parameterOptions(const QVariantList &labels, const QVariantMap &value_map);

} // namespace dltool::parameter
