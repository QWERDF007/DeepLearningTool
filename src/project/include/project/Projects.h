#pragma once

#include "ProjectExport.h"
#include "common/Singleton.h"
#include "data/DataManager.h"

#include <QAbstractListModel>
#include <QItemSelectionModel>
#include <QObject>

class QQmlApplicationEngine;

namespace dltool::database {
class DataBase;
class ProjectDataBase;
class RecentProjectsDataBase;
} // namespace dltool::database

namespace dltool::project {

class Project : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Project)
    QML_UNCREATABLE("Can not create Project directly!")
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(int method READ method CONSTANT)
    Q_PROPERTY(QString path READ path CONSTANT)
    Q_PROPERTY(QString description READ description NOTIFY descriptionChanged FINAL)
    Q_PROPERTY(QString imageBasePath READ imageBasePath NOTIFY imageBasePathChanged FINAL)
    Q_PROPERTY(data::DataManager *dataManager READ dataManager CONSTANT FINAL)

public:
    Project(const QString &name, const int method, const QString &path, const QString &description,
            const QString image_base_path, const qint64 ctime, const qint64 mtime, QObject *parent = nullptr);
    Project(const QString &path, QObject *parent = nullptr);
    ~Project();

    void initProject();
    void openProject();

    static std::tuple<bool, QString> isValid(const int method, const QString &path, bool is_new);

    QString name() const
    {
        return name_;
    }

    int method() const
    {
        return method_;
    }

    QString path() const
    {
        return path_;
    }

    QString description() const
    {
        return description_;
    }

    QString imageBasePath() const
    {
        return image_base_path_;
    }

    qint64 ctime() const
    {
        return ctime_;
    }

    qint64 mtime() const
    {
        return mtime_;
    }

    data::DataManager *dataManager() const
    {
        return data_manager_;
    }

    /**
     * @brief 设置 QML 引擎引用
     * @param engine QML 应用引擎指针
     */
    void setQmlEngine(QQmlApplicationEngine *engine)
    {
        qml_engine_ = engine;
    }

private:
    void init();

    QString name_;
    int     method_;
    QString path_;
    QString description_;
    QString image_base_path_;
    qint64  ctime_;
    qint64  mtime_;

    dltool::database::ProjectDataBase *database_{nullptr};

    data::DataManager *data_manager_{nullptr};

    QQmlApplicationEngine *qml_engine_{nullptr};

signals:
    void descriptionChanged();
    void imageBasePathChanged();
};

class RectentProjects : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QItemSelectionModel *selection READ selection CONSTANT)
    Q_PROPERTY(QString currentProjectPath READ currentProjectPath NOTIFY currentProjectPathChanged FINAL)
public:
    explicit RectentProjects(const QString &path, QObject *parent = nullptr);
    ~RectentProjects();

    void init();

    struct ProjectBaseInfo
    {
        QString name;
        QString path;
        qint64  mtime{0};
    };

    enum Role
    {
        NameRole = Qt::UserRole + 1,
        PathRole,
        ToolTipRole,
        SelectedRole,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

    QHash<int, QByteArray> roleNames() const override;

    bool insertRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

    bool addProject(const QString &path);

    bool updateProjectBaseInfo(const QString &path, const QString &new_name, const qint64 new_mtime);

    bool openProject(const QString &path);

    bool removeProject(const QString &path);

    QItemSelectionModel *selection() const
    {
        return selection_;
    }

    QString currentProjectPath() const
    {
        return current_path_;
    }

    bool setCurrentProjectPath(const QString &path);

private:
    QString path_;
    QString current_path_;

    dltool::database::RecentProjectsDataBase *database_{nullptr};

    QItemSelectionModel *selection_{nullptr};

    std::vector<ProjectBaseInfo> project_infos;

    QVariant getName(const QModelIndex &index) const;
    QVariant getPath(const QModelIndex &index) const;
    QVariant getTooltip(const QModelIndex &index) const;
    QVariant getSelected(const QModelIndex &index) const;

    void updateSelection(const QItemSelection &selected, const QItemSelection &deselected);
    void onCurrentChanged(const QModelIndex &current, const QModelIndex &previous);

signals:
    void currentProjectPathChanged();
};

class PROJECT_API ProjectManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ProjectManager)
    QT_QML_SINGLETON(ProjectManager)
    Q_PROPERTY(Project *currentProject READ currentProject NOTIFY currentProjectChanged FINAL)
    Q_PROPERTY(RectentProjects *recentProjects READ recentProjects CONSTANT)
public:
    Q_INVOKABLE Project *createProject(const QString &name, const int method, const QString &path, const QString &desc,
                                       const QString image_base_path);

    Q_INVOKABLE Project *openProject(const QString &path);

    Q_INVOKABLE void closeProject();

    Q_INVOKABLE bool updateProjectBaseInfo(const QString &path, const QString &name, const QString &description);

    Q_INVOKABLE void deleteProject(const QString &path);

    Q_INVOKABLE void removeFromRectentProjects(const QString &path);

    Q_INVOKABLE QString isProjectValid(const int method, const QString &path, bool is_new);

    Q_INVOKABLE QString projectSuffix() const
    {
        return ".dlpro";
    }

    Q_INVOKABLE QString projectFileFilter() const
    {
        return "Project files (*.dlpro)";
    }

    Project *currentProject() const
    {
        return current_project_;
    }

    RectentProjects *recentProjects() const
    {
        return recent_projects_;
    }

    Q_INVOKABLE QVariantMap getProjectInfo(const QString &path);
    Q_INVOKABLE QVariantMap getLabelInfo(const QString &path);

    /**
     * @brief 设置 QML 引擎引用，用于注册图像提供器
     * @param engine QML 应用引擎指针
     */
    void setQmlEngine(QQmlApplicationEngine *engine)
    {
        qml_engine_ = engine;
    }

private:
    explicit ProjectManager(QObject *parent = nullptr);
    ~ProjectManager();

    void updateProjectMtime(const QString &path);

    Project         *current_project_{nullptr};
    RectentProjects *recent_projects_{nullptr};

    QQmlApplicationEngine *qml_engine_{nullptr};

signals:
    void currentProjectChanged();
};

} // namespace dltool::project
