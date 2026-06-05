#pragma once

#include "settings/SettingsExport.h"

#include <QObject>
#include <QtQml>

namespace dltool::database {
class SettingsDataBase;
}

namespace dltool::settings {

class SETTINGS_API DataSettings : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(DataSettings)
    QML_UNCREATABLE("DataSettings is managed by GlobalSettings")

    Q_PROPERTY(int thumbnailMargin READ thumbnailMargin WRITE setThumbnailMargin NOTIFY thumbnailMarginChanged)
    Q_PROPERTY(
        int thumbnailCacheSize READ thumbnailCacheSize WRITE setThumbnailCacheSize NOTIFY thumbnailCacheSizeChanged)
    Q_PROPERTY(int imageLoadThreads READ imageLoadThreads WRITE setImageLoadThreads NOTIFY imageLoadThreadsChanged)

    Q_PROPERTY(int labelBorderWidth READ labelBorderWidth WRITE setLabelBorderWidth NOTIFY labelBorderWidthChanged)
    Q_PROPERTY(int labelFillOpacity READ labelFillOpacity WRITE setLabelFillOpacity NOTIFY labelFillOpacityChanged)

    Q_PROPERTY(double imageCellScale READ imageCellScale WRITE setImageCellScale NOTIFY imageCellScaleChanged)
    Q_PROPERTY(
        double imageCellScaleFrom READ imageCellScaleFrom WRITE setImageCellScaleFrom NOTIFY imageCellScaleFromChanged)
    Q_PROPERTY(double imageCellScaleTo READ imageCellScaleTo WRITE setImageCellScaleTo NOTIFY imageCellScaleToChanged)
    Q_PROPERTY(double imageCellScaleStepSize READ imageCellScaleStepSize WRITE setImageCellScaleStepSize NOTIFY
                   imageCellScaleStepSizeChanged)

    Q_PROPERTY(double labelThumbnailScale READ labelThumbnailScale WRITE setLabelThumbnailScale NOTIFY
                   labelThumbnailScaleChanged)
    Q_PROPERTY(double labelThumbnailScaleFrom READ labelThumbnailScaleFrom CONSTANT)
    Q_PROPERTY(double labelThumbnailScaleTo READ labelThumbnailScaleTo CONSTANT)
    Q_PROPERTY(double labelThumbnailScaleStepSize READ labelThumbnailScaleStepSize CONSTANT)

    Q_PROPERTY(double labelThumbnailAspectRatio READ labelThumbnailAspectRatio WRITE setLabelThumbnailAspectRatio NOTIFY
                   labelThumbnailAspectRatioChanged)
    Q_PROPERTY(double labelThumbnailAspectRatioFrom READ labelThumbnailAspectRatioFrom CONSTANT)
    Q_PROPERTY(double labelThumbnailAspectRatioTo READ labelThumbnailAspectRatioTo CONSTANT)
    Q_PROPERTY(double labelThumbnailAspectRatioStepSize READ labelThumbnailAspectRatioStepSize CONSTANT)

    Q_PROPERTY(double labelThumbnailBorderPadding READ labelThumbnailBorderPadding WRITE setLabelThumbnailBorderPadding
                   NOTIFY labelThumbnailBorderPaddingChanged)
    Q_PROPERTY(double labelThumbnailBorderPaddingFrom READ labelThumbnailBorderPaddingFrom CONSTANT)
    Q_PROPERTY(double labelThumbnailBorderPaddingTo READ labelThumbnailBorderPaddingTo CONSTANT)
    Q_PROPERTY(double labelThumbnailBorderPaddingStepSize READ labelThumbnailBorderPaddingStepSize CONSTANT)

public:
    explicit DataSettings(QObject *parent = nullptr);
    ~DataSettings() override;

    int thumbnailMargin() const
    {
        return thumbnail_.margin;
    }
    void setThumbnailMargin(int value);

    int thumbnailCacheSize() const
    {
        return thumbnail_.cache_size;
    }
    void setThumbnailCacheSize(int value);

    int imageLoadThreads() const
    {
        return thumbnail_.image_load_threads;
    }
    void setImageLoadThreads(int value);

    int labelBorderWidth() const
    {
        return label_display_.border_width;
    }
    void setLabelBorderWidth(int value);

    int labelFillOpacity() const
    {
        return label_display_.fill_opacity;
    }
    void setLabelFillOpacity(int value);

    double imageCellScale() const
    {
        return image_cell_.scale;
    }
    void setImageCellScale(double value);

    double imageCellScaleFrom() const
    {
        return image_cell_.scale_from;
    }
    void setImageCellScaleFrom(double value);

    double imageCellScaleTo() const
    {
        return image_cell_.scale_to;
    }
    void setImageCellScaleTo(double value);

    double imageCellScaleStepSize() const
    {
        return image_cell_.scale_step_size;
    }
    void setImageCellScaleStepSize(double value);

    double labelThumbnailScale() const
    {
        return label_thumbnail_.scale;
    }
    void setLabelThumbnailScale(double value);

    double labelThumbnailScaleFrom() const
    {
        return label_thumbnail_.scale_from;
    }

    double labelThumbnailScaleTo() const
    {
        return label_thumbnail_.scale_to;
    }

    double labelThumbnailScaleStepSize() const
    {
        return label_thumbnail_.scale_step_size;
    }

    double labelThumbnailAspectRatio() const
    {
        return label_thumbnail_.aspect_ratio;
    }
    void setLabelThumbnailAspectRatio(double value);

    double labelThumbnailAspectRatioFrom() const
    {
        return label_thumbnail_.aspect_ratio_from;
    }

    double labelThumbnailAspectRatioTo() const
    {
        return label_thumbnail_.aspect_ratio_to;
    }

    double labelThumbnailAspectRatioStepSize() const
    {
        return label_thumbnail_.aspect_ratio_step_size;
    }

    double labelThumbnailBorderPadding() const
    {
        return label_thumbnail_.border_padding;
    }
    void setLabelThumbnailBorderPadding(double value);

    double labelThumbnailBorderPaddingFrom() const
    {
        return label_thumbnail_.border_padding_from;
    }

    double labelThumbnailBorderPaddingTo() const
    {
        return label_thumbnail_.border_padding_to;
    }

    double labelThumbnailBorderPaddingStepSize() const
    {
        return label_thumbnail_.border_padding_step_size;
    }

    void load(database::SettingsDataBase *database);
    void save(database::SettingsDataBase *database);
    void reset();

signals:
    void thumbnailMarginChanged();
    void thumbnailCacheSizeChanged();
    void imageLoadThreadsChanged();
    void labelBorderWidthChanged();
    void labelFillOpacityChanged();
    void imageCellScaleChanged();
    void imageCellScaleFromChanged();
    void imageCellScaleToChanged();
    void imageCellScaleStepSizeChanged();
    void labelThumbnailScaleChanged();
    void labelThumbnailAspectRatioChanged();
    void labelThumbnailBorderPaddingChanged();

private:
    struct ThumbnailSettings
    {
        int margin{10};
        int cache_size{100};
        int image_load_threads{4};
    };

    struct LabelDisplaySettings
    {
        int border_width{2};
        int fill_opacity{30};
    };

    struct ImageCellSettings
    {
        double scale{1.0};
        double scale_from{0.5};
        double scale_to{4.0};
        double scale_step_size{0.25};
    };

    struct LabelThumbnailSettings
    {
        double scale{1.0};
        double scale_from{0.5};
        double scale_to{4.0};
        double scale_step_size{0.25};
        double aspect_ratio{1.0};
        double aspect_ratio_from{0.5};
        double aspect_ratio_to{2.0};
        double aspect_ratio_step_size{0.1};
        double border_padding{0.1};
        double border_padding_from{0.0};
        double border_padding_to{1.0};
        double border_padding_step_size{0.1};
    };

    ThumbnailSettings      thumbnail_;
    LabelDisplaySettings   label_display_;
    ImageCellSettings      image_cell_;
    LabelThumbnailSettings label_thumbnail_;
};

} // namespace dltool::settings
