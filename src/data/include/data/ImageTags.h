#pragma once

#include <QAbstractListModel>
#include <QtQml>
#include <map>
#include <set>

namespace dltool::data {
class ProjectDataBase;
}

namespace dltool::data {

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

    std::set<int64_t> &imageIds()
    {
        return image_ids_;
    }

    const std::set<int64_t> &imageIds() const
    {
        return image_ids_;
    }

    void addImageIds(const std::vector<int64_t> &image_ids)
    {
        image_ids_.insert(image_ids.begin(), image_ids.end());
    }

    void removeImageIds(const std::vector<int64_t> &image_ids)
    {
        for (const auto &image_id : image_ids)
        {
            image_ids_.erase(image_id);
        }
    }

private:
    int64_t id_;

    QString name_;

    std::set<int64_t> image_ids_;
};

class ImageTagsListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ImageTagsModel)
    QML_UNCREATABLE("Can not create ImageTagsModel directly!")
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
    Q_INVOKABLE bool setImageTag(const int64_t image_id, const int64_t tag_id);

    bool removeImagesTags(const std::vector<int64_t> &image_ids);

    ImageTag *getImageTag(const int64_t tag_id);

    std::vector<std::vector<int64_t>> getImagesTagIds(const std::vector<int64_t> &image_ids) const;

    void updateStats();

private:
    void init();
    bool initTagClass();
    bool initImagesTag();

    int getTagClassId(const QModelIndex &index) const;

    QVariant getTagClassName(const QModelIndex &index) const;
    QVariant getSelectedImagesTagStats(const QModelIndex &index) const;
    QVariant getCurrentImageTagStats(const QModelIndex &index) const;

    std::vector<int64_t> getValidImagesId(const std::vector<int64_t> &new_image_ids, const int64_t tag_id);

    /**
     * @brief 检查指定的图像是否有任何 tag
     * @param image_ids 要检查的图像 ID 列表
     * @return 如果至少有一个图像有 tag 则返回 true，否则返回 false
     */
    bool hasAnyTags(const std::vector<int64_t> &image_ids) const;

    data::ProjectDataBase *database_{nullptr};

    ImageInstancesListModel *image_instances_{nullptr};

    std::map<int64_t, ImageTag *> image_tags_;
};

} // namespace dltool::data
