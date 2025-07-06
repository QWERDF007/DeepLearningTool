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
class DatasetsListModel;
class ImageInstancesListModel;
class LabelClassesListModel;
class ImageTagsListModel;
class LabelInstancesListModel;
class ImageLabelsListModel;
class ImageLabelsTableModel;

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
    Q_PROPERTY(DatasetsListModel *datasets READ datasets NOTIFY datasetsChanged FINAL)
    Q_PROPERTY(ImageInstancesListModel *imageInstances READ imageInstances NOTIFY imageInstancesChanged FINAL)
    Q_PROPERTY(LabelClassesListModel *labelClasses READ labelClasses NOTIFY labelClassesChanged FINAL)
    Q_PROPERTY(ImageTagsListModel *imageTags READ imageTags NOTIFY imageTagsChanged FINAL)
    Q_PROPERTY(ImageLabelsListModel *imageLabelsList READ imageLabelsList CONSTANT)
    Q_PROPERTY(ImageLabelsTableModel *imageLabelsTable READ imageLabelsTable CONSTANT)
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

    DatasetsListModel *datasets() const
    {
        return datasets_;
    }

    ImageInstancesListModel *imageInstances() const
    {
        return image_instances_;
    }

    LabelClassesListModel *labelClasses() const
    {
        return label_classes_;
    }

    ImageTagsListModel *imageTags() const
    {
        return image_tags_;
    }

    ImageLabelsListModel *imageLabelsList() const
    {
        return image_labels_list_;
    }

    ImageLabelsTableModel *imageLabelsTable() const
    {
        return image_labels_table_;
    }

    Q_INVOKABLE QList<QString> getAllDatasetsName() const;

    Q_INVOKABLE int     getDatasetId(const QString &dataset_name) const;
    Q_INVOKABLE QString getDatasetName(const int dataset_id) const;

    Q_INVOKABLE void addDataset(const QString &name);
    Q_INVOKABLE void updateDataset(const int64_t dataset_id, const QString &name);
    Q_INVOKABLE void deleteDataset(const int64_t dataset_id);

    Q_INVOKABLE void importData(const int64_t dataset_id, const int data_format, const QString &image_dir,
                                const QString &data_dir);

    Q_INVOKABLE void deleteSelectedImages();

    Q_INVOKABLE QVariantMap getImageInstanceInfo(const int64_t image_id);

    Q_INVOKABLE void addLabelClass(const QString &name, const QString &color, const QString &shortcut);
    Q_INVOKABLE void updateLabelClass(const int64_t label_class_id, const QString &name, const QString &color,
                                      const QString &shortcut, const int64_t ordinal_index);
    Q_INVOKABLE void deleteLabelClass(const int64_t label_class_id);

    Q_INVOKABLE void addLabels(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_class_ids,
                               const std::vector<QVariantMap> &data);
    Q_INVOKABLE void updateLabels(const std::vector<int64_t> &label_ids, const std::vector<QVariantMap> &data);
    Q_INVOKABLE void deleteLabels(const std::vector<int64_t> &label_ids);

    Q_INVOKABLE void addTagClass(const QString &name);

private:
    void init();

    QString name_;
    int     method_;
    QString path_;
    QString description_;
    QString image_base_path_;
    qint64  ctime_;
    qint64  mtime_;

    data::ProjectDataBase *database_{nullptr};

    DatasetsListModel       *datasets_{nullptr};
    ImageInstancesListModel *image_instances_{nullptr};
    LabelClassesListModel   *label_classes_{nullptr};
    ImageTagsListModel      *image_tags_{nullptr};
    LabelInstancesListModel *label_instances_{nullptr};
    ImageLabelsListModel    *image_labels_list_{nullptr};
    ImageLabelsTableModel   *image_labels_table_{nullptr};

signals:
    void descriptionChanged();
    void imageBasePathChanged();
    void datasetsChanged();
    void imageInstancesChanged();
    void labelClassesChanged();
    void imageTagsChanged();
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

    data::RecentProjectsDataBase *database_{nullptr};

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

class ProjectManager : public QObject
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

private:
    explicit ProjectManager(QObject *parent = nullptr);
    ~ProjectManager();

    void updateProjectMtime(const QString &path);

    Project         *current_project_{nullptr};
    RectentProjects *recent_projects_{nullptr};

signals:
    void currentProjectChanged();
};

} // namespace dltool::project
