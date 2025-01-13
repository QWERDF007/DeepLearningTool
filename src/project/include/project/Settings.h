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
    Q_PROPERTY(double imageCellScaleStep READ imageCellScaleStep WRITE setImageCellScaleStep NOTIFY imageCellScaleStepChanged)
    Q_PROPERTY(double imageBrightness READ imageBrightness WRITE setImageBrightness NOTIFY imageBrightnessChanged)
    Q_PROPERTY(double imageContrast READ imageContrast WRITE setImageContrast NOTIFY imageContrastChanged)
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

    double imageCellScaleStep() const
    {
        return image_cell_scale_step_;
    }

    void setImageCellScaleStep(const double step);

    double imageBrightness() const
    {
        return image_brightness_;
    }

    void setImageBrightness(const double brightness);

    double imageContrast() const
    {
        return image_contrast_;
    }

    void setImageContrast(const double contrast);

private:
    explicit Settings(QObject *parent = nullptr);
    ~Settings();

    double image_cell_scale_{1.0};
    double image_cell_scale_from_{0.25};
    double image_cell_scale_to_{8.0};
    double image_cell_scale_step_{0.25};

    double image_brightness_{0.0};
    double image_contrast_{0.0};

signals:
    void imageCellScaleChanged();
    void imageCellScaleFromChanged();
    void imageCellScaleToChanged();
    void imageCellScaleStepChanged();

    void imageBrightnessChanged();
    void imageContrastChanged();
};

} // namespace dltool::project
