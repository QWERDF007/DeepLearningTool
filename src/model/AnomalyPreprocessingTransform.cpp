#include "model/AnomalyPreprocessingTransform.h"

#include <QPainter>
#include <QVariantList>

#include <cmath>

namespace dltool::model {

namespace {

QVariantMap networkConfig(const QVariantMap &preprocessing)
{
    const QVariantMap nested = preprocessing.value(QStringLiteral("network")).toMap();
    return nested.isEmpty() ? preprocessing : nested;
}

int positiveInteger(const QVariant &value, const int fallback = 0)
{
    bool      ok     = false;
    const int result = value.toInt(&ok);
    return ok && result > 0 ? result : fallback;
}

int nonNegativeInteger(const QVariant &value, const int fallback = 0)
{
    bool      ok     = false;
    const int result = value.toInt(&ok);
    return ok && result >= 0 ? result : fallback;
}

QSize configuredSize(const QVariantMap &config, const QString &key)
{
    const QVariant     value = config.value(key);
    const QVariantList list  = value.toList();
    if (!list.isEmpty())
    {
        if (list.size() == 1)
        {
            const int side = positiveInteger(list.front());
            return side > 0 ? QSize(side, side) : QSize{};
        }
        const int height = positiveInteger(list.at(0));
        const int width  = positiveInteger(list.at(1));
        return width > 0 && height > 0 ? QSize(width, height) : QSize{};
    }

    const QVariantMap map = value.toMap();
    if (!map.isEmpty())
    {
        const int width  = positiveInteger(map.value(QStringLiteral("width")));
        const int height = positiveInteger(map.value(QStringLiteral("height")));
        return width > 0 && height > 0 ? QSize(width, height) : QSize{};
    }

    const int side = positiveInteger(value);
    return side > 0 ? QSize(side, side) : QSize{};
}

QSize configuredResizeSize(const QVariantMap &config, const QSize &fallback)
{
    QSize size = configuredSize(config, QStringLiteral("resize_size"));
    if (!size.isValid())
        size = configuredSize(config, QStringLiteral("image_size"));

    const int width  = positiveInteger(config.value(QStringLiteral("resize_width")), size.width());
    const int height = positiveInteger(config.value(QStringLiteral("resize_height")), size.height());
    return width > 0 && height > 0 ? QSize(width, height) : fallback;
}

QSize configuredCropSize(const QVariantMap &config)
{
    QSize size = configuredSize(config, QStringLiteral("center_crop_size"));
    if (!size.isValid())
        size = configuredSize(config, QStringLiteral("crop_size"));

    const int width  = positiveInteger(config.value(QStringLiteral("crop_width")), size.width());
    const int height = positiveInteger(config.value(QStringLiteral("crop_height")), size.height());
    return width > 0 && height > 0 ? QSize(width, height) : QSize{};
}

QMargins configuredPadding(const QVariantMap &config)
{
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    const QVariant     padding_value = config.value(QStringLiteral("padding"));
    const QVariantList padding_list  = padding_value.toList();
    if (padding_list.size() >= 4)
    {
        left   = nonNegativeInteger(padding_list.at(0));
        top    = nonNegativeInteger(padding_list.at(1));
        right  = nonNegativeInteger(padding_list.at(2));
        bottom = nonNegativeInteger(padding_list.at(3));
    }
    else if (padding_list.size() == 2)
    {
        left = right = nonNegativeInteger(padding_list.at(0));
        top = bottom = nonNegativeInteger(padding_list.at(1));
    }
    else if (padding_list.size() == 1)
    {
        left = top = right = bottom = nonNegativeInteger(padding_list.front());
    }
    else
    {
        const QVariantMap padding_map = padding_value.toMap();
        if (!padding_map.isEmpty())
        {
            left   = nonNegativeInteger(padding_map.value(QStringLiteral("left")));
            top    = nonNegativeInteger(padding_map.value(QStringLiteral("top")));
            right  = nonNegativeInteger(padding_map.value(QStringLiteral("right")));
            bottom = nonNegativeInteger(padding_map.value(QStringLiteral("bottom")));
        }
        else
        {
            const int all = nonNegativeInteger(padding_value);
            left = top = right = bottom = all;
        }
    }

    left   = nonNegativeInteger(config.value(QStringLiteral("padding_left")), left);
    top    = nonNegativeInteger(config.value(QStringLiteral("padding_top")), top);
    right  = nonNegativeInteger(config.value(QStringLiteral("padding_right")), right);
    bottom = nonNegativeInteger(config.value(QStringLiteral("padding_bottom")), bottom);
    return {left, top, right, bottom};
}

int roundHalfToEven(const double value)
{
    const double lower    = std::floor(value);
    const double fraction = value - lower;
    if (fraction < 0.5)
        return static_cast<int>(lower);
    if (fraction > 0.5)
        return static_cast<int>(lower + 1.0);
    const int lower_integer = static_cast<int>(lower);
    return lower_integer % 2 == 0 ? lower_integer : lower_integer + 1;
}

} // namespace

AnomalyPreprocessingTransform AnomalyPreprocessingTransform::fromConfig(
    const QSize &source_size, const QSize &model_size, const QVariantMap &preprocessing_config)
{
    AnomalyPreprocessingTransform transform;
    transform.source_size_ = source_size;
    transform.model_size_  = model_size;
    if (!source_size.isValid() || source_size.isEmpty() || !model_size.isValid() || model_size.isEmpty())
        return transform;

    const QVariantMap config = networkConfig(preprocessing_config);
    transform.resized_size_  = configuredResizeSize(config, source_size);
    transform.padding_       = configuredPadding(config);

    QSize padded_size(transform.resized_size_.width() + transform.padding_.left() + transform.padding_.right(),
                      transform.resized_size_.height() + transform.padding_.top() + transform.padding_.bottom());
    QSize crop_size = configuredCropSize(config);
    if (!crop_size.isValid())
        crop_size = padded_size;

    // torchvision CenterCrop pads a too-small input symmetrically before
    // cropping. Odd extra pixels are assigned to the right/bottom side.
    if (crop_size.width() > padded_size.width())
    {
        const int difference = crop_size.width() - padded_size.width();
        transform.padding_.setLeft(transform.padding_.left() + difference / 2);
        transform.padding_.setRight(transform.padding_.right() + (difference + 1) / 2);
    }
    if (crop_size.height() > padded_size.height())
    {
        const int difference = crop_size.height() - padded_size.height();
        transform.padding_.setTop(transform.padding_.top() + difference / 2);
        transform.padding_.setBottom(transform.padding_.bottom() + (difference + 1) / 2);
    }
    padded_size = QSize(transform.resized_size_.width() + transform.padding_.left() + transform.padding_.right(),
                        transform.resized_size_.height() + transform.padding_.top() + transform.padding_.bottom());

    bool      crop_x_ok = false;
    bool      crop_y_ok = false;
    const int configured_crop_x = config.value(QStringLiteral("crop_x")).toInt(&crop_x_ok);
    const int configured_crop_y = config.value(QStringLiteral("crop_y")).toInt(&crop_y_ok);
    const int crop_x = crop_x_ok ? configured_crop_x
                                 : roundHalfToEven((padded_size.width() - crop_size.width()) / 2.0);
    const int crop_y = crop_y_ok ? configured_crop_y
                                 : roundHalfToEven((padded_size.height() - crop_size.height()) / 2.0);

    transform.padded_size_ = padded_size;
    transform.crop_rect_   = QRect(QPoint(crop_x, crop_y), crop_size);
    transform.valid_       = transform.resized_size_.isValid() && !transform.resized_size_.isEmpty()
                       && transform.padded_size_.isValid() && !transform.padded_size_.isEmpty()
                       && transform.crop_rect_.width() > 0 && transform.crop_rect_.height() > 0
                       && QRect(QPoint(0, 0), transform.padded_size_).contains(transform.crop_rect_);
    return transform;
}

bool AnomalyPreprocessingTransform::isValid() const
{
    return valid_;
}

QSize AnomalyPreprocessingTransform::sourceSize() const
{
    return source_size_;
}

QSize AnomalyPreprocessingTransform::resizedSize() const
{
    return resized_size_;
}

QSize AnomalyPreprocessingTransform::paddedSize() const
{
    return padded_size_;
}

QSize AnomalyPreprocessingTransform::modelSize() const
{
    return model_size_;
}

QMargins AnomalyPreprocessingTransform::padding() const
{
    return padding_;
}

QRect AnomalyPreprocessingTransform::cropRect() const
{
    return crop_rect_;
}

QPointF AnomalyPreprocessingTransform::imageToModel(const QPointF &point) const
{
    if (!valid_)
        return {};
    const double resized_edge_x = (point.x() + 0.5) * resized_size_.width() / source_size_.width();
    const double resized_edge_y = (point.y() + 0.5) * resized_size_.height() / source_size_.height();
    const double crop_edge_x    = resized_edge_x + padding_.left() - crop_rect_.x();
    const double crop_edge_y    = resized_edge_y + padding_.top() - crop_rect_.y();
    return {crop_edge_x * model_size_.width() / crop_rect_.width() - 0.5,
            crop_edge_y * model_size_.height() / crop_rect_.height() - 0.5};
}

QPointF AnomalyPreprocessingTransform::modelToImage(const QPointF &point) const
{
    if (!valid_)
        return {};
    const double crop_edge_x = (point.x() + 0.5) * crop_rect_.width() / model_size_.width();
    const double crop_edge_y = (point.y() + 0.5) * crop_rect_.height() / model_size_.height();
    const double resized_edge_x = crop_rect_.x() + crop_edge_x - padding_.left();
    const double resized_edge_y = crop_rect_.y() + crop_edge_y - padding_.top();
    return {resized_edge_x * source_size_.width() / resized_size_.width() - 0.5,
            resized_edge_y * source_size_.height() / resized_size_.height() - 0.5};
}

QImage AnomalyPreprocessingTransform::applyToImage(const QImage &source) const
{
    if (!valid_ || source.isNull() || source.size() != source_size_)
        return {};

    QImage resized = source.convertToFormat(QImage::Format_RGB888)
                         .scaled(resized_size_, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (resized.isNull())
        return {};

    QImage padded(padded_size_, QImage::Format_RGB888);
    padded.fill(Qt::black);
    QPainter painter(&padded);
    painter.drawImage(QPoint(padding_.left(), padding_.top()), resized);
    painter.end();

    QImage cropped = padded.copy(crop_rect_);
    if (cropped.isNull())
        return {};
    if (cropped.size() != model_size_)
        cropped = cropped.scaled(model_size_, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return cropped;
}

} // namespace dltool::model
