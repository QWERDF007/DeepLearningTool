#include "data/LabelInstanceImageProvider.h"

#include "data/Images.h"
#include "data/LabelClasses.h"
#include "data/LabelData.h"
#include "data/Labels.h"
#include "settings/GlobalSettings.h"

#include <spdlog/spdlog.h>

#include <QDebug>
#include <QPainter>
#include <algorithm>
#include <limits>

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

QRectF effectiveLabelBounds(const LabelData_t *label_data)
{
    if (label_data == nullptr)
        return {};

    if (const auto *seg_data = dynamic_cast<const SegLabelData_t *>(label_data);
        seg_data != nullptr && seg_data->points.size() >= 3)
    {
        double min_x = std::numeric_limits<double>::max();
        double min_y = std::numeric_limits<double>::max();
        double max_x = std::numeric_limits<double>::lowest();
        double max_y = std::numeric_limits<double>::lowest();

        for (const QPointF &point : seg_data->points)
        {
            min_x = std::min(min_x, point.x());
            min_y = std::min(min_y, point.y());
            max_x = std::max(max_x, point.x());
            max_y = std::max(max_y, point.y());
        }

        if (max_x > min_x && max_y > min_y)
            return QRectF(QPointF(min_x, min_y), QPointF(max_x, max_y));
    }

    return QRectF(label_data->x, label_data->y, label_data->width, label_data->height);
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
        double  padding = 0.0; // 额外像素边距
        int     margin_override = -1;

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
                    if (ok && parsed_padding >= 0.0)
                    {
                        padding = parsed_padding;
                    }
                    else
                    {
                        spdlog::debug("[LabelInstanceImageProvider] 无效的 padding 值: {}, 使用默认值 0",
                                      key_value[1].toStdString());
                    }
                }
                if (key_value.size() == 2 && key_value[0] == "margin")
                {
                    bool parsed = false;
                    const int margin = key_value[1].toInt(&parsed);
                    if (parsed && margin >= 0)
                        margin_override = margin;
                }
            }
        }

        // 解析 label_id
        bool    ok       = false;
        int64_t label_id = id_part.toLongLong(&ok);

        if (!ok || label_id < 0)
        {
            spdlog::debug("[LabelInstanceImageProvider] 无效的 label_id: {}", id_part.toStdString());
            QImage empty_image = createEmptyImage();
            if (size)
                *size = empty_image.size();
            return empty_image;
        }

        // 生成缩略图
        QImage result = generateThumbnail(label_id, padding, margin_override);
        if (size)
            *size = result.size();

        return result;
    }
    catch (const std::exception &e)
    {
        spdlog::error("[LabelInstanceImageProvider] 请求图像发生异常: {}", e.what());
        QImage empty_image = createEmptyImage();
        if (size)
            *size = empty_image.size();
        return empty_image;
    }
    catch (...)
    {
        spdlog::error("[LabelInstanceImageProvider] 请求图像发生未知异常");
        QImage empty_image = createEmptyImage();
        if (size)
            *size = empty_image.size();
        return empty_image;
    }
}

QImage LabelInstanceImageProvider::generateThumbnail(int64_t label_id, double padding, int margin_override) const
{
    try
    {
        // 1. 验证输入参数
        if (label_id < 0 || !label_instances_)
        {
            spdlog::debug("[LabelInstanceImageProvider] 无效的 label_id: {} 或 label_instances_ 为空", label_id);
            return createEmptyImage();
        }

        // 2. 获取标注实例信息
        LabelInstance *label_instance = label_instances_->getLabelInstance(label_id);
        if (!label_instance)
        {
            spdlog::debug("[LabelInstanceImageProvider] 未找到标注实例: {}", label_id);
            return createEmptyImage();
        }

        const LabelData &label_data = label_instance->data();
        if (!label_data)
        {
            spdlog::debug("[LabelInstanceImageProvider] 标注数据为空: {}", label_id);
            return createEmptyImage();
        }

        int64_t image_id = label_instance->imageId();
        QRectF  bbox = effectiveLabelBounds(label_data.get());
        if (bbox.width() <= 0 || bbox.height() <= 0)
        {
            spdlog::debug("[LabelInstanceImageProvider] 标注 bbox 无效: {}", label_id);
            return createEmptyImage();
        }

        // 3. 获取配置参数
        int margin = margin_override >= 0
                       ? margin_override
                       : dltool::settings::GlobalSettings::getInstance()
                           ->valueForField(dltool::settings::generated::field::Data::Margin, 10)
                           .toInt();

        // 4. 加载原始图像
        if (!image_instances_)
        {
            spdlog::error("[LabelInstanceImageProvider] 图像实例模型 image_instances_ 为空");
            return createErrorPlaceholder();
        }

        ImageInstance *image_instance = image_instances_->getImageInstance(image_id);
        if (!image_instance)
        {
            spdlog::error("[LabelInstanceImageProvider] 未找到图像实例: {}", image_id);
            return createErrorPlaceholder();
        }

        QImage source_image(image_instance->path());
        if (source_image.isNull())
        {
            spdlog::error("[LabelInstanceImageProvider] 加载图像失败: {}", image_instance->path().toStdString());
            return createErrorPlaceholder();
        }

        // 5. 获取填充颜色
        QColor fill_color(DEFAULT_FILL_COLOR);

        // 6. 根据 padding 参数计算扩展边距。
        // padding 使用像素单位，避免标注越大边距变化越剧烈。
        int extended_margin = margin + qRound(padding);

        // 7. 裁剪图像（不绘制矩形边框，边框由 QML 层负责）
        QImage cropped_image = cropImageWithMargin(source_image, bbox, extended_margin, fill_color);

        return cropped_image;
    }
    catch (const std::exception &e)
    {
        spdlog::error("[LabelInstanceImageProvider] 生成缩略图时发生异常: {}", e.what());
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
