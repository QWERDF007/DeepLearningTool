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

void UISettings::setImageBrightness(double value)
{
    value = std::clamp(value, -1.0, 1.0);
    if (image_brightness_ != value)
    {
        image_brightness_ = value;
        emit imageBrightnessChanged();
    }
}

void UISettings::setImageContrast(double value)
{
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
        return;

    {
        QString err_msg;
        const auto row = database->loadImageEnhanceSettings(err_msg);
        if (!err_msg.isEmpty())
            spdlog::warn("Load image enhance settings failed: {}", err_msg.toUtf8().constData());
        setImageBrightness(row.value(QStringLiteral("brightness"), 0.0).toDouble());
        setImageContrast(row.value(QStringLiteral("contrast"), 0.0).toDouble());
    }

    {
        QString err_msg;
        const auto row = database->loadUiSettings(err_msg);
        if (!err_msg.isEmpty())
            spdlog::warn("Load UI settings failed: {}", err_msg.toUtf8().constData());
        setTheme(row.value(QStringLiteral("theme"), QStringLiteral("dark")).toString());
        setLanguage(row.value(QStringLiteral("language"), QStringLiteral("zh_CN")).toString());
    }
}

void UISettings::save(database::SettingsDataBase *database)
{
    if (!database)
        return;

    {
        QString err_msg;
        database->saveImageEnhanceSettings(
            QVariantMap{
                {QStringLiteral("brightness"), image_brightness_},
                {QStringLiteral("contrast"), image_contrast_},
            },
            err_msg);
        if (!err_msg.isEmpty())
            spdlog::error("Save image enhance settings failed: {}", err_msg.toUtf8().constData());
    }

    {
        QString err_msg;
        database->saveUiSettings(
            QVariantMap{
                {QStringLiteral("theme"), theme_},
                {QStringLiteral("language"), language_},
            },
            err_msg);
        if (!err_msg.isEmpty())
            spdlog::error("Save UI settings failed: {}", err_msg.toUtf8().constData());
    }
}

void UISettings::reset()
{
    setImageBrightness(0.0);
    setImageContrast(0.0);
    setTheme("dark");
    setLanguage("zh_CN");
}

} // namespace dltool::settings
