#pragma once

#include "settings/SettingsExport.h"

#include <QObject>
#include <QSettings>
#include <QtQml>

namespace dltool::settings {

/**
 * @brief 数据相关设置
 * 
 * 包含图像处理、标注显示等相关的配置
 */
class SETTINGS_API DataSettings : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(DataSettings)
    QML_UNCREATABLE("DataSettings is managed by GlobalSettings")

    // 缩略图边距
    Q_PROPERTY(int thumbnailMargin READ thumbnailMargin WRITE setThumbnailMargin NOTIFY thumbnailMarginChanged)

    // 缩略图缓存大小（MB）
    Q_PROPERTY(
        int thumbnailCacheSize READ thumbnailCacheSize WRITE setThumbnailCacheSize NOTIFY thumbnailCacheSizeChanged)

    // 图像加载线程数
    Q_PROPERTY(int imageLoadThreads READ imageLoadThreads WRITE setImageLoadThreads NOTIFY imageLoadThreadsChanged)

    // 标注边框宽度
    Q_PROPERTY(int labelBorderWidth READ labelBorderWidth WRITE setLabelBorderWidth NOTIFY labelBorderWidthChanged)

    // 标注填充透明度 (0-100)
    Q_PROPERTY(int labelFillOpacity READ labelFillOpacity WRITE setLabelFillOpacity NOTIFY labelFillOpacityChanged)

public:
    explicit DataSettings(QObject *parent = nullptr);
    ~DataSettings();

    int thumbnailMargin() const
    {
        return thumbnail_margin_;
    }

    void setThumbnailMargin(int value);

    int thumbnailCacheSize() const
    {
        return thumbnail_cache_size_;
    }

    void setThumbnailCacheSize(int value);

    int imageLoadThreads() const
    {
        return image_load_threads_;
    }

    void setImageLoadThreads(int value);

    int labelBorderWidth() const
    {
        return label_border_width_;
    }

    void setLabelBorderWidth(int value);

    int labelFillOpacity() const
    {
        return label_fill_opacity_;
    }

    void setLabelFillOpacity(int value);

    /**
     * @brief 从 QSettings 加载设置
     * @param settings QSettings 对象
     */
    void load(QSettings *settings);

    /**
     * @brief 保存设置到 QSettings
     * @param settings QSettings 对象
     */
    void save(QSettings *settings);

    /**
     * @brief 重置所有设置为默认值
     */
    void reset();

signals:
    void thumbnailMarginChanged();
    void thumbnailCacheSizeChanged();
    void imageLoadThreadsChanged();
    void labelBorderWidthChanged();
    void labelFillOpacityChanged();

private:
    int thumbnail_margin_{10};
    int thumbnail_cache_size_{100}; // 100MB
    int image_load_threads_{4};
    int label_border_width_{2};
    int label_fill_opacity_{30}; // 30%
};

} // namespace dltool::settings
