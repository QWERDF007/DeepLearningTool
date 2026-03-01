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

namespace {
// 常量定义
constexpr int  DEFAULT_PLACEHOLDER_WIDTH  = 200;
constexpr int  DEFAULT_PLACEHOLDER_HEIGHT = 150;
constexpr char DEFAULT_BORDER_COLOR[]     = "#FFFFFF";
constexpr char DEFAULT_FILL_COLOR[]       = "#1a1a1a";
constexpr int  EMPTY_IMAGE_SIZE           = 1;

/**
 * @brief 创建透明的空图像
 */
QImage createEmptyImage()
{
    QImage empty_image(EMPTY_IMAGE_SIZE, EMPTY_IMAGE_SIZE, QImage::Format_ARGB32);
    empty_image.fill(Qt::transparent);
    return empty_image;
}

} // anonymous namespace

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
        // 解析 URL: "label_id?padding=value"
        QString id_part = id;
        double  padding = 0.1; // 默认值

        // 检查是否包含查询参数
        int query_pos = id.indexOf('?');
        if (query_pos != -1)
        {
            id_part              = id.left(query_pos);
            QString query_string = id.mid(query_pos + 1);

            // 解析 padding 参数
            QStringList params = query_string.split('&');
            for (const QString &param : params)
            {
                QStringList key_value = param.split('=');
                if (key_value.size() == 2 && key_value[0] == "padding")
                {
                    bool   ok             = false;
                    double parsed_padding = key_value[1].toDouble(&ok);
                    if (ok && parsed_padding >= 0.0 && parsed_padding <= 1.0)
                    {
                        padding = parsed_padding;
                    }
                    else
                    {
                        spdlog::debug("[LabelInstanceImageProvider] Invalid padding value: {}, using default 0.1",
                                      key_value[1].toStdString());
                    }
                    break;
                }
            }
        }

        // 解析 label_id
        bool    ok       = false;
        int64_t label_id = id_part.toLongLong(&ok);

        if (!ok || label_id < 0)
        {
            spdlog::debug("[LabelInstanceImageProvider] Invalid label_id: {}", id_part.toStdString());
            QImage empty_image = createEmptyImage();
            if (size)
                *size = empty_image.size();
            return empty_image;
        }

        // 生成缩略图
        QImage result = generateThumbnail(label_id, padding);
        if (size)
            *size = result.size();

        return result;
    }
    catch (const std::exception &e)
    {
        spdlog::error("[LabelInstanceImageProvider] Exception in requestImage: {}", e.what());
        QImage empty_image = createEmptyImage();
        if (size)
            *size = empty_image.size();
        return empty_image;
    }
    catch (...)
    {
        spdlog::error("[LabelInstanceImageProvider] Unknown exception in requestImage");
        QImage empty_image = createEmptyImage();
        if (size)
            *size = empty_image.size();
        return empty_image;
    }
}

QImage LabelInstanceImageProvider::generateThumbnail(int64_t label_id, double padding) const
{
    try
    {
        // 1. 验证输入参数
        if (label_id < 0 || !label_instances_)
        {
            spdlog::debug("[LabelInstanceImageProvider] Invalid label_id: {} or label_instances_ is null", label_id);
            return createEmptyImage();
        }

        // 2. 获取标注实例信息
        LabelInstance *label_instance = label_instances_->getLabelInstance(label_id);
        if (!label_instance)
        {
            spdlog::debug("[LabelInstanceImageProvider] Label instance not found: {}", label_id);
            return createEmptyImage();
        }

        const LabelData &label_data = label_instance->data();
        if (!label_data)
        {
            spdlog::debug("[LabelInstanceImageProvider] Label data is null for label_id: {}", label_id);
            return createEmptyImage();
        }

        int64_t image_id       = label_instance->imageId();
        int64_t label_class_id = label_instance->labelClassId();
        QRectF  bbox(label_data->x, label_data->y, label_data->width, label_data->height);

        // 3. 获取配置参数
        auto settings     = dltool::settings::GlobalSettings::getInstance()->data();
        int  margin       = settings->thumbnailMargin();
        int  border_width = settings->labelBorderWidth();

        // 4. 加载原始图像
        if (!image_instances_)
        {
            spdlog::error("[LabelInstanceImageProvider] Image instances model is null");
            return createErrorPlaceholder();
        }

        ImageInstance *image_instance = image_instances_->getImageInstance(image_id);
        if (!image_instance)
        {
            spdlog::error("[LabelInstanceImageProvider] Image instance not found: {}", image_id);
            return createErrorPlaceholder();
        }

        QImage source_image(image_instance->path());
        if (source_image.isNull())
        {
            spdlog::error("[LabelInstanceImageProvider] Failed to load image: {}",
                          image_instance->path().toStdString());
            return createErrorPlaceholder();
        }

        // 5. 获取边框颜色
        QString border_color_str = label_classes_ ? label_classes_->getLabelClassColor(label_class_id) : "";
        QColor  border_color(border_color_str.isEmpty() ? DEFAULT_BORDER_COLOR : border_color_str);
        QColor  fill_color(DEFAULT_FILL_COLOR);

        // 6. 根据 padding 参数计算扩展的边距
        // padding 是相对于 bbox 尺寸的比例（0.0 - 1.0）
        // 计算额外的边距：padding * max(width, height)
        double max_dimension   = qMax(bbox.width(), bbox.height());
        int    extended_margin = margin + static_cast<int>(padding * max_dimension);

        // 7. 裁剪图像并绘制边框
        QImage cropped_image = cropImageWithMargin(source_image, bbox, extended_margin, fill_color);

        QRectF crop_rect(bbox.x() - extended_margin, bbox.y() - extended_margin, bbox.width() + 2 * extended_margin,
                         bbox.height() + 2 * extended_margin);

        drawBoundingBox(cropped_image, bbox, crop_rect, border_color, border_width);

        return cropped_image;
    }
    catch (const std::exception &e)
    {
        spdlog::error("[LabelInstanceImageProvider] Exception in generateThumbnail: {}", e.what());
        return createEmptyImage();
    }
}

QImage LabelInstanceImageProvider::cropImageWithMargin(const QImage &source_image, const QRectF &bbox, int margin,
                                                       const QColor &fill_color) const
{
    // 计算裁剪区域（bbox 扩展 margin 像素）
    QRectF crop_rect(bbox.x() - margin, bbox.y() - margin, bbox.width() + 2 * margin, bbox.height() + 2 * margin);

    // 计算裁剪区域与原始图像的交集（有效区域）
    QRectF source_rect(0, 0, source_image.width(), source_image.height());
    QRectF valid_rect = crop_rect.intersected(source_rect);

    // 创建输出图像，填充背景色
    int    output_width  = qRound(crop_rect.width());
    int    output_height = qRound(crop_rect.height());
    QImage output_image(output_width, output_height, QImage::Format_ARGB32);
    output_image.fill(fill_color);

    // 将原始图像的有效部分绘制到输出图像
    if (!valid_rect.isEmpty())
    {
        QPainter painter(&output_image);

        // 计算目标位置（相对于裁剪区域的偏移）
        QPointF target_offset(valid_rect.x() - crop_rect.x(), valid_rect.y() - crop_rect.y());

        painter.drawImage(target_offset, source_image, valid_rect);
    }

    return output_image;
}

void LabelInstanceImageProvider::drawBoundingBox(QImage &image, const QRectF &bbox, const QRectF &crop_rect,
                                                 const QColor &border_color, int border_width) const
{
    QPainter painter(&image);

    // 设置画笔
    QPen pen(border_color);
    pen.setWidth(border_width);
    pen.setStyle(Qt::SolidLine);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    // 将 bbox 坐标转换到裁剪图像的坐标系
    QRectF bbox_in_output(bbox.x() - crop_rect.x(), bbox.y() - crop_rect.y(), bbox.width(), bbox.height());

    // 调整矩形位置，确保边框完全可见
    // QPainter 的 drawRect 会将线宽的一半绘制在矩形边界内，一半在外
    qreal  half_border = border_width / 2.0;
    QRectF adjusted_rect(bbox_in_output.x() + half_border, bbox_in_output.y() + half_border,
                         bbox_in_output.width() - border_width, bbox_in_output.height() - border_width);

    painter.drawRect(adjusted_rect);
}

QImage LabelInstanceImageProvider::createErrorPlaceholder() const
{
    // 创建灰色背景占位图像
    QImage placeholder(DEFAULT_PLACEHOLDER_WIDTH, DEFAULT_PLACEHOLDER_HEIGHT, QImage::Format_ARGB32);
    placeholder.fill(QColor(128, 128, 128));

    // 绘制错误提示文本
    QPainter painter(&placeholder);
    painter.setPen(Qt::white);
    painter.setFont(QFont("Arial", 12));
    painter.drawText(placeholder.rect(), Qt::AlignCenter, "Image Load Failed");

    return placeholder;
}

} // namespace dltool::data
