#pragma once

/**
 * @file SettingsKeys.h
 * @brief 生成设置键入口声明。
 */

#include "dltool/settings/Export.h"
#include "dltool/settings/SettingsKeys.hpp"

#include <QString>
#include <optional>
#include <string_view>

namespace dltool::settings {

/**
 * @brief 将生成设置键中的字符串视图转换为 QString。
 * @param value 生成键 API 返回的 UTF-8 字符串视图。
 * @return QString 字符串。
 */
SETTINGS_API QString toQString(std::string_view value);

/**
 * @brief 获取生成访问器内字段枚举对应的字段名。
 * @param accessor_key 生成访问器键。
 * @param field_key 对应访问器的生成字段枚举整数值。
 * @return 字段英文键名；未知键返回空字符串。
 */
SETTINGS_API QString fieldName(generated::AccessorKey accessor_key, int field_key);

/**
 * @brief 根据访问器路径查找对应的生成访问器枚举键。
 * @param path 访问器路径，例如 "data" 或 "advanced.smartAnnotation"。
 * @return 匹配成功返回 AccessorKey，否则返回 std::nullopt。
 */
SETTINGS_API std::optional<generated::AccessorKey> accessorKeyForPath(const QString &path);

/**
 * @brief 根据分组键查找对应的生成访问器枚举键。
 * @param group_key 分组键，例如 "DataSettings" 或 "SmartAnnotationSettings"。
 * @return 匹配成功返回 AccessorKey，否则返回 std::nullopt。
 */
SETTINGS_API std::optional<generated::AccessorKey> accessorKeyForGroupKey(const QString &group_key);

} // namespace dltool::settings
