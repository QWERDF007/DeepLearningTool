#pragma once

/**
 * @file SettingsKeys.h
 * @brief 生成设置键入口和侧边栏枚举声明。
 */

#include "dltool/settings/Export.h"
#include "dltool/settings/SettingsKeys.hpp"

#include <QString>
#include <QtQml>
#include <string_view>

/**
 * @namespace dltool::settings::sidebar
 * @brief 侧边栏场景枚举命名空间。
 */
namespace dltool::settings::sidebar {
Q_NAMESPACE_EXPORT(SETTINGS_API)

/**
 * @brief 设置侧边栏键。
 */
enum class Key
{
    Gallery = 0, ///< 图库页面侧边栏。
    Review,      ///< 复核页面侧边栏。
};
Q_ENUM_NS(Key)
QML_NAMED_ELEMENT(SettingsSidebar)
} // namespace dltool::settings::sidebar

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
 * @brief 获取侧边栏枚举对应的名称。
 * @param key 侧边栏枚举键。
 * @return 侧边栏名称；未知键返回空字符串。
 */
SETTINGS_API QString sidebarName(sidebar::Key key);

/**
 * @brief 获取侧边栏整数键对应的名称。
 * @param key 侧边栏整数键。
 * @return 侧边栏名称；未知键返回空字符串。
 */
SETTINGS_API QString sidebarName(int key);

} // namespace dltool::settings
