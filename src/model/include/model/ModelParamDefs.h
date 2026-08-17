#pragma once

#include "model/IParams.h"

#include <QString>
#include <QVariant>
#include <QVariantList>
#include <vector>

namespace dltool::model {

/**
 * @brief 参数组定义，描述一组参数的元信息
 */
struct ParamGroupDefinition
{
    QString                      name_en;       ///< 英文名称
    QString                      name_cn;       ///< 中文名称
    QString                      description;   ///< 描述
    bool                         enabled{true}; ///< 是否启用
    int                          part_index{0}; ///< 分组索引
    std::vector<ParamDefinition> params;        ///< 参数定义列表
};

/**
 * @brief 创建整数类型参数
 * @param name_en 英文名称
 * @param name_cn 中文名称
 * @param default_value 默认值
 * @param from 最小值
 * @param to 最大值
 * @param step 步长
 * @param description 描述
 * @return 参数定义
 */
MODEL_API ParamDefinition makeIntegerParam(const QString &name_en, const QString &name_cn, int default_value,
                                           int from, int to, int step, const QString &description = {});

/**
 * @brief 创建浮点类型参数
 * @param name_en 英文名称
 * @param name_cn 中文名称
 * @param default_value 默认值
 * @param from 最小值
 * @param to 最大值
 * @param step 步长
 * @param description 描述
 * @return 参数定义
 */
MODEL_API ParamDefinition makeDoubleParam(const QString &name_en, const QString &name_cn, double default_value,
                                          double from, double to, double step, const QString &description = {});

/**
 * @brief 创建滑块类型参数
 * @param name_en 英文名称
 * @param name_cn 中文名称
 * @param default_value 默认值
 * @param from 最小值
 * @param to 最大值
 * @param step 步长
 * @param description 描述
 * @return 参数定义
 */
MODEL_API ParamDefinition makeSliderParam(const QString &name_en, const QString &name_cn, double default_value,
                                          double from, double to, double step, const QString &description = {});

/**
 * @brief 创建复选框类型参数
 * @param name_en 英文名称
 * @param name_cn 中文名称
 * @param default_value 默认值
 * @param description 描述
 * @return 参数定义
 */
MODEL_API ParamDefinition makeCheckParam(const QString &name_en, const QString &name_cn, bool default_value,
                                         const QString &description = {});

/**
 * @brief 创建下拉框类型参数
 * @param name_en 英文名称
 * @param name_cn 中文名称
 * @param default_value 默认值
 * @param options 选项列表
 * @param description 描述
 * @return 参数定义
 */
MODEL_API ParamDefinition makeComboParam(const QString &name_en, const QString &name_cn, const QString &default_value,
                                         QVariantList options, const QString &description = {});

/**
 * @brief 创建并解析由后端 provider 动态提供选项的参数。
 * @param name_en 英文名称
 * @param name_cn 中文名称
 * @param default_value 默认值
 * @param display_type 展示类型，例如 combo
 * @param backend_key 动态数据 provider key
 * @param description 描述
 * @return 动态参数定义
 */
MODEL_API ParamDefinition makeDynamicParam(const QString &name_en, const QString &name_cn,
                                           const QVariant &default_value, const QString &display_type,
                                           const QString &backend_key, const QString &description = {});

} // namespace dltool::model
