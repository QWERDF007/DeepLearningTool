#include "data/LabelInstanceImageProvider.h"

#include "data/Images.h"
#include "data/LabelCLasses.h"
#include "data/LabelData.h"
#include "data/Labels.h"
#include "settings/GlobalSettings.h"

#include <spdlog/spdlog.h>

#include <QDebug>
#include <QPainter>

namespace dltool::data {

LabelInstanceImageProvider::LabelInstanceImageProvider(LabelInstancesListModel *label_instances,
                                                       ImageInstancesListModel *image_instances,
                                                       LabelClassesListModel   *label_classes)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , label_instances_(label_instances)
    , image_instances_(image_instances)
    , label_classes_(label_classes)
{
}

QImage LabelInstanceImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    Q_UNUSED(requestedSize);

    try
    {
        // 解析 id 参数为 label_id（直接转换字符串为 int64_t）
        bool    ok       = false;
        int64_t label_id = id.toLongLong(&ok);

        if (!ok || label_id < 0)
        {
            spdlog::debug("[LabelInstanceImageProvider] Invalid label_id: {}", id.toStdString());
            QImage emptyImage(1, 1, QImage::Format_ARGB32);
            emptyImage.fill(Qt::transparent);
            if (size)
            {
                *size = emptyImage.size();
            }
            return emptyImage;
        }

        // 调用 generateThumbnail() 生成图像
        QImage result = generateThumbnail(label_id);

        // 设置 size 输出参数为图像尺寸
        if (size)
        {
            *size = result.size();
        }

        // 返回图像
        return result;
    }
    catch (const std::exception &e)
    {
        // 添加异常处理，捕获所有异常并返回空图像
        spdlog::error("Exception in requestImage: {}", e.what());
        QImage emptyImage(1, 1, QImage::Format_ARGB32);
        emptyImage.fill(Qt::transparent);
        if (size)
        {
            *size = emptyImage.size();
        }
        return emptyImage;
    }
    catch (...)
    {
        spdlog::error("Unknown exception in requestImage");
        QImage emptyImage(1, 1, QImage::Format_ARGB32);
        emptyImage.fill(Qt::transparent);
        if (size)
        {
            *size = emptyImage.size();
        }
        return emptyImage;
    }
}

QImage LabelInstanceImageProvider::generateThumbnail(int64_t label_id) const
{
    try
    {
        // 验证 label_id 有效性
        if (label_id < 0 || !label_instances_)
        {
            spdlog::debug("Invalid label_id: {} or label_instances_ is null", label_id);
            QImage emptyImage(1, 1, QImage::Format_ARGB32);
            emptyImage.fill(Qt::transparent);
            return emptyImage;
        }

        // 通过 label_instances_ 查询标注信息（image_id, bbox, label_class_id）
        LabelInstance *label_instance = label_instances_->getLabelInstance(label_id);
        if (!label_instance)
        {
            spdlog::debug("Label instance not found: {}", label_id);
            QImage emptyImage(1, 1, QImage::Format_ARGB32);
            emptyImage.fill(Qt::transparent);
            return emptyImage;
        }

        int64_t image_id       = label_instance->imageId();
        int64_t label_class_id = label_instance->labelClassId();

        // 获取 bbox 信息
        const LabelData &label_data = label_instance->data();
        if (!label_data)
        {
            spdlog::debug("Label data is null for label_id: {}", label_id);
            QImage emptyImage(1, 1, QImage::Format_ARGB32);
            emptyImage.fill(Qt::transparent);
            return emptyImage;
        }

        QRectF bbox(label_data->x, label_data->y, label_data->width, label_data->height);

        // 从 GlobalSettings 获取 thumbnailMargin 值
        int margin = dltool::settings::GlobalSettings::getInstance()->data()->thumbnailMargin();

        // 通过 image_instances_ 查询图像路径
        if (!image_instances_)
        {
            spdlog::error("Image instances model is null");
            return createErrorPlaceholder();
        }

        ImageInstance *image_instance = image_instances_->getImageInstance(image_id);
        if (!image_instance)
        {
            spdlog::error("Image instance not found: {}", image_id);
            return createErrorPlaceholder();
        }

        QString imagePath = image_instance->path();

        // 使用 QImage::load() 加载原始图像，失败则返回错误占位图像
        QImage sourceImage(imagePath);
        if (sourceImage.isNull())
        {
            spdlog::error("Failed to load image: {}", imagePath.toStdString());
            return createErrorPlaceholder();
        }

        // 通过 label_classes_ 查询类别颜色
        QString borderColorStr;
        if (label_classes_)
        {
            borderColorStr = label_classes_->getLabelClassColor(label_class_id);
        }
        QColor borderColor(borderColorStr.isEmpty() ? "#FFFFFF" : borderColorStr);

        // 使用固定的填充颜色（深灰色 #1a1a1a）
        QColor fillColor("#1a1a1a");

        // 调用 cropImageWithMargin() 裁剪图像
        QImage croppedImage = cropImageWithMargin(sourceImage, bbox, margin, fillColor);

        // 计算裁剪区域（用于 drawBoundingBox）
        QRectF cropRect(bbox.x() - margin, bbox.y() - margin, bbox.width() + 2 * margin, bbox.height() + 2 * margin);

        // 从 GlobalSettings 获取 labelBorderWidth 值
        int borderWidth = dltool::settings::GlobalSettings::getInstance()->data()->labelBorderWidth();

        // 调用 drawBoundingBox() 绘制矩形框
        drawBoundingBox(croppedImage, bbox, cropRect, borderColor, borderWidth);

        // 返回处理后的图像
        return croppedImage;
    }
    catch (const std::exception &e)
    {
        spdlog::error("Exception in generateThumbnail: {}", e.what());
        QImage emptyImage(1, 1, QImage::Format_ARGB32);
        emptyImage.fill(Qt::transparent);
        return emptyImage;
    }
}

QImage LabelInstanceImageProvider::cropImageWithMargin(const QImage &sourceImage, const QRectF &bbox, int margin,
                                                       const QColor &fillColor) const
{
    // 计算裁剪区域：bbox 扩展 margin 像素
    QRectF cropRect(bbox.x() - margin, bbox.y() - margin, bbox.width() + 2 * margin, bbox.height() + 2 * margin);

    // 计算裁剪区域与原始图像的交集（有效区域）
    QRectF sourceRect(0, 0, sourceImage.width(), sourceImage.height());
    QRectF validRect = cropRect.intersected(sourceRect);

    // 创建新 QImage，尺寸为裁剪区域大小，填充固定颜色 #1a1a1a
    int    outputWidth  = qRound(cropRect.width());
    int    outputHeight = qRound(cropRect.height());
    QImage outputImage(outputWidth, outputHeight, QImage::Format_ARGB32);
    outputImage.fill(fillColor);

    // 使用 QPainter 将原始图像的有效部分绘制到新图像
    if (!validRect.isEmpty())
    {
        QPainter painter(&outputImage);

        // 计算源图像中要复制的区域
        QRectF sourceRegion = validRect;

        // 计算目标图像中的绘制位置（相对于裁剪区域的偏移）
        QPointF targetOffset(validRect.x() - cropRect.x(), validRect.y() - cropRect.y());

        // 绘制图像的有效部分
        painter.drawImage(targetOffset, sourceImage, sourceRegion);
    }

    // 返回裁剪后的图像
    return outputImage;
}

void LabelInstanceImageProvider::drawBoundingBox(QImage &image, const QRectF &bbox, const QRectF &cropRect,
                                                 const QColor &borderColor, int borderWidth) const
{
    // 创建 QPainter 对象，目标为裁剪后的图像
    QPainter painter(&image);

    // 设置画笔：颜色为 borderColor，宽度为 2 像素，样式为实线
    QPen pen(borderColor);
    pen.setWidth(borderWidth);
    pen.setStyle(Qt::SolidLine);
    painter.setPen(pen);

    // 不填充矩形内部
    painter.setBrush(Qt::NoBrush);

    // 计算矩形框在裁剪图像中的位置（坐标系转换）
    // bbox 是相对于原始图像的坐标
    // cropRect 是裁剪区域相对于原始图像的坐标
    // 需要将 bbox 转换到裁剪图像的坐标系
    QRectF bboxInOutput(bbox.x() - cropRect.x(), bbox.y() - cropRect.y(), bbox.width(), bbox.height());

    // 使用 QPainter::drawRect() 绘制矩形框（不填充）
    // 为了确保矩形框边框在图像内完全可见，需要调整矩形位置
    // QPainter 的 drawRect 会将线宽的一半绘制在矩形边界内，一半在外
    // 为了让边框完全可见，需要将矩形向内收缩 borderWidth/2
    qreal  halfBorder = borderWidth / 2.0;
    QRectF adjustedRect(bboxInOutput.x() + halfBorder, bboxInOutput.y() + halfBorder,
                        bboxInOutput.width() - borderWidth, bboxInOutput.height() - borderWidth);

    painter.drawRect(adjustedRect);
}

QImage LabelInstanceImageProvider::createErrorPlaceholder() const
{
    // 生成灰色背景图像（200x150）
    QImage placeholder(200, 150, QImage::Format_ARGB32);
    placeholder.fill(QColor(128, 128, 128));

    // 使用 QPainter 绘制 "Image Load Failed" 文本
    QPainter painter(&placeholder);
    painter.setPen(Qt::white);
    painter.setFont(QFont("Arial", 12));
    painter.drawText(placeholder.rect(), Qt::AlignCenter, "Image Load Failed");

    // 返回占位图像
    return placeholder;
}

} // namespace dltool::data
