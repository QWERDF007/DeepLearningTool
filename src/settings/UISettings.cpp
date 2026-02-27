#include "settings/UISettings.h"

#include <algorithm>

namespace dltool::settings {

UISettings::UISettings(QObject *parent)
    : QObject(parent)
{
}

UISettings::~UISettings() {}

void UISettings::setImageCellScale(double value)
{
    // 验证：缩放值必须在合理范围内
    value = std::clamp(value, 0.1, 10.0);

    if (image_cell_scale_ != value)
    {
        image_cell_scale_ = value;
        emit imageCellScaleChanged();
    }
}

void UISettings::setImageCellScaleFrom(double value)
{
    // 验证：最小缩放值必须大于 0
    value = std::max(0.1, value);

    if (image_cell_scale_from_ != value)
    {
        image_cell_scale_from_ = value;
        emit imageCellScaleFromChanged();
    }
}

void UISettings::setImageCellScaleTo(double value)
{
    // 验证：最大缩放值必须大于最小值
    value = std::max(image_cell_scale_from_ + 0.1, value);

    if (image_cell_scale_to_ != value)
    {
        image_cell_scale_to_ = value;
        emit imageCellScaleToChanged();
    }
}

void UISettings::setImageCellScaleStepSize(double value)
{
    // 验证：步长必须大于 0
    value = std::max(0.01, value);

    if (image_cell_scale_step_size_ != value)
    {
        image_cell_scale_step_size_ = value;
        emit imageCellScaleStepSizeChanged();
    }
}

void UISettings::setImageBrightness(double value)
{
    // 验证：亮度必须在 -1.0 到 1.0 之间
    value = std::clamp(value, -1.0, 1.0);

    if (image_brightness_ != value)
    {
        image_brightness_ = value;
        emit imageBrightnessChanged();
    }
}

void UISettings::setImageContrast(double value)
{
    // 验证：对比度必须在 -1.0 到 1.0 之间
    value = std::clamp(value, -1.0, 1.0);

    if (image_contrast_ != value)
    {
        image_contrast_ = value;
        emit imageContrastChanged();
    }
}

void UISettings::setTheme(const QString &value)
{
    if (theme_ != value)
    {
        theme_ = value;
        emit themeChanged();
    }
}

void UISettings::setLanguage(const QString &value)
{
    if (language_ != value)
    {
        language_ = value;
        emit languageChanged();
    }
}

void UISettings::load(QSettings *settings)
{
    if (!settings)
    {
        return;
    }

    settings->beginGroup("UI");

    setImageCellScale(settings->value("imageCellScale", 1.0).toDouble());
    setImageCellScaleFrom(settings->value("imageCellScaleFrom", 0.5).toDouble());
    setImageCellScaleTo(settings->value("imageCellScaleTo", 4.0).toDouble());
    setImageCellScaleStepSize(settings->value("imageCellScaleStepSize", 0.25).toDouble());

    setImageBrightness(settings->value("imageBrightness", 0.0).toDouble());
    setImageContrast(settings->value("imageContrast", 0.0).toDouble());

    setTheme(settings->value("theme", "dark").toString());
    setLanguage(settings->value("language", "zh_CN").toString());

    settings->endGroup();
}

void UISettings::save(QSettings *settings)
{
    if (!settings)
    {
        return;
    }

    settings->beginGroup("UI");

    settings->setValue("imageCellScale", image_cell_scale_);
    settings->setValue("imageCellScaleFrom", image_cell_scale_from_);
    settings->setValue("imageCellScaleTo", image_cell_scale_to_);
    settings->setValue("imageCellScaleStepSize", image_cell_scale_step_size_);

    settings->setValue("imageBrightness", image_brightness_);
    settings->setValue("imageContrast", image_contrast_);

    settings->setValue("theme", theme_);
    settings->setValue("language", language_);

    settings->endGroup();
}

void UISettings::reset()
{
    setImageCellScale(1.0);
    setImageCellScaleFrom(0.5);
    setImageCellScaleTo(4.0);
    setImageCellScaleStepSize(0.25);

    setImageBrightness(0.0);
    setImageContrast(0.0);

    setTheme("dark");
    setLanguage("zh_CN");
}

} // namespace dltool::settings
