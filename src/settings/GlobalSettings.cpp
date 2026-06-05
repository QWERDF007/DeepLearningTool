#include "settings/GlobalSettings.h"

#include "database/DataBase.h"

#include <spdlog/spdlog.h>

#include <QTimer>

namespace dltool::settings {

namespace {

QString settingsDatabasePath()
{
    return dltool::database::DataBase::applicationDatabasePath(QStringLiteral("settings.db"));
}

} // namespace

GlobalSettings::GlobalSettings(QObject *parent)
    : QObject(parent)
    , project_settings_(new ProjectSettings(this))
    , data_settings_(new DataSettings(this))
    , advanced_settings_(new AdvancedSettings(this))
    , ui_settings_(new UISettings(this))
    , settings_database_(new dltool::database::SettingsDataBase(settingsDatabasePath(), this))
    , save_timer_(new QTimer(this))
    , auto_save_enabled_(true)
{
    // 配置延迟保存定时器
    save_timer_->setSingleShot(true);
    save_timer_->setInterval(1000); // 1秒延迟
    connect(save_timer_, &QTimer::timeout, this, &GlobalSettings::save);

    // 自动加载设置
    try
    {
        load();
        spdlog::info("Settings loaded successfully from: {}", settingsDatabasePath().toUtf8().constData());
    }
    catch (const std::exception &e)
    {
        spdlog::error("Failed to load settings: {}. Using default values.", e.what());
        // 使用默认值
        reset();
    }
    catch (...)
    {
        spdlog::error("Unknown error occurred while loading settings. Using default values.");
        // 使用默认值
        reset();
    }

    // 连接自动保存信号
    connectAutoSave();
}

GlobalSettings::~GlobalSettings()
{
    // 在应用退出时保存设置
    if (auto_save_enabled_)
    {
        try
        {
            save();
            spdlog::info("Settings saved on application exit");
        }
        catch (const std::exception &e)
        {
            spdlog::error("Failed to save settings on exit: {}", e.what());
        }
        catch (...)
        {
            spdlog::error("Unknown error while saving settings on exit");
        }
    }

    // 设置数据库和子设置对象会被 Qt 的父子关系自动删除
}

void GlobalSettings::load()
{
    if (!settings_database_)
    {
        spdlog::error("Cannot load settings: database object is null");
        return;
    }

    try
    {
        // 调用所有子设置的 load 方法
        project_settings_->load(settings_database_);
        data_settings_->load(settings_database_);
        advanced_settings_->load(settings_database_);
        ui_settings_->load(settings_database_);

        spdlog::info("All settings loaded successfully");
    }
    catch (const std::exception &e)
    {
        spdlog::error("Exception while loading settings: {}. Using default values.", e.what());
        reset();
    }
    catch (...)
    {
        spdlog::error("Unknown exception while loading settings. Using default values.");
        reset();
    }
}

void GlobalSettings::save()
{
    if (!settings_database_)
    {
        spdlog::error("Cannot save settings: database object is null");
        return;
    }

    try
    {
        // 调用所有子设置的 save 方法
        project_settings_->save(settings_database_);
        data_settings_->save(settings_database_);
        advanced_settings_->save(settings_database_);
        ui_settings_->save(settings_database_);

        spdlog::info("Settings saved successfully to: {}", settingsDatabasePath().toUtf8().constData());
    }
    catch (const std::exception &e)
    {
        spdlog::error("Exception while saving settings: {}", e.what());
    }
    catch (...)
    {
        spdlog::error("Unknown exception while saving settings");
    }
}

void GlobalSettings::reset()
{
    // 调用所有子设置的 reset 方法
    project_settings_->reset();
    data_settings_->reset();
    advanced_settings_->reset();
    ui_settings_->reset();
}

void GlobalSettings::setAutoSaveEnabled(bool enabled)
{
    auto_save_enabled_ = enabled;
    spdlog::info("Auto-save {}", enabled ? "enabled" : "disabled");
}

bool GlobalSettings::autoSaveEnabled() const
{
    return auto_save_enabled_;
}

void GlobalSettings::scheduleSave()
{
    if (!auto_save_enabled_)
    {
        return;
    }

    // 重启定时器（延迟保存）
    save_timer_->start();
}

void GlobalSettings::connectAutoSave()
{
    // 连接 ProjectSettings 的所有信号
    connect(project_settings_, &ProjectSettings::maxRecentProjectsChanged, this, &GlobalSettings::scheduleSave);
    connect(project_settings_, &ProjectSettings::autoSaveIntervalChanged, this, &GlobalSettings::scheduleSave);
    connect(project_settings_, &ProjectSettings::autoSaveEnabledChanged, this, &GlobalSettings::scheduleSave);

    // 连接 DataSettings 的所有信号
    connect(data_settings_, &DataSettings::thumbnailMarginChanged, this, &GlobalSettings::scheduleSave);
    connect(data_settings_, &DataSettings::thumbnailCacheSizeChanged, this, &GlobalSettings::scheduleSave);
    connect(data_settings_, &DataSettings::imageLoadThreadsChanged, this, &GlobalSettings::scheduleSave);
    connect(data_settings_, &DataSettings::labelBorderWidthChanged, this, &GlobalSettings::scheduleSave);
    connect(data_settings_, &DataSettings::labelFillOpacityChanged, this, &GlobalSettings::scheduleSave);
    connect(data_settings_, &DataSettings::imageCellScaleChanged, this, &GlobalSettings::scheduleSave);
    connect(data_settings_, &DataSettings::imageCellScaleFromChanged, this, &GlobalSettings::scheduleSave);
    connect(data_settings_, &DataSettings::imageCellScaleToChanged, this, &GlobalSettings::scheduleSave);
    connect(data_settings_, &DataSettings::imageCellScaleStepSizeChanged, this, &GlobalSettings::scheduleSave);
    auto *image_search = advanced_settings_->imageSearch();
    connect(image_search, &ImageSearchSettings::enabledChanged, this, &GlobalSettings::scheduleSave);
    connect(image_search, &ImageSearchSettings::modelChanged, this, &GlobalSettings::scheduleSave);
    connect(image_search, &ImageSearchSettings::modelPathChanged, this, &GlobalSettings::scheduleSave);
    connect(image_search, &ImageSearchSettings::featureNameChanged, this, &GlobalSettings::scheduleSave);
    connect(image_search, &ImageSearchSettings::rebuildIndexChanged, this, &GlobalSettings::scheduleSave);
    connect(image_search, &ImageSearchSettings::topKChanged, this, &GlobalSettings::scheduleSave);
    connect(image_search, &ImageSearchSettings::normChanged, this, &GlobalSettings::scheduleSave);
    connect(image_search, &ImageSearchSettings::preprocessBackendChanged, this, &GlobalSettings::scheduleSave);
    connect(image_search, &ImageSearchSettings::faissBackendChanged, this, &GlobalSettings::scheduleSave);
    connect(image_search, &ImageSearchSettings::indexStorageChanged, this, &GlobalSettings::scheduleSave);
    connect(image_search, &ImageSearchSettings::diskBuildBatchSizeChanged, this, &GlobalSettings::scheduleSave);
    connect(image_search, &ImageSearchSettings::modelBatchSizeChanged, this, &GlobalSettings::scheduleSave);
    connect(image_search, &ImageSearchSettings::modelBackendChanged, this, &GlobalSettings::scheduleSave);
    connect(image_search, &ImageSearchSettings::modelDeviceChanged, this, &GlobalSettings::scheduleSave);
    connect(image_search, &ImageSearchSettings::indexDirectoryChanged, this, &GlobalSettings::scheduleSave);
    connect(image_search, &ImageSearchSettings::customFeatureNamesChanged, this, &GlobalSettings::scheduleSave);

    auto *smart_annotation = advanced_settings_->smartAnnotation();
    connect(smart_annotation, &SmartAnnotationSettings::enabledChanged, this, &GlobalSettings::scheduleSave);
    connect(smart_annotation, &SmartAnnotationSettings::modelChanged, this, &GlobalSettings::scheduleSave);
    connect(smart_annotation, &SmartAnnotationSettings::modelPathChanged, this, &GlobalSettings::scheduleSave);
    connect(smart_annotation, &SmartAnnotationSettings::modelBackendChanged, this, &GlobalSettings::scheduleSave);
    connect(smart_annotation, &SmartAnnotationSettings::modelDeviceChanged, this, &GlobalSettings::scheduleSave);
    connect(smart_annotation, &SmartAnnotationSettings::maskThresholdChanged, this, &GlobalSettings::scheduleSave);
    connect(smart_annotation, &SmartAnnotationSettings::polygonSimplifyEpsilonChanged, this,
            &GlobalSettings::scheduleSave);
    connect(smart_annotation, &SmartAnnotationSettings::maskAlphaChanged, this, &GlobalSettings::scheduleSave);
    connect(smart_annotation, &SmartAnnotationSettings::refreshIntervalChanged, this, &GlobalSettings::scheduleSave);

    // 连接 UISettings 的所有信号
    connect(ui_settings_, &UISettings::imageBrightnessChanged, this, &GlobalSettings::scheduleSave);
    connect(ui_settings_, &UISettings::imageContrastChanged, this, &GlobalSettings::scheduleSave);
    connect(ui_settings_, &UISettings::themeChanged, this, &GlobalSettings::scheduleSave);
    connect(ui_settings_, &UISettings::languageChanged, this, &GlobalSettings::scheduleSave);

    spdlog::info("Auto-save connections established");
}

} // namespace dltool::settings
