#pragma once

#include "dltool/model/Export.h"

#include <QVariant>
#include <QVariantList>
#include <QString>
#include <QStringList>

namespace dltool::model {

/**
 * @brief 构建分组下拉（group_combo）的两级选项列表。
 *
 * 一级为变体叶子项（参数声明 variants/variant_name_template 且解析到变体值时）
 * 与同框架同架构（变体匹配时过滤）的用户已训练模型；二级为各模型权重目录下按
 * 扩展名枚举的权重文件。返回形如 {label, value, subOptions: [{label, value}]} 的列表。
 * @param project_dir 项目目录（模型目录 <project>/models 的父目录）
 * @param project_db 项目数据库路径
 * @param framework_name 当前模型框架名称
 * @param architecture 当前模型架构名称
 * @param extensions 可枚举的权重文件扩展名（来自框架注册的 weight_extensions）
 * @param variants 变体列表（如 {n,s,m,l,x}），空表示无预置叶子项
 * @param variant_template 变体叶子项名模板（{size} 会被替换），如 "yolov8{size}.pt"
 * @param variant_param 选项过滤使用的变体参数名（如 model_size），空表示不按变体过滤
 * @param variant_value 当前解析到的变体值，空表示不按变体过滤
 * @param current_value 当前已选值，不在选项中时追加"当前值"条目
 * @return 两级选项列表
 */
MODEL_API QVariantList modelNestedOptions(const QString &project_dir, const QString &project_db,
                                          const QString &framework_name, const QString &architecture,
                                          const QStringList &extensions, const QStringList &variants,
                                          const QString &variant_template, const QString &variant_param,
                                          const QString &variant_value, const QVariant &current_value);

} // namespace dltool::model

