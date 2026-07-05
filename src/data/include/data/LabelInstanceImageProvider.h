#pragma once

#include "dltool/data/Export.h"

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
 * 为 QML 提供裁剪后的标注区域图像。
 * 矩形边框的绘制由 QML 层负责。
 * URL 格式：image://labelInstance/label_id?padding=value
 * - label_id: 标注实例 ID
 * - padding: 可选，额外像素边距，默认 0
 * 边距从 Settings 单例获取
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
     * @param padding 额外像素边距
     * @return 处理后的图像
     */
    QImage generateThumbnail(int64_t label_id, double padding = 0.0, int margin_override = -1) const;

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
     * @brief 创建错误占位图像
     * @return 错误占位图像
     */
    QImage createErrorPlaceholder() const;

    LabelInstancesListModel *label_instances_;
    ImageInstancesListModel *image_instances_;
    LabelClassesListModel   *label_classes_;
};

} // namespace dltool::data
