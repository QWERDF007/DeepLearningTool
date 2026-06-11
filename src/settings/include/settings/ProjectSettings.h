#pragma once

#include "dltool/settings/Export.h"

#include <QObject>
#include <QtQml>

namespace dltool::database {
class SettingsDataBase;
}

namespace dltool::settings {

/**
 * @brief 项目相关设置
 *
 * 包含项目管理、工作流等相关的配置
 */
class SETTINGS_API ProjectSettings : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ProjectSettings)
    QML_UNCREATABLE("ProjectSettings is managed by GlobalSettings")

    // 最近项目数量限制
    Q_PROPERTY(int maxRecentProjects READ maxRecentProjects WRITE setMaxRecentProjects NOTIFY maxRecentProjectsChanged)

    // 自动保存间隔（秒）
    Q_PROPERTY(int autoSaveInterval READ autoSaveInterval WRITE setAutoSaveInterval NOTIFY autoSaveIntervalChanged)

    // 是否启用自动保存
    Q_PROPERTY(bool autoSaveEnabled READ autoSaveEnabled WRITE setAutoSaveEnabled NOTIFY autoSaveEnabledChanged)

public:
    explicit ProjectSettings(QObject *parent = nullptr);
    ~ProjectSettings();

    int maxRecentProjects() const
    {
        return max_recent_projects_;
    }

    void setMaxRecentProjects(int value);

    int autoSaveInterval() const
    {
        return auto_save_interval_;
    }

    void setAutoSaveInterval(int value);

    bool autoSaveEnabled() const
    {
        return auto_save_enabled_;
    }

    void setAutoSaveEnabled(bool value);

    /**
     * @brief 从数据库加载设置
     * @param database 设置数据库
     */
    void load(database::SettingsDataBase *database);

    /**
     * @brief 保存设置到数据库
     * @param database 设置数据库
     */
    void save(database::SettingsDataBase *database);

    /**
     * @brief 重置所有设置为默认值
     */
    void reset();

signals:
    void maxRecentProjectsChanged();
    void autoSaveIntervalChanged();
    void autoSaveEnabledChanged();

private:
    int  max_recent_projects_{10};
    int  auto_save_interval_{300}; // 5分钟
    bool auto_save_enabled_{true};
};

} // namespace dltool::settings
