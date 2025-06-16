#pragma once

#include <QAbstractListModel>
#include <QtQml>
#include <map>

namespace dltool::data {
class ProjectDataBase;
}

namespace dltool::project {

class ImageInstancesListModel;

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
    ImageTagsListModel(data::ProjectDataBase *database, ImageInstancesListModel *image_instances,
                       QObject *parent = nullptr);
    ~ImageTagsListModel();

    enum Role
    {
        TagIdRole = Qt::UserRole + 1,
        NameRole,
        SelectedImagesStatsRole,
        CurrentImageStatsRole,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    QHash<int, QByteArray> roleNames() const override;

    bool addTagClass(const QString &name);
    bool updateTagClass(const int64_t tag_id, const QString &name);
    bool deleteTagClass(const int64_t tag_id);

    Q_INVOKABLE bool setImagesTag(const std::vector<int64_t> &image_ids, const int64_t tag_id);

private:
    void init();

    int getTagClassId(const QModelIndex &index) const;

    QVariant getTagClassName(const QModelIndex &index) const;
    QVariant getSelectedImagesTagStats(const QModelIndex &index) const;
    QVariant getCurrentImageTagStats(const QModelIndex &index) const;

    data::ProjectDataBase *database_{nullptr};

    ImageInstancesListModel *image_instances_{nullptr};

    std::map<int64_t, ImageTag *> image_tags_;
};

} // namespace dltool::project
