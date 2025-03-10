#pragma once

#include "common/Singleton.h"

namespace dltool::project {

class Settings : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Settings)
    QT_QML_SINGLETON(Settings)
    // clang-format off
    Q_PROPERTY(double imageCellScale READ imageCellScale WRITE setImageCellScale NOTIFY imageCellScaleChanged)
    Q_PROPERTY(double imageCellScaleFrom READ imageCellScaleFrom WRITE setImageCellScaleFrom NOTIFY imageCellScaleFromChanged)
    Q_PROPERTY(double imageCellScaleTo READ imageCellScaleTo WRITE setImageCellScaleTo NOTIFY imageCellScaleToChanged)
    Q_PROPERTY(double imageCellScaleStepSize READ imageCellScaleStepSize WRITE setImageCellScaleStepSize NOTIFY imageCellScaleStepSizeChanged)

    Q_PROPERTY(double imageBrightness READ imageBrightness WRITE setImageBrightness NOTIFY imageBrightnessChanged)
    Q_PROPERTY(double imageBrightnessFrom READ imageBrightnessFrom CONSTANT)
    Q_PROPERTY(double imageBrightnessTo READ imageBrightnessTo CONSTANT)
    Q_PROPERTY(double imageBrightnessStepSize READ imageBrightnessStepSize CONSTANT)

    Q_PROPERTY(double imageContrast READ imageContrast WRITE setImageContrast NOTIFY imageContrastChanged)
    Q_PROPERTY(double imageContrastFrom READ imageContrastFrom CONSTANT)
    Q_PROPERTY(double imageContrastTo READ imageContrastTo CONSTANT)
    Q_PROPERTY(double imageContrastStepSize READ imageContrastStepSize CONSTANT)
    // clang-format on
public:
    double imageCellScale() const
    {
        return image_cell_scale_;
    }

    void setImageCellScale(const double scale);

    double imageCellScaleFrom() const
    {
        return image_cell_scale_from_;
    }

    void setImageCellScaleFrom(const double from);

    double imageCellScaleTo() const
    {
        return image_cell_scale_to_;
    }

    void setImageCellScaleTo(const double to);

    double imageCellScaleStepSize() const
    {
        return image_cell_scale_step_size_;
    }

    void setImageCellScaleStepSize(const double step_size);

    double imageBrightness() const
    {
        return image_brightness_;
    }

    void setImageBrightness(const double brightness);

    double imageBrightnessFrom() const
    {
        return image_brightness_from_;
    }

    double imageBrightnessTo() const
    {
        return image_brightness_to_;
    }

    double imageBrightnessStepSize() const
    {
        return image_brightness_step_size_;
    }

    double imageContrast() const
    {
        return image_contrast_;
    }

    double imageContrastFrom() const
    {
        return image_contrast_from_;
    }

    double imageContrastTo() const
    {
        return image_contrast_to_;
    }

    double imageContrastStepSize() const
    {
        return image_contrast_step_size_;
    }

    void setImageContrast(const double contrast);

private:
    explicit Settings(QObject *parent = nullptr);
    ~Settings();

    double image_cell_scale_{1.0};
    double image_cell_scale_from_{0.25};
    double image_cell_scale_to_{4.0};
    double image_cell_scale_step_size_{0.25};

    double image_brightness_{0.0};
    double image_brightness_from_{-1.0};
    double image_brightness_to_{1.0};
    double image_brightness_step_size_{0.1};

    double image_contrast_{0.0};
    double image_contrast_from_{-1.0};
    double image_contrast_to_{1.0};
    double image_contrast_step_size_{0.1};

signals:
    void imageCellScaleChanged();
    void imageCellScaleFromChanged();
    void imageCellScaleToChanged();
    void imageCellScaleStepSizeChanged();

    void imageBrightnessChanged();
    void imageBrightnessFromChanged();
    void imageBrightnessToChanged();
    void imageBrightnessStepSizeChanged();

    void imageContrastChanged();
    void imageContrastFromChanged();
    void imageContrastToChanged();
    void imageContrastStepSizeChanged();
};

} // namespace dltool::project
