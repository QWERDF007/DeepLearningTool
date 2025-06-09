#pragma once

#include <QAbstractListModel>
#include <QtQml>
#include <map>

namespace dltool::data {
class ProjectDataBase;
}

namespace dltool::project {

class ImageTag : public QObject
{
public:
    ImageTag(const int64_t id, const QString &name, QObject *parent = nullptr)
        : QObject(parent)
        , id_(id)
        , name_(name)

    {
    }

    ~ImageTag() {}

    QString name() const
    {
        return name_;
    }

    bool setName(const QString &name)
    {
        if (name_ == name)
            return false;
        name_ = name;
        return true;
    }

    int64_t id() const
    {
        return id_;
    }

private:
    int64_t id_;

    QString name_;
};

class ImageTagsListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS
public:
    ImageTagsListModel(data::ProjectDataBase *database, QObject *parent = nullptr);
    ~ImageTagsListModel();

    enum Role
    {
        TagIdRole = Qt::UserRole + 1,
        NameRole,
        StatsRole,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    QHash<int, QByteArray> roleNames() const override;

    bool addTag(const QString &name);
    bool updateTag(const int64_t tag_id, const QString &name);
    bool deleteTag(const int64_t tag_id);

private:
    void init();

    int getTagId(const QModelIndex &index) const;

    QVariant getTagName(const QModelIndex &index) const;
    QVariant getTagStats(const QModelIndex &index) const;

    data::ProjectDataBase *database_{nullptr};

    std::map<int64_t, ImageTag *> image_tags_;
};

} // namespace dltool::project
