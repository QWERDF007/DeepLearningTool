#pragma once

#include "DataExport.h"

#include <QColor>
#include <QImage>
#include <QQuickImageProvider>
#include <QRectF>
#include <QString>

namespace dltool::data {

class LabelInstancesListModel;
class ImageInstancesListModel;
class LabelClassesListModel;

/**
 * @brief 标注实例图像提供器
 * 
 * 为 QML 提供裁剪和处理后的标注区域图像。
 * URL 格式：image://labelInstance/label_id?padding=value
 * - label_id: 标注实例 ID
 * - padding: 可选，边界扩展比例（0.0-1.0），默认 0.1
 * 边距从 Settings 单例获取，颜色从 LabelClasses 获取
 */
class DATA_API LabelInstanceImageProvider : public QQuickImageProvider
{
public:
    /**
     * @brief 构造函数
     * @param label_instances 标注实例列表模型
     * @param image_instances 图像实例列表模型
     * @param label_classes 标注类别列表模型
     */
    LabelInstanceImageProvider(LabelInstancesListModel *label_instances, ImageInstancesListModel *image_instances,
                               LabelClassesListModel *label_classes);

    /**
     * @brief 请求图像
     * @param id 图像 ID，格式："label_id"
     * @param size 输出图像尺寸（由 QML 填充）
     * @param requestedSize 请求的图像尺寸
     * @return 处理后的 QImage
     */
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    /**
     * @brief 生成标注缩略图
     * @param label_id 标注 ID
     * @param padding 边界扩展比例（0.0 - 1.0），相对于标注框的最大尺寸
     * @return 处理后的图像
     */
    QImage generateThumbnail(int64_t label_id, double padding = 0.1) const;

    /**
     * @brief 裁剪图像区域
     * @param source_image 原始图像
     * @param bbox 标注矩形（相对于原始图像）
     * @param margin 边距
     * @param fill_color 填充颜色
     * @return 裁剪后的图像
     */
    QImage cropImageWithMargin(const QImage &source_image, const QRectF &bbox, int margin,
                               const QColor &fill_color) const;

    /**
     * @brief 在图像上绘制矩形框
     * @param image 目标图像（会被修改）
     * @param bbox 标注矩形（相对于原始图像）
     * @param crop_rect 裁剪区域（相对于原始图像）
     * @param border_color 边框颜色
     * @param border_width 边框宽度（像素）
     */
    void drawBoundingBox(QImage &image, const QRectF &bbox, const QRectF &crop_rect, const QColor &border_color,
                         int border_width = 2) const;

    /**
     * @brief 创建错误占位图像
     * @return 错误占位图像
     */
    QImage createErrorPlaceholder() const;

    LabelInstancesListModel *label_instances_;
    ImageInstancesListModel *image_instances_;
    LabelClassesListModel   *label_classes_;
};

} // namespace dltool::data
