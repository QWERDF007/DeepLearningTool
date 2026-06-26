#pragma once

/**
 * @file SettingsValue.h
 * @brief 生成字段键的常用类型化读取辅助函数。
 */

#include "settings/GlobalSettings.h"

#include <QString>

namespace dltool::settings {

/**
 * @brief 读取字符串设置值并去除首尾空白。
 * @param settings 全局设置实例，可为空。
 * @param field_key 生成字段键。
 * @param fallback 设置缺失或实例为空时使用的备用值。
 * @return 设置字符串值。
 */
template<typename FieldKey>
QString settingString(const GlobalSettings *settings, FieldKey field_key, const QString &fallback = {})
{
    return settings != nullptr ? settings->valueForField(field_key, fallback).toString().trimmed() : fallback;
}

/**
 * @brief 读取整数设置值。
 * @param settings 全局设置实例，可为空。
 * @param field_key 生成字段键。
 * @param fallback 设置缺失或转换失败时使用的备用值。
 * @return 设置整数值。
 */
template<typename FieldKey>
int settingInt(const GlobalSettings *settings, FieldKey field_key, int fallback)
{
    bool      ok    = false;
    const int value = settings != nullptr ? settings->valueForField(field_key, fallback).toInt(&ok) : fallback;
    return ok ? value : fallback;
}

/**
 * @brief 读取 double 设置值。
 * @param settings 全局设置实例，可为空。
 * @param field_key 生成字段键。
 * @param fallback 设置缺失或转换失败时使用的备用值。
 * @return 设置 double 值。
 */
template<typename FieldKey>
double settingDouble(const GlobalSettings *settings, FieldKey field_key, double fallback)
{
    bool         ok    = false;
    const double value = settings != nullptr ? settings->valueForField(field_key, fallback).toDouble(&ok) : fallback;
    return ok ? value : fallback;
}

/**
 * @brief 读取布尔设置值。
 * @param settings 全局设置实例，可为空。
 * @param field_key 生成字段键。
 * @param fallback 设置缺失或实例为空时使用的备用值。
 * @return 设置布尔值。
 */
template<typename FieldKey>
bool settingBool(const GlobalSettings *settings, FieldKey field_key, bool fallback)
{
    return settings != nullptr ? settings->valueForField(field_key, fallback).toBool() : fallback;
}

} // namespace dltool::settings
