#pragma once

#include "common/Singleton.h"

#include <QAbstractListModel>
#include <QItemSelectionModel>
#include <QObject>

namespace dltool::data {
class DataBase;
class ProjectDataBase;
class RecentProjectsDataBase;
} // namespace dltool::data

namespace dltool::project {

class Project : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(int method READ method CONSTANT)
    Q_PROPERTY(QString path READ path CONSTANT)
    Q_PROPERTY(QString description READ description NOTIFY descriptionChanged FINAL)
    Q_PROPERTY(QString imageBasePath READ imageBasePath NOTIFY imageBasePathChanged FINAL)
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

private:
    QString name_;
    int     method_;
    QString path_;
    QString description_;
    QString image_base_path_;
    qint64  ctime_;
    qint64  mtime_;

    data::ProjectDataBase *database_{nullptr};

signals:
    void descriptionChanged();
    void imageBasePathChanged();
};

class RectentProjects : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QItemSelectionModel *selection READ selection CONSTANT)
public:
    explicit RectentProjects(const QString &path, QObject *parent = nullptr);
    ~RectentProjects();

    void init();

    struct ProjectBaseInfo
    {
        QString name;
        QString path;
        qint64  mtime;
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

    bool updateProject(const QString &path, const QString &new_name, const qint64 new_mtime);

    bool openProject(const QString &path);

    bool removeProject(const QString& path);

    QItemSelectionModel *selection() const
    {
        return selection_;
    }

private:
    QString path_;

    data::RecentProjectsDataBase *database_{nullptr};

    QItemSelectionModel *selection_{nullptr};

    std::vector<ProjectBaseInfo> project_infos;

    QVariant getName(const QModelIndex &index) const;
    QVariant getPath(const QModelIndex &index) const;
    QVariant getTooltip(const QModelIndex &index) const;
    QVariant getSelected(const QModelIndex &index) const;

    void updateSelection(const QItemSelection &selected, const QItemSelection &deselected);
};

class ProjectManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ProjectManager)
    QT_QML_SINGLETON(ProjectManager)
    Q_PROPERTY(Project *project READ project NOTIFY projectChanged FINAL)
    Q_PROPERTY(RectentProjects *recentProjects READ recentProjects CONSTANT)
public:
    Q_INVOKABLE Project *createProject(const QString &name, const int method, const QString &path, const QString &desc,
                                       const QString image_base_path);
    Q_INVOKABLE Project *openProject(const QString &path);
    Q_INVOKABLE void     closeProject();
    Q_INVOKABLE bool     updateProjectBaseInfo(const QString &path, const QString &new_name,
                                               const QString &new_description);
    Q_INVOKABLE bool     deleteProject(const QString &path);
    Q_INVOKABLE bool     removeFromRectentProjects(const QString &path);

    Q_INVOKABLE QString isProjectValid(const int method, const QString &path, bool is_new);

    Q_INVOKABLE QString projectSuffix() const
    {
        return ".dlpro";
    }

    Q_INVOKABLE QString projectFileFilter() const
    {
        return "Project files (*.dlpro)";
    }

    Project *project() const
    {
        return current_project_;
    }

    RectentProjects *recentProjects() const
    {
        return recent_projects_;
    }

    Q_INVOKABLE QVariantMap getProjectInfo(const QString &path);

private:
    explicit ProjectManager(QObject *parent = nullptr);
    ~ProjectManager();

    Project         *current_project_{nullptr};
    RectentProjects *recent_projects_{nullptr};

signals:
    void projectChanged();
};

} // namespace dltool::project
