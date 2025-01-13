#include "project/Settings.h"

#include "Settings.h"

#include <algorithm>

namespace dltool::project {

Settings::Settings(QObject *parent)
    : QObject(parent)
{
}

Settings::~Settings() {}

void Settings::setImageCellScale(const double scale)
{
    if (image_cell_scale_ == scale)
        return;
    image_cell_scale_ = std::clamp(scale, image_cell_scale_from_, image_cell_scale_to_);
    emit imageCellScaleChanged();
}

void Settings::setImageCellScaleFrom(const double from)
{
    if (image_cell_scale_from_ == from)
        return;
    image_cell_scale_from_ = from;
    emit imageCellScaleFromChanged();
}

void Settings::setImageCellScaleTo(const double to)
{
    if (image_cell_scale_to_ == to)
        return;
    image_cell_scale_to_ = to;
    emit imageCellScaleToChanged();
}

void Settings::setImageCellScaleStep(const double step)
{
    if (step == image_cell_scale_step_)
        return;
    image_cell_scale_step_ = step;
    emit imageCellScaleStepChanged();
}

void Settings::setImageBrightness(const double brightness)
{
    if (image_brightness_ == brightness)
        return;
    image_brightness_ = std::clamp(brightness, -1.0, 1.0);
    emit imageBrightnessChanged();
}

void Settings::setImageContrast(const double contrast)
{
    if (image_contrast_ == contrast)
        return;
    image_contrast_ = std::clamp(contrast, -1.0, 1.0);
    emit imageContrastChanged();
}

} // namespace dltool::project
