#pragma once

#include <QAbstractListModel>
#include <QtQml>
#include <map>
#include <set>

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

    std::set<int64_t> &imagesId()
    {
        return images_id_;
    }

    const std::set<int64_t> &imagesId() const
    {
        return images_id_;
    }

    void addImageId(const std::vector<int64_t> &image_ids)
    {
        images_id_.insert(image_ids.begin(), image_ids.end());
    }

    void removeImageId(const std::vector<int64_t> &image_ids)
    {
        for (const auto &image_id : image_ids)
        {
            images_id_.erase(image_id);
        }
    }

private:
    int64_t id_;

    QString name_;

    std::set<int64_t> images_id_;
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

    Q_INVOKABLE bool setImagesTag(const std::vector<int64_t> &images_id, const int64_t tag_id);
    Q_INVOKABLE bool setImagesTag(const int64_t image_id, const int64_t tag_id);

    bool removeImagesTags(const std::vector<int64_t> &images_id);

    ImageTag *getImageTag(const int64_t tag_id);

private:
    void init();
    bool initTagClass();
    bool initImagesTag();

    int getTagClassId(const QModelIndex &index) const;

    QVariant getTagClassName(const QModelIndex &index) const;
    QVariant getSelectedImagesTagStats(const QModelIndex &index) const;
    QVariant getCurrentImageTagStats(const QModelIndex &index) const;

    std::vector<int64_t> getValidImagesId(const std::vector<int64_t> &new_images_id, const int64_t tag_id);

    void updateStats();

    data::ProjectDataBase *database_{nullptr};

    ImageInstancesListModel *image_instances_{nullptr};

    std::map<int64_t, ImageTag *> image_tags_;
};

} // namespace dltool::project
