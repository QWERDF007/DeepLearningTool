#pragma once

#include "common/Singleton.h"
#include "settings/AdvancedSettings.h"
#include "settings/DataSettings.h"
#include "settings/ProjectSettings.h"
#include "settings/SettingsExport.h"
#include "settings/UISettings.h"

#include <QObject>
#include <QTimer>
#include <QtQml>

namespace dltool::database {
class SettingsDataBase;
}

namespace dltool::settings {

/**
 * @brief 全局设置管理器
 *
 * 提供对所有设置的统一访问接口，负责设置的加载和保存
 */
class SETTINGS_API GlobalSettings : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(GlobalSettings)
    QT_QML_SINGLETON(GlobalSettings)

    Q_PROPERTY(ProjectSettings *project READ project CONSTANT)
    Q_PROPERTY(DataSettings *data READ data CONSTANT)
    Q_PROPERTY(AdvancedSettings *advanced READ advanced CONSTANT)
    Q_PROPERTY(UISettings *ui READ ui CONSTANT)

public:
    ProjectSettings *project() const
    {
        return project_settings_;
    }

    DataSettings *data() const
    {
        return data_settings_;
    }

    AdvancedSettings *advanced() const
    {
        return advanced_settings_;
    }

    UISettings *ui() const
    {
        return ui_settings_;
    }

    /**
     * @brief 加载所有设置
     */
    Q_INVOKABLE void load();

    /**
     * @brief 保存所有设置
     */
    Q_INVOKABLE void save();

    /**
     * @brief 重置所有设置为默认值
     */
    Q_INVOKABLE void reset();

    /**
     * @brief 启用自动保存（设置变化时延迟保存）
     * @param enabled 是否启用自动保存
     */
    Q_INVOKABLE void setAutoSaveEnabled(bool enabled);

    /**
     * @brief 获取自动保存是否启用
     */
    Q_INVOKABLE bool autoSaveEnabled() const;

private:
    explicit GlobalSettings(QObject *parent = nullptr);
    ~GlobalSettings();

    /**
     * @brief 延迟保存（避免频繁写入磁盘）
     */
    void scheduleSave();

    /**
     * @brief 连接所有设置的信号以触发自动保存
     */
    void connectAutoSave();

    ProjectSettings *project_settings_;
    DataSettings    *data_settings_;
    AdvancedSettings *advanced_settings_;
    UISettings      *ui_settings_;

    database::SettingsDataBase *settings_database_; // 设置数据库

    QTimer *save_timer_;        // 延迟保存定时器
    bool    auto_save_enabled_; // 是否启用自动保存
};

} // namespace dltool::settings
