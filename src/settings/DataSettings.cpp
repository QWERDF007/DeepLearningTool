#include "settings/DataSettings.h"

#include "database/DataBase.h"

#include <spdlog/spdlog.h>

#include <algorithm>

namespace dltool::settings {

DataSettings::DataSettings(QObject *parent)
    : QObject(parent)
{
}

DataSettings::~DataSettings() = default;

void DataSettings::setThumbnailMargin(int value)
{
    value = std::max(0, value);
    if (thumbnail_.margin != value)
    {
        thumbnail_.margin = value;
        emit thumbnailMarginChanged();
    }
}

void DataSettings::setThumbnailCacheSize(int value)
{
    value = std::max(1, value);
    if (thumbnail_.cache_size != value)
    {
        thumbnail_.cache_size = value;
        emit thumbnailCacheSizeChanged();
    }
}

void DataSettings::setImageLoadThreads(int value)
{
    value = std::clamp(value, 1, 16);
    if (thumbnail_.image_load_threads != value)
    {
        thumbnail_.image_load_threads = value;
        emit imageLoadThreadsChanged();
    }
}

void DataSettings::setLabelBorderWidth(int value)
{
    value = std::max(1, value);
    if (label_display_.border_width != value)
    {
        label_display_.border_width = value;
        emit labelBorderWidthChanged();
    }
}

void DataSettings::setLabelFillOpacity(int value)
{
    value = std::clamp(value, 0, 100);
    if (label_display_.fill_opacity != value)
    {
        label_display_.fill_opacity = value;
        emit labelFillOpacityChanged();
    }
}

void DataSettings::setImageCellScale(double value)
{
    value = std::clamp(value, 0.1, 10.0);
    if (image_cell_.scale != value)
    {
        image_cell_.scale = value;
        emit imageCellScaleChanged();
    }
}

void DataSettings::setImageCellScaleFrom(double value)
{
    value = std::max(0.1, value);
    if (image_cell_.scale_from != value)
    {
        image_cell_.scale_from = value;
        emit imageCellScaleFromChanged();
    }
}

void DataSettings::setImageCellScaleTo(double value)
{
    value = std::max(image_cell_.scale_from + 0.1, value);
    if (image_cell_.scale_to != value)
    {
        image_cell_.scale_to = value;
        emit imageCellScaleToChanged();
    }
}

void DataSettings::setImageCellScaleStepSize(double value)
{
    value = std::max(0.01, value);
    if (image_cell_.scale_step_size != value)
    {
        image_cell_.scale_step_size = value;
        emit imageCellScaleStepSizeChanged();
    }
}

void DataSettings::setLabelThumbnailScale(double value)
{
    value = std::clamp(value, 0.5, 4.0);
    if (label_thumbnail_.scale != value)
    {
        label_thumbnail_.scale = value;
        emit labelThumbnailScaleChanged();
    }
}

void DataSettings::setLabelThumbnailAspectRatio(double value)
{
    value = std::clamp(value, 0.5, 2.0);
    if (label_thumbnail_.aspect_ratio != value)
    {
        label_thumbnail_.aspect_ratio = value;
        emit labelThumbnailAspectRatioChanged();
    }
}

void DataSettings::setLabelThumbnailBorderPadding(double value)
{
    value = std::clamp(value, 0.0, 1.0);
    if (label_thumbnail_.border_padding != value)
    {
        label_thumbnail_.border_padding = value;
        emit labelThumbnailBorderPaddingChanged();
    }
}

void DataSettings::load(database::SettingsDataBase *database)
{
    if (!database)
    {
        return;
    }

    {
        QString err_msg;
        const auto row = database->loadThumbnailSettings(err_msg);
        if (!err_msg.isEmpty())
        {
            spdlog::warn("Load thumbnail settings failed: {}", err_msg.toUtf8().constData());
        }

        setThumbnailMargin(row.value(QStringLiteral("margin"), 10).toInt());
        setThumbnailCacheSize(row.value(QStringLiteral("cache_size"), 100).toInt());
        setImageLoadThreads(row.value(QStringLiteral("image_load_threads"), 4).toInt());
        setImageCellScale(row.value(QStringLiteral("cell_scale"), 1.0).toDouble());
        setImageCellScaleFrom(row.value(QStringLiteral("cell_scale_from"), 0.5).toDouble());
        setImageCellScaleTo(row.value(QStringLiteral("cell_scale_to"), 4.0).toDouble());
        setImageCellScaleStepSize(row.value(QStringLiteral("cell_scale_step"), 0.25).toDouble());
        setLabelThumbnailScale(row.value(QStringLiteral("label_scale"), 1.0).toDouble());
        setLabelThumbnailAspectRatio(row.value(QStringLiteral("label_aspect_ratio"), 1.0).toDouble());
        setLabelThumbnailBorderPadding(row.value(QStringLiteral("label_border_padding"), 0.1).toDouble());
    }

    {
        QString err_msg;
        const auto row = database->loadLabelDisplaySettings(err_msg);
        if (!err_msg.isEmpty())
        {
            spdlog::warn("Load label display settings failed: {}", err_msg.toUtf8().constData());
        }

        setLabelBorderWidth(row.value(QStringLiteral("border_width"), 2).toInt());
        setLabelFillOpacity(row.value(QStringLiteral("fill_opacity"), 30).toInt());
    }
}

void DataSettings::save(database::SettingsDataBase *database)
{
    if (!database)
    {
        return;
    }

    {
        QString err_msg;
        database->saveThumbnailSettings(
            QVariantMap{
                {              QStringLiteral("margin"), thumbnail_.margin},
                {          QStringLiteral("cache_size"), thumbnail_.cache_size},
                {  QStringLiteral("image_load_threads"), thumbnail_.image_load_threads},
                {          QStringLiteral("cell_scale"), image_cell_.scale},
                {     QStringLiteral("cell_scale_from"), image_cell_.scale_from},
                {       QStringLiteral("cell_scale_to"), image_cell_.scale_to},
                {       QStringLiteral("cell_scale_step"), image_cell_.scale_step_size},
                {         QStringLiteral("label_scale"), label_thumbnail_.scale},
                {  QStringLiteral("label_aspect_ratio"), label_thumbnail_.aspect_ratio},
                {QStringLiteral("label_border_padding"), label_thumbnail_.border_padding},
            },
            err_msg);
        if (!err_msg.isEmpty())
        {
            spdlog::error("Save thumbnail settings failed: {}", err_msg.toUtf8().constData());
        }
    }

    {
        QString err_msg;
        database->saveLabelDisplaySettings(
            QVariantMap{
                { QStringLiteral("border_width"), label_display_.border_width},
                {QStringLiteral("fill_opacity"), label_display_.fill_opacity},
            },
            err_msg);
        if (!err_msg.isEmpty())
        {
            spdlog::error("Save label display settings failed: {}", err_msg.toUtf8().constData());
        }
    }
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
