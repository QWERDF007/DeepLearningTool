#pragma once

#include <QAbstractListModel>
#include <QtQml>
#include <map>

namespace dltool::data {
class ProjectDataBase;
}

namespace dltool::project {

class Tag : public QObject
{
public:
    Tag(const int64_t id, const QString &name, const QString &shortcut, QObject *parent = nullptr);
    ~Tag();

    QString name() const
    {
        return name_;
    }

    int64_t id() const
    {
        return id_;
    }

    QString shortcut() const
    {
        return shortcut_;
    }

private:
    int64_t id_;

    QString name_;
    QString shortcut_;
};

class TagsListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS
public:
    TagsListModel(data::ProjectDataBase *database, QObject *parent = nullptr);
    ~TagsListModel();

    enum Role
    {
        TagIdRole = Qt::UserRole + 1,
        NameRole,
        ShortcutRole,
        StatsRole,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    QHash<int, QByteArray> roleNames() const override;

    bool addTag(const QString &name, const QString &shortcut);
    bool updateTag(const int64_t tag_id, const QString &name, const QString &shortcut);
    bool deleteTag(const int64_t tag_id);

    QList<QString> getAllDatasetsName() const;

    int     getTagId(const QString &tag_name) const;
    QString getTagName(const int tag_id) const;

private:
    void init();

    QVariant getTagId(const QModelIndex &index) const;
    QVariant getTagName(const QModelIndex &index) const;
    QVariant getTagShortcut(const QModelIndex &index) const;
    QVariant getTagStats(const QModelIndex &index) const;

    data::ProjectDataBase *database_{nullptr};

    std::map<int64_t, Tag *> tags_;
};

} // namespace dltool::project
