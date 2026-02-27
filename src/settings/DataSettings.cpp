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

    settings->endGroup();
}

void DataSettings::reset()
{
    setThumbnailMargin(10);
    setThumbnailCacheSize(100);
    setImageLoadThreads(4);
    setLabelBorderWidth(2);
    setLabelFillOpacity(30);
}

} // namespace dltool::settings
