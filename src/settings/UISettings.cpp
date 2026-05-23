#include "settings/UISettings.h"

#include "database/DataBase.h"

#include <spdlog/spdlog.h>

#include <algorithm>

namespace dltool::settings {

UISettings::UISettings(QObject *parent)
    : QObject(parent)
{
}

UISettings::~UISettings() {}

// DEPRECATED: imageCellScale 方法已迁移到 DataSettings
// void UISettings::setImageCellScale(double value)
// {
//     // 验证：缩放值必须在合理范围内
//     value = std::clamp(value, 0.1, 10.0);

//     if (image_cell_scale_ != value)
//     {
//         image_cell_scale_ = value;
//         emit imageCellScaleChanged();
//     }
// }

// void UISettings::setImageCellScaleFrom(double value)
// {
//     // 验证：最小缩放值必须大于 0
//     value = std::max(0.1, value);

//     if (image_cell_scale_from_ != value)
//     {
//         image_cell_scale_from_ = value;
//         emit imageCellScaleFromChanged();
//     }
// }

// void UISettings::setImageCellScaleTo(double value)
// {
//     // 验证：最大缩放值必须大于最小值
//     value = std::max(image_cell_scale_from_ + 0.1, value);

//     if (image_cell_scale_to_ != value)
//     {
//         image_cell_scale_to_ = value;
//         emit imageCellScaleToChanged();
//     }
// }

// void UISettings::setImageCellScaleStepSize(double value)
// {
//     // 验证：步长必须大于 0
//     value = std::max(0.01, value);

//     if (image_cell_scale_step_size_ != value)
//     {
//         image_cell_scale_step_size_ = value;
//         emit imageCellScaleStepSizeChanged();
//     }
// }

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

void UISettings::load(database::SettingsDataBase *database)
{
    if (!database)
    {
        return;
    }

    const QString group = QStringLiteral("UI");
    QString       err_msg;

    // DEPRECATED: imageCellScale 已迁移到 DataSettings，不再从 UI 组加载
    // setImageCellScale(database->value(group, QStringLiteral("imageCellScale"), 1.0, err_msg).toDouble());
    // setImageCellScaleFrom(database->value(group, QStringLiteral("imageCellScaleFrom"), 0.5, err_msg).toDouble());
    // setImageCellScaleTo(database->value(group, QStringLiteral("imageCellScaleTo"), 4.0, err_msg).toDouble());
    // setImageCellScaleStepSize(database->value(group, QStringLiteral("imageCellScaleStepSize"), 0.25,
    // err_msg).toDouble());

    setImageBrightness(database->value(group, QStringLiteral("imageBrightness"), 0.0, err_msg).toDouble());
    setImageContrast(database->value(group, QStringLiteral("imageContrast"), 0.0, err_msg).toDouble());

    setTheme(database->value(group, QStringLiteral("theme"), QStringLiteral("dark"), err_msg).toString());
    setLanguage(database->value(group, QStringLiteral("language"), QStringLiteral("zh_CN"), err_msg).toString());

    if (!err_msg.isEmpty())
    {
        spdlog::warn("Load UI settings failed: {}", err_msg.toUtf8().constData());
    }
}

void UISettings::save(database::SettingsDataBase *database)
{
    if (!database)
    {
        return;
    }

    const QString group = QStringLiteral("UI");

    auto save_value = [database, &group](const QString &key, const QVariant &value) {
        QString err_msg;
        if (!database->setValue(group, key, value, err_msg))
        {
            spdlog::error("Save UI setting {} failed: {}", key.toUtf8().constData(), err_msg.toUtf8().constData());
        }
    };

    // DEPRECATED: imageCellScale 已迁移到 DataSettings，不再保存到 UI 组
    // save_value(QStringLiteral("imageCellScale"), image_cell_scale_);
    // save_value(QStringLiteral("imageCellScaleFrom"), image_cell_scale_from_);
    // save_value(QStringLiteral("imageCellScaleTo"), image_cell_scale_to_);
    // save_value(QStringLiteral("imageCellScaleStepSize"), image_cell_scale_step_size_);

    save_value(QStringLiteral("imageBrightness"), image_brightness_);
    save_value(QStringLiteral("imageContrast"), image_contrast_);

    save_value(QStringLiteral("theme"), theme_);
    save_value(QStringLiteral("language"), language_);
}

void UISettings::reset()
{
    // DEPRECATED: imageCellScale 已迁移到 DataSettings
    // setImageCellScale(1.0);
    // setImageCellScaleFrom(0.5);
    // setImageCellScaleTo(4.0);
    // setImageCellScaleStepSize(0.25);

    setImageBrightness(0.0);
    setImageContrast(0.0);

    setTheme("dark");
    setLanguage("zh_CN");
}

} // namespace dltool::settings
