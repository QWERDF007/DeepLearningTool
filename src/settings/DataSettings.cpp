#include "settings/DataSettings.h"

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

void DataSettings::load(QSettings *settings)
{
    if (!settings)
    {
        return;
    }

    settings->beginGroup("Data");

    setThumbnailMargin(settings->value("thumbnailMargin", 10).toInt());
    setThumbnailCacheSize(settings->value("thumbnailCacheSize", 100).toInt());
    setImageLoadThreads(settings->value("imageLoadThreads", 4).toInt());
    setLabelBorderWidth(settings->value("labelBorderWidth", 2).toInt());
    setLabelFillOpacity(settings->value("labelFillOpacity", 30).toInt());

    setImageCellScale(settings->value("imageCellScale", 1.0).toDouble());
    setImageCellScaleFrom(settings->value("imageCellScaleFrom", 0.5).toDouble());
    setImageCellScaleTo(settings->value("imageCellScaleTo", 4.0).toDouble());
    setImageCellScaleStepSize(settings->value("imageCellScaleStepSize", 0.25).toDouble());

    settings->endGroup();
}

void DataSettings::save(QSettings *settings)
{
    if (!settings)
    {
        return;
    }

    settings->beginGroup("Data");

    settings->setValue("thumbnailMargin", thumbnail_margin_);
    settings->setValue("thumbnailCacheSize", thumbnail_cache_size_);
    settings->setValue("imageLoadThreads", image_load_threads_);
    settings->setValue("labelBorderWidth", label_border_width_);
    settings->setValue("labelFillOpacity", label_fill_opacity_);

    settings->setValue("imageCellScale", image_cell_scale_);
    settings->setValue("imageCellScaleFrom", image_cell_scale_from_);
    settings->setValue("imageCellScaleTo", image_cell_scale_to_);
    settings->setValue("imageCellScaleStepSize", image_cell_scale_step_size_);

    settings->endGroup();
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
}

} // namespace dltool::settings
