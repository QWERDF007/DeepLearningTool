#include "data/ImageInstanceImageProvider.h"

#include "data/Images.h"

#include <spdlog/spdlog.h>

#include <QColor>
#include <QFont>
#include <QImageReader>
#include <QPainter>
#include <algorithm>

namespace dltool::data {

namespace {

constexpr int DEFAULT_THUMBNAIL_WIDTH  = 240;
constexpr int DEFAULT_THUMBNAIL_HEIGHT = 240;
constexpr int EMPTY_IMAGE_SIZE         = 1;

QSize normalizedRequestedSize(const QSize &requested_size)
{
    if (requested_size.width() > 0 && requested_size.height() > 0)
        return requested_size;
    return QSize(DEFAULT_THUMBNAIL_WIDTH, DEFAULT_THUMBNAIL_HEIGHT);
}

QSize scaledSizeForReader(const QSize &source_size, const QSize &requested_size)
{
    if (!source_size.isValid())
        return QSize();

    QSize target_size = source_size.scaled(normalizedRequestedSize(requested_size), Qt::KeepAspectRatio);
    target_size.setWidth(std::max(1, target_size.width()));
    target_size.setHeight(std::max(1, target_size.height()));
    return target_size;
}

} // namespace

ImageInstanceImageProvider::ImageInstanceImageProvider(ImageInstancesListModel *image_instances)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , image_instances_(image_instances)
{
}

QImage ImageInstanceImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    try
    {
        QString id_part = id;
        const int query_pos = id.indexOf('?');
        if (query_pos != -1)
            id_part = id.left(query_pos);

        bool ok = false;
        const int64_t image_id = id_part.toLongLong(&ok);
        if (!ok || image_id < 0)
        {
            spdlog::debug("[ImageInstanceImageProvider] 无效的 image_id: {}", id_part.toStdString());
            QImage empty_image = createEmptyImage();
            if (size)
                *size = empty_image.size();
            return empty_image;
        }

        QImage result = loadThumbnail(image_id, requestedSize);
        if (size)
            *size = result.size();
        return result;
    }
    catch (const std::exception &e)
    {
        spdlog::error("[ImageInstanceImageProvider] 请求图像发生异常: {}", e.what());
        QImage empty_image = createEmptyImage();
        if (size)
            *size = empty_image.size();
        return empty_image;
    }
    catch (...)
    {
        spdlog::error("[ImageInstanceImageProvider] 请求图像发生未知异常");
        QImage empty_image = createEmptyImage();
        if (size)
            *size = empty_image.size();
        return empty_image;
    }
}

QImage ImageInstanceImageProvider::loadThumbnail(int64_t image_id, const QSize &requested_size) const
{
    if (!image_instances_)
    {
        spdlog::error("[ImageInstanceImageProvider] 图像实例模型为空");
        return createErrorPlaceholder(requested_size);
    }

    const QString image_path = image_instances_->getImagePath(image_id);
    if (image_path.isEmpty())
    {
        spdlog::debug("[ImageInstanceImageProvider] 未找到图像实例: {}", image_id);
        return createEmptyImage();
    }

    QImageReader reader(image_path);
    reader.setAutoTransform(true);

    const QSize reader_size = reader.size();
    const QSize scaled_size = scaledSizeForReader(reader_size, requested_size);
    if (scaled_size.isValid())
        reader.setScaledSize(scaled_size);

    QImage image = reader.read();
    if (image.isNull())
    {
        spdlog::error("[ImageInstanceImageProvider] 加载图像失败: {}, {}", image_path.toStdString(),
                      reader.errorString().toStdString());
        return createErrorPlaceholder(requested_size);
    }

    const QSize target_size = normalizedRequestedSize(requested_size);
    if (image.width() > target_size.width() || image.height() > target_size.height())
        image = image.scaled(target_size, Qt::KeepAspectRatio, Qt::FastTransformation);

    return image;
}

QImage ImageInstanceImageProvider::createEmptyImage() const
{
    QImage empty_image(EMPTY_IMAGE_SIZE, EMPTY_IMAGE_SIZE, QImage::Format_ARGB32);
    empty_image.fill(Qt::transparent);
    return empty_image;
}

QImage ImageInstanceImageProvider::createErrorPlaceholder(const QSize &requested_size) const
{
    const QSize target_size = normalizedRequestedSize(requested_size);
    QImage placeholder(target_size, QImage::Format_ARGB32);
    placeholder.fill(QColor(128, 128, 128));

    QPainter painter(&placeholder);
    painter.setPen(Qt::white);
    painter.setFont(QFont("Arial", 12));
    painter.drawText(placeholder.rect(), Qt::AlignCenter, "Image Load Failed");
    return placeholder;
}

} // namespace dltool::data
