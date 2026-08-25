#include "model/EvaluationThumbnailImageProvider.h"

#include "model/AnomalyPreprocessingTransform.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QPainter>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>

namespace dltool::model {

namespace {

double queryDouble(const QUrlQuery &query, const QString &name, bool *ok = nullptr)
{
    bool         parsed = false;
    const double value  = query.queryItemValue(name).toDouble(&parsed);
    if (ok != nullptr)
        *ok = parsed;
    return value;
}

bool queryFlag(const QUrlQuery &query, const QString &name)
{
    const QString value = query.queryItemValue(name).trimmed().toLower();
    return value == QStringLiteral("1") || value == QStringLiteral("true") || value == QStringLiteral("yes");
}

cv::Mat readScoreMap(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray bytes = file.readAll();
    if (bytes.isEmpty())
        return {};
    cv::Mat encoded(1, bytes.size(), CV_8UC1, const_cast<char *>(bytes.constData()));
    return cv::imdecode(encoded, cv::IMREAD_UNCHANGED);
}

QVariantMap preprocessingConfig(const QUrlQuery &query)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(
        query.queryItemValue(QStringLiteral("preprocessing")).toUtf8(), &error);
    return error.error == QJsonParseError::NoError && document.isObject() ? document.toVariant().toMap()
                                                                          : QVariantMap{};
}

QImage makeHeatmap(const QImage &source, const QString &scorePath, const QUrlQuery &query)
{
    const cv::Mat score_map = readScoreMap(scorePath);
    if (score_map.empty() || score_map.type() != CV_32FC1 || score_map.cols <= 0 || score_map.rows <= 0)
        return {};

    const QSize model_size(score_map.cols, score_map.rows);
    const AnomalyPreprocessingTransform transform
        = AnomalyPreprocessingTransform::fromConfig(source.size(), model_size, preprocessingConfig(query));
    QImage base = transform.applyToImage(source);
    if (base.isNull())
        return {};

    bool   threshold_ok = false;
    double threshold    = queryDouble(query, QStringLiteral("heatmapThreshold"), &threshold_ok);
    if (!threshold_ok || !std::isfinite(threshold) || threshold <= 0.0)
        threshold = 1.0;
    threshold = std::clamp(threshold, 0.0001, 1000.0);

    cv::Mat normalized_map(model_size.height(), model_size.width(), CV_8UC1, cv::Scalar(0));
    for (int y = 0; y < score_map.rows; ++y)
    {
        const float *source_row = score_map.ptr<float>(y);
        uchar       *target_row = normalized_map.ptr<uchar>(y);
        for (int x = 0; x < score_map.cols; ++x)
        {
            const double value = static_cast<double>(source_row[x]);
            const double normalized_value = std::isfinite(value)
                                                ? std::clamp(value / threshold, 0.0, 1.0)
                                                : 0.0;
            target_row[x] = static_cast<uchar>(std::lround(normalized_value * 255.0));
        }
    }

    // Keep the color mapping in C++ so every thumbnail uses the same global
    // threshold instead of normalizing each image by its own maximum.
    cv::Mat color_bgr;
    cv::applyColorMap(normalized_map, color_bgr, cv::COLORMAP_JET);
    QImage colored(model_size, QImage::Format_RGB888);
    for (int y = 0; y < model_size.height(); ++y)
    {
        uchar       *rgb_row  = colored.scanLine(y);
        for (int x = 0; x < model_size.width(); ++x)
        {
            const cv::Vec3b bgr = color_bgr.at<cv::Vec3b>(y, x);
            rgb_row[x * 3 + 0] = bgr[2];
            rgb_row[x * 3 + 1] = bgr[1];
            rgb_row[x * 3 + 2] = bgr[0];
        }
    }

    QImage result = base.convertToFormat(QImage::Format_ARGB32);
    QPainter painter(&result);
    painter.setOpacity(0.5);
    painter.drawImage(QPoint(0, 0), colored);
    painter.end();
    return result;
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
    bool         x_ok      = false;
    bool         y_ok      = false;
    bool         width_ok  = false;
    bool         height_ok = false;
    const double x         = queryDouble(query, prefix + QStringLiteral("_x"), &x_ok);
    const double y         = queryDouble(query, prefix + QStringLiteral("_y"), &y_ok);
    const double width     = queryDouble(query, prefix + QStringLiteral("_w"), &width_ok);
    const double height    = queryDouble(query, prefix + QStringLiteral("_h"), &height_ok);
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
    const Box gt = queryBox(query, QStringLiteral("gt"));
    const Box pd = queryBox(query, QStringLiteral("pd"));
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
    const int       query_index = id.indexOf(QChar('?'));
    const QUrlQuery query(query_index >= 0 ? id.mid(query_index + 1) : QString());
    const bool      heatmap = queryFlag(query, QStringLiteral("heatmap"));
    const QString   cache_key = id + (heatmap
                                          ? QString()
                                          : QLatin1Char('\x1f') + QString::number(requestedSize.width()) + QLatin1Char('x')
                                                + QString::number(requestedSize.height()));
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
    const int       query_index = id.indexOf(QChar('?'));
    const QString   query_text  = query_index >= 0 ? id.mid(query_index + 1) : QString();
    const QUrlQuery query(query_text);
    const QString   encoded_path = query.queryItemValue(QStringLiteral("path"));
    if (encoded_path.isEmpty())
        return {};

    // QUrlQuery returns the decoded value.  Do not run a second percent
    // decoding pass: a literal '%' in a Windows or network path is valid.
    const QString image_path = encoded_path;
    QImage        image(image_path);
    if (image.isNull())
        return {};

    const bool heatmap = queryFlag(query, QStringLiteral("heatmap"));
    if (heatmap)
    {
        const QImage rendered = makeHeatmap(image, query.queryItemValue(QStringLiteral("scorePath")), query);
        if (!rendered.isNull())
            return rendered;
        // Let QML switch back to the original URL so a failed derived
        // visualization cannot be mistaken for a valid heatmap.
        return {};
    }

    const QRect crop = cropRect(image, query);
    if (crop.isValid())
        image = image.copy(crop);
    if (image.isNull())
        return {};

    if (!heatmap && requestedSize.isValid() && requestedSize.width() > 0 && requestedSize.height() > 0
        && (image.width() > requestedSize.width() || image.height() > requestedSize.height()))
    {
        image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}

} // namespace dltool::model
