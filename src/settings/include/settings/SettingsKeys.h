#pragma once

/**
 * @file SettingsKeys.h
 * @brief 设置访问器、字段和侧边栏枚举键声明。
 */

#include "dltool/settings/Export.h"

#include <QString>
#include <QtQml>

/**
 * @namespace dltool::settings::accessor
 * @brief 设置分组访问器枚举命名空间。
 */
namespace dltool::settings::accessor {
Q_NAMESPACE_EXPORT(SETTINGS_API)

/**
 * @brief 设置分组访问器键。
 */
enum class Key
{
    Software = 0,    ///< 软件设置。
    Data,            ///< 数据设置。
    Ui,              ///< 界面设置。
    ImageSearch,     ///< 图像搜索设置。
    RoiSearch,       ///< ROI 搜索设置。
    SmartAnnotation, ///< 智能标注设置。
    FewShotLearning, ///< 小样本学习设置。
};
Q_ENUM_NS(Key)
QML_NAMED_ELEMENT(SettingsAccessor)
} // namespace dltool::settings::accessor

/**
 * @namespace dltool::settings::field
 * @brief 设置字段枚举命名空间。
 */
namespace dltool::settings::field {
Q_NAMESPACE_EXPORT(SETTINGS_API)

/**
 * @brief 设置字段键。
 */
enum class Key
{
    Model = 0,         ///< 模型名称或模型类型字段。
    FeatureName,       ///< 特征名称字段。
    MaxRecentProjects, ///< 最大最近项目数。
    AutoSaveInterval,  ///< 自动保存间隔。
    AutoSaveEnabled,   ///< 自动保存开关。
    PythonEnvPath,     ///< Python 环境路径。
    Sam2Checkpoint,    ///< SAM2 检查点路径。
    Sam2Architecture,  ///< SAM2 网络结构。
    KShot,             ///< K-shot 数量。
    Epochs,            ///< 训练轮数。
    BatchSize,         ///< 批大小。
    NumWorkers,        ///< 数据加载线程数。
    ImageSize,         ///< 图像尺寸。
    LearningRate,      ///< 学习率。
    WeightDecay,       ///< 权重衰减。
    SupportRatio,      ///< 支持集比例。
    OutputDir,         ///< 输出目录。
};
Q_ENUM_NS(Key)
QML_NAMED_ELEMENT(SettingsFieldKey)
} // namespace dltool::settings::field

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
 * @brief 获取访问器枚举对应的路径字符串。
 * @param key 访问器枚举键。
 * @return 访问器路径字符串；未知键返回空字符串。
 */
SETTINGS_API QString accessorPath(accessor::Key key);

/**
 * @brief 获取访问器整数键对应的路径字符串。
 * @param key 访问器整数键。
 * @return 访问器路径字符串；未知键返回空字符串。
 */
SETTINGS_API QString accessorPath(int key);

/**
 * @brief 获取字段枚举对应的字段名。
 * @param key 字段枚举键。
 * @return 字段名字符串；未知键返回空字符串。
 */
SETTINGS_API QString fieldName(field::Key key);

/**
 * @brief 获取字段整数键对应的字段名。
 * @param key 字段整数键。
 * @return 字段名字符串；未知键返回空字符串。
 */
SETTINGS_API QString fieldName(int key);

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
