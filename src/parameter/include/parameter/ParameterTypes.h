#pragma once

#include "dltool/parameter/Export.h"

#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

namespace dltool::parameter {

/** 参数选项生成方式。 */
enum class ParameterKind
{
    Static,
    Dynamic,
};

/** 动态选项的显示值和实际值。 */
struct PARAMETER_API ParameterOption
{
    QString  display_value; ///< 界面显示值。
    QVariant actual_value;  ///< 后端实际值。
};

/**
 * @brief 供 settings、model 和 feature 共用的参数定义。
 * @details 只保存参数公共元数据，不包含分组、持久化或模型任务语义。
 */
struct PARAMETER_API ParameterSpec
{
    QString       name_en;                              ///< 英文名称或字段键。
    QString       name_cn;                              ///< 中文显示名称。
    QString       description;                          ///< 说明文本。
    QVariant      value;                                ///< 当前值。
    QVariant      default_value;                        ///< 默认值。
    QString       value_type{QStringLiteral("string")}; ///< 值类型（int/double/bool/string）。
    QVariantList  value_range;                          ///< 值域范围。
    QString       display_type{QStringLiteral("text")}; ///< 展示类型。
    QString       backend_key;                          ///< 动态 provider key。
    QVariantList  options;                              ///< 选项显示值列表。
    QVariantMap   options_value_map;                    ///< 显示值到实际值的映射。
    ParameterKind kind{ParameterKind::Static};          ///< 参数选项生成方式。
    bool          enabled{true};                        ///< 是否启用。
    QString       unit;                                 ///< 单位。
};

} // namespace dltool::parameter
