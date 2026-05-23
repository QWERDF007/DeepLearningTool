#include "settings/DataSettings.h"

#include "database/DataBase.h"

#include <spdlog/spdlog.h>

#include <algorithm>

namespace dltool::settings {

DataSettings::DataSettings(QObject *parent)
    : QObject(parent)
{
}

DataSettings::~DataSettings() {}

void DataSettings::setThumbnailMargin(int value)
{
    // 验证：边距必须大于等于 0
    value = std::max(0, value);

    if (thumbnail_margin_ != value)
    {
        thumbnail_margin_ = value;
        emit thumbnailMarginChanged();
    }
}

void DataSettings::setThumbnailCacheSize(int value)
{
    // 验证：缓存大小必须大于 0
    value = std::max(1, value);

    if (thumbnail_cache_size_ != value)
    {
        thumbnail_cache_size_ = value;
        emit thumbnailCacheSizeChanged();
    }
}

void DataSettings::setImageLoadThreads(int value)
{
    // 验证：线程数必须在 1-16 之间
    value = std::clamp(value, 1, 16);

    if (image_load_threads_ != value)
    {
        image_load_threads_ = value;
        emit imageLoadThreadsChanged();
    }
}

void DataSettings::setLabelBorderWidth(int value)
{
    // 验证：边框宽度必须大于 0
    value = std::max(1, value);

    if (label_border_width_ != value)
    {
        label_border_width_ = value;
        emit labelBorderWidthChanged();
    }
}

void DataSettings::setLabelFillOpacity(int value)
{
    // 验证：透明度必须在 0-100 之间
    value = std::clamp(value, 0, 100);

    if (label_fill_opacity_ != value)
    {
        label_fill_opacity_ = value;
        emit labelFillOpacityChanged();
    }
}

void DataSettings::setImageCellScale(double value)
{
    // 验证：缩放值必须在合理范围内
    value = std::clamp(value, 0.1, 10.0);

    if (image_cell_scale_ != value)
    {
        image_cell_scale_ = value;
        emit imageCellScaleChanged();
    }
}

void DataSettings::setImageCellScaleFrom(double value)
{
    // 验证：最小缩放值必须大于 0
    value = std::max(0.1, value);

    if (image_cell_scale_from_ != value)
    {
        image_cell_scale_from_ = value;
        emit imageCellScaleFromChanged();
    }
}

void DataSettings::setImageCellScaleTo(double value)
{
    // 验证：最大缩放值必须大于最小值
    value = std::max(image_cell_scale_from_ + 0.1, value);

    if (image_cell_scale_to_ != value)
    {
        image_cell_scale_to_ = value;
        emit imageCellScaleToChanged();
    }
}

void DataSettings::setImageCellScaleStepSize(double value)
{
    // 验证：步长必须大于 0
    value = std::max(0.01, value);

    if (image_cell_scale_step_size_ != value)
    {
        image_cell_scale_step_size_ = value;
        emit imageCellScaleStepSizeChanged();
    }
}

void DataSettings::setLabelThumbnailScale(double value)
{
    // 验证：缩放值必须在 0.5-4.0 范围内
    value = std::clamp(value, 0.5, 4.0);

    if (label_thumbnail_scale_ != value)
    {
        label_thumbnail_scale_ = value;
        emit labelThumbnailScaleChanged();
    }
}

void DataSettings::setLabelThumbnailAspectRatio(double value)
{
    // 验证：长宽比必须在 0.5-2.0 范围内
    value = std::clamp(value, 0.5, 2.0);

    if (label_thumbnail_aspect_ratio_ != value)
    {
        label_thumbnail_aspect_ratio_ = value;
        emit labelThumbnailAspectRatioChanged();
    }
}

void DataSettings::setLabelThumbnailBorderPadding(double value)
{
    // 验证：边界必须在 0.0-1.0 范围内
    value = std::clamp(value, 0.0, 1.0);

    if (label_thumbnail_border_padding_ != value)
    {
        label_thumbnail_border_padding_ = value;
        emit labelThumbnailBorderPaddingChanged();
    }
}

void DataSettings::load(database::SettingsDataBase *database)
{
    if (!database)
    {
        return;
    }

    const QString group = QStringLiteral("Data");
    QString       err_msg;

    setThumbnailMargin(database->value(group, QStringLiteral("thumbnailMargin"), 10, err_msg).toInt());
    setThumbnailCacheSize(database->value(group, QStringLiteral("thumbnailCacheSize"), 100, err_msg).toInt());
    setImageLoadThreads(database->value(group, QStringLiteral("imageLoadThreads"), 4, err_msg).toInt());
    setLabelBorderWidth(database->value(group, QStringLiteral("labelBorderWidth"), 2, err_msg).toInt());
    setLabelFillOpacity(database->value(group, QStringLiteral("labelFillOpacity"), 30, err_msg).toInt());

    setImageCellScale(database->value(group, QStringLiteral("imageCellScale"), 1.0, err_msg).toDouble());
    setImageCellScaleFrom(database->value(group, QStringLiteral("imageCellScaleFrom"), 0.5, err_msg).toDouble());
    setImageCellScaleTo(database->value(group, QStringLiteral("imageCellScaleTo"), 4.0, err_msg).toDouble());
    setImageCellScaleStepSize(database->value(group, QStringLiteral("imageCellScaleStepSize"), 0.25, err_msg).toDouble());

    setLabelThumbnailScale(database->value(group, QStringLiteral("labelThumbnailScale"), 1.0, err_msg).toDouble());
    setLabelThumbnailAspectRatio(
        database->value(group, QStringLiteral("labelThumbnailAspectRatio"), 1.0, err_msg).toDouble());
    setLabelThumbnailBorderPadding(
        database->value(group, QStringLiteral("labelThumbnailBorderPadding"), 0.1, err_msg).toDouble());

    if (!err_msg.isEmpty())
    {
        spdlog::warn("Load Data settings failed: {}", err_msg.toUtf8().constData());
    }
}

void DataSettings::save(database::SettingsDataBase *database)
{
    if (!database)
    {
        return;
    }

    const QString group = QStringLiteral("Data");

    auto save_value = [database, &group](const QString &key, const QVariant &value) {
        QString err_msg;
        if (!database->setValue(group, key, value, err_msg))
        {
            spdlog::error("Save Data setting {} failed: {}", key.toUtf8().constData(), err_msg.toUtf8().constData());
        }
    };

    save_value(QStringLiteral("thumbnailMargin"), thumbnail_margin_);
    save_value(QStringLiteral("thumbnailCacheSize"), thumbnail_cache_size_);
    save_value(QStringLiteral("imageLoadThreads"), image_load_threads_);
    save_value(QStringLiteral("labelBorderWidth"), label_border_width_);
    save_value(QStringLiteral("labelFillOpacity"), label_fill_opacity_);

    save_value(QStringLiteral("imageCellScale"), image_cell_scale_);
    save_value(QStringLiteral("imageCellScaleFrom"), image_cell_scale_from_);
    save_value(QStringLiteral("imageCellScaleTo"), image_cell_scale_to_);
    save_value(QStringLiteral("imageCellScaleStepSize"), image_cell_scale_step_size_);

    save_value(QStringLiteral("labelThumbnailScale"), label_thumbnail_scale_);
    save_value(QStringLiteral("labelThumbnailAspectRatio"), label_thumbnail_aspect_ratio_);
    save_value(QStringLiteral("labelThumbnailBorderPadding"), label_thumbnail_border_padding_);
}

void DataSettings::reset()
{
    setThumbnailMargin(10);
    setThumbnailCacheSize(100);
    setImageLoadThreads(4);
    setLabelBorderWidth(2);
    setLabelFillOpacity(30);

    setImageCellScale(1.0);
    setImageCellScaleFrom(0.5);
    setImageCellScaleTo(4.0);
    setImageCellScaleStepSize(0.25);

    setLabelThumbnailScale(1.0);
    setLabelThumbnailAspectRatio(1.0);
    setLabelThumbnailBorderPadding(0.1);
}

} // namespace dltool::settings
