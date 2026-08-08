#include "model/EvaluationThumbnailImageProvider.h"

#include <QFileInfo>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>

namespace dltool::model {

namespace {

double queryDouble(const QUrlQuery &query, const QString &name, bool *ok = nullptr)
{
    bool parsed = false;
    const double value = query.queryItemValue(name).toDouble(&parsed);
    if (ok != nullptr)
        *ok = parsed;
    return value;
}

struct Box
{
    bool   valid{false};
    double x{0.0};
    double y{0.0};
    double width{0.0};
    double height{0.0};
};

Box queryBox(const QUrlQuery &query, const QString &prefix)
{
    bool x_ok = false;
    bool y_ok = false;
    bool width_ok = false;
    bool height_ok = false;
    const double x = queryDouble(query, prefix + QStringLiteral("_x"), &x_ok);
    const double y = queryDouble(query, prefix + QStringLiteral("_y"), &y_ok);
    const double width = queryDouble(query, prefix + QStringLiteral("_w"), &width_ok);
    const double height = queryDouble(query, prefix + QStringLiteral("_h"), &height_ok);
    if (!x_ok || !y_ok || !width_ok || !height_ok || width <= 0.0 || height <= 0.0)
        return {};
    return {true, x, y, width, height};
}

/**
 * @brief 推导裁剪视口:GT/PRED bounds 并集扩展 5% 边距,钳制到图像边界。
 *
 * 与 QML 侧 EvaluationInstanceThumbnail 使用的公式保持一致,
 * 评估阶段不再预计算该视口。
 */
QRect cropRect(const QImage &image, const QUrlQuery &query)
{
    const Box gt  = queryBox(query, QStringLiteral("gt"));
    const Box pd  = queryBox(query, QStringLiteral("pd"));
    Box       bounds;
    if (!gt.valid)
        bounds = pd;
    else if (!pd.valid)
        bounds = gt;
    else
    {
        const double left   = std::min(gt.x, pd.x);
        const double top    = std::min(gt.y, pd.y);
        const double right  = std::max(gt.x + gt.width, pd.x + pd.width);
        const double bottom = std::max(gt.y + gt.height, pd.y + pd.height);
        bounds              = {true, left, top, right - left, bottom - top};
    }
    if (!bounds.valid)
        return {};

    const double padding = std::max(4.0, std::max(bounds.width, bounds.height) * 0.05);
    double       left    = std::max(0.0, bounds.x - padding);
    double       top     = std::max(0.0, bounds.y - padding);
    double       right   = std::min(bounds.x + bounds.width + padding, static_cast<double>(image.width()));
    double       bottom  = std::min(bounds.y + bounds.height + padding, static_cast<double>(image.height()));
    left                 = std::min(left, right);
    top                  = std::min(top, bottom);
    if (right <= left || bottom <= top)
        return {};

    const int left_px   = std::clamp(static_cast<int>(std::floor(left)), 0, image.width());
    const int top_px    = std::clamp(static_cast<int>(std::floor(top)), 0, image.height());
    const int right_px  = std::clamp(static_cast<int>(std::ceil(right)), left_px, image.width());
    const int bottom_px = std::clamp(static_cast<int>(std::ceil(bottom)), top_px, image.height());
    return QRect(left_px, top_px, right_px - left_px, bottom_px - top_px);
}

} // namespace

EvaluationThumbnailImageProvider::EvaluationThumbnailImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
    , cache_(128)
{
}

QImage EvaluationThumbnailImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const QString cache_key = id + QLatin1Char('\x1f') + QString::number(requestedSize.width())
        + QLatin1Char('x') + QString::number(requestedSize.height());
    {
        QMutexLocker locker(&mutex_);
        if (const QImage *cached = cache_.object(cache_key))
        {
            if (size != nullptr)
                *size = cached->size();
            return *cached;
        }
    }

    const QImage image = loadImage(id, requestedSize);
    if (size != nullptr)
        *size = image.size();
    if (image.isNull())
        return {};

    QMutexLocker locker(&mutex_);
    cache_.insert(cache_key, new QImage(image));
    return image;
}

QImage EvaluationThumbnailImageProvider::loadImage(const QString &id, const QSize &requestedSize) const
{
    const int query_index = id.indexOf(QChar('?'));
    const QString query_text = query_index >= 0 ? id.mid(query_index + 1) : QString();
    const QUrlQuery query(query_text);
    const QString encoded_path = query.queryItemValue(QStringLiteral("path"));
    if (encoded_path.isEmpty())
        return {};

    // QUrlQuery returns the decoded value.  Do not run a second percent
    // decoding pass: a literal '%' in a Windows or network path is valid.
    const QString image_path = encoded_path;
    QImage image(image_path);
    if (image.isNull())
        return {};

    const QRect crop = cropRect(image, query);
    if (crop.isValid())
        image = image.copy(crop);
    if (image.isNull())
        return {};

    if (requestedSize.isValid() && requestedSize.width() > 0 && requestedSize.height() > 0
        && (image.width() > requestedSize.width() || image.height() > requestedSize.height()))
    {
        image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}

} // namespace dltool::model
