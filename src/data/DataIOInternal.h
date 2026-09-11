#pragma once

/**
 * @file DataIOInternal.h
 * @brief data 模块私有 helper 声明。
 *
 * 只供 src/data 内部实现使用，不进入消费者 include 路径。
 */

#include <QString>
#include <QStringList>

#include <cstdint>
#include <map>

namespace dltool::data::detail {

/**
 * @brief 返回支持的图像扩展名过滤器（含大小写两套）。
 * @return 可直接传给 QDir::entryInfoList 的过滤器列表。
 */
QStringList imageNameFilters();

/**
 * @brief 返回数据操作并行线程数。
 * @return 设置值与上下限收敛后的线程数。
 */
int dataIOThreadCount();

/**
 * @brief 返回 Mask 导入多边形近似比例。
 * @return 大于等于零的比例；设置无效时返回默认值。
 */
double maskImportPolygonApproxRatio();

/**
 * @brief 为 LabelMe / Mask 导出生成唯一图像文件名。
 * @param source_path 源图像路径。
 * @param stable_id 稳定 ID，用于冲突时消歧。
 * @param used_names 已占用的文件名集合。
 * @param used_stems 已占用的文件主名集合。
 * @return 唯一文件名。
 */
QString uniqueImageName(const QString &source_path, int64_t stable_id, const std::map<QString, int> &used_names,
                        const std::map<QString, int> &used_stems);

/**
 * @brief 登记扫描到的标签类别，去重并分配默认颜色。
 * @param label_class_info 类别名到颜色的映射，就地更新。
 * @param raw_name 原始类别名。
 * @param color_index 颜色索引，就地递增。
 */
void addScannedLabelClass(std::map<QString, QString> &label_class_info, const QString &raw_name, int &color_index);

} // namespace dltool::data::detail
