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

QRect cropRect(const QImage &image, const QUrlQuery &query)
{
    bool x_ok = false;
    bool y_ok = false;
    bool width_ok = false;
    bool height_ok = false;
    const double x = queryDouble(query, QStringLiteral("x"), &x_ok);
    const double y = queryDouble(query, QStringLiteral("y"), &y_ok);
    const double width = queryDouble(query, QStringLiteral("width"), &width_ok);
    const double height = queryDouble(query, QStringLiteral("height"), &height_ok);
    if (!x_ok || !y_ok || !width_ok || !height_ok || width <= 0.0 || height <= 0.0)
        return {};

    const int left = std::clamp(static_cast<int>(std::floor(x)), 0, image.width());
    const int top = std::clamp(static_cast<int>(std::floor(y)), 0, image.height());
    const int right = std::clamp(static_cast<int>(std::ceil(x + width)), left, image.width());
    const int bottom = std::clamp(static_cast<int>(std::ceil(y + height)), top, image.height());
    return QRect(left, top, right - left, bottom - top);
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
