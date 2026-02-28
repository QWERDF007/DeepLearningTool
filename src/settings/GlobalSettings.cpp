#include "settings/GlobalSettings.h"

#include <spdlog/spdlog.h>

#include <QCoreApplication>
#include <QTimer>

namespace dltool::settings {

GlobalSettings::GlobalSettings(QObject *parent)
    : QObject(parent)
    , project_settings_(new ProjectSettings(this))
    , data_settings_(new DataSettings(this))
    , ui_settings_(new UISettings(this))
    , qsettings_(nullptr)
    , save_timer_(new QTimer(this))
    , auto_save_enabled_(true)
{
    // 初始化 QSettings 对象
    // 使用应用程序的组织名称和应用程序名称
    qsettings_ = new QSettings(QCoreApplication::organizationName(), QCoreApplication::applicationName(), this);

    // 配置延迟保存定时器
    save_timer_->setSingleShot(true);
    save_timer_->setInterval(1000); // 1秒延迟
    connect(save_timer_, &QTimer::timeout, this, &GlobalSettings::save);

    // 自动加载设置
    try
    {
        load();
        spdlog::info("Settings loaded successfully from: {}", qsettings_->fileName().toStdString());
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

    // QSettings 会被 Qt 的父子关系自动删除
    // 子设置对象也会被自动删除
}

void GlobalSettings::load()
{
    if (!qsettings_)
    {
        spdlog::error("Cannot load settings: QSettings object is null");
        return;
    }

    // 检查配置文件状态
    QSettings::Status status = qsettings_->status();
    if (status != QSettings::NoError)
    {
        if (status == QSettings::AccessError)
        {
            spdlog::warn(
                "Settings file access error. File may not exist yet or is not accessible. Using default values.");
        }
        else if (status == QSettings::FormatError)
        {
            spdlog::error("Settings file format error. File may be corrupted. Using default values.");
            // 重置为默认值
            reset();
            return;
        }
    }

    try
    {
        // 调用所有子设置的 load 方法
        project_settings_->load(qsettings_);
        data_settings_->load(qsettings_);
        ui_settings_->load(qsettings_);

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
    if (!qsettings_)
    {
        spdlog::error("Cannot save settings: QSettings object is null");
        return;
    }

    try
    {
        // 调用所有子设置的 save 方法
        project_settings_->save(qsettings_);
        data_settings_->save(qsettings_);
        ui_settings_->save(qsettings_);

        // 确保立即写入磁盘
        qsettings_->sync();

        // 检查写入状态
        QSettings::Status status = qsettings_->status();
        if (status != QSettings::NoError)
        {
            if (status == QSettings::AccessError)
            {
                spdlog::error("Failed to save settings: Access error. Check file permissions.");
            }
            else if (status == QSettings::FormatError)
            {
                spdlog::error("Failed to save settings: Format error.");
            }
        }
        else
        {
            spdlog::info("Settings saved successfully to: {}", qsettings_->fileName().toStdString());
        }
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

    // 连接 UISettings 的所有信号
    connect(ui_settings_, &UISettings::imageBrightnessChanged, this, &GlobalSettings::scheduleSave);
    connect(ui_settings_, &UISettings::imageContrastChanged, this, &GlobalSettings::scheduleSave);
    connect(ui_settings_, &UISettings::themeChanged, this, &GlobalSettings::scheduleSave);
    connect(ui_settings_, &UISettings::languageChanged, this, &GlobalSettings::scheduleSave);

    spdlog::info("Auto-save connections established");
}

} // namespace dltool::settings
