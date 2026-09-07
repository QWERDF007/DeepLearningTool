#pragma once

#include "dltool/common/Export.h"

#include <QPointF>
#include <QRectF>
#include <QSize>
#include <vector>

namespace dltool::common::geometry {

/**
 * @brief 将两个对角点规范为顺时针的四点矩形多边形。
 *
 * 坐标使用像素边界坐标，矩形右侧和下侧可以等于图像宽高。
 * 无法形成有效矩形时返回空结果。
 */
COMMON_API std::vector<QPointF> rectangleToPolygon(const QPointF &first, const QPointF &second);

/**
 * @brief 将多边形裁剪到像素边界矩形内。
 *
 * 输入输出均使用像素边界坐标；函数保留所有有面积的连通区域，
 * 只丢弃少于三个点或退化为零面积的结果。
 */
COMMON_API std::vector<QPointF> clipPolygon(const std::vector<QPointF> &polygon, const QRectF &bounds);

/**
 * @brief 按源图像和目标图像尺寸缩放并裁剪多边形。
 */
COMMON_API std::vector<QPointF> mapPolygon(const std::vector<QPointF> &polygon, const QSize &source_size,
                                           const QSize &target_size);

/**
 * @brief 返回多边形的轴对齐边界。
 */
COMMON_API QRectF polygonBounds(const std::vector<QPointF> &polygon);

} // namespace dltool::common::geometry
