#pragma once

#include <QAbstractListModel>
#include <QtQml>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace dltool::database {
class ProjectDataBase;
} // namespace dltool::database

namespace dltool::data {

class ImageInstancesListModel;
class LabelInstancesListModel;
class ImageLabelsListModel;

class Tag
{
public:
    Tag(int64_t id, QString name)
        : id_(id)
        , name_(std::move(name))
    {
    }

    int64_t id() const
    {
        return id_;
    }

    const QString &name() const
    {
        return name_;
    }

    bool setName(const QString &name)
    {
        if (name_ == name)
        {
            return false;
        }
        name_ = name;
        return true;
    }

    const std::set<int64_t> &imageIds() const
    {
        return image_ids_;
    }

    const std::set<int64_t> &labelIds() const
    {
        return label_ids_;
    }

    void addImageIds(const std::vector<int64_t> &image_ids)
    {
        image_ids_.insert(image_ids.begin(), image_ids.end());
    }

    void removeImageIds(const std::vector<int64_t> &image_ids)
    {
        for (const int64_t image_id : image_ids)
        {
            image_ids_.erase(image_id);
        }
    }

    void addLabelIds(const std::vector<int64_t> &label_ids)
    {
        label_ids_.insert(label_ids.begin(), label_ids.end());
    }

    void removeLabelIds(const std::vector<int64_t> &label_ids)
    {
        for (const int64_t label_id : label_ids)
        {
            label_ids_.erase(label_id);
        }
    }

private:
    int64_t id_{-1};
    QString name_;

    std::set<int64_t> image_ids_;
    std::set<int64_t> label_ids_;
};

class ImageTagsListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ImageTagsModel)
    QML_UNCREATABLE("Can not create ImageTagsModel directly!")
public:
    ImageTagsListModel(dltool::database::ProjectDataBase *database, ImageInstancesListModel *image_instances,
                       LabelInstancesListModel *label_instances, ImageLabelsListModel *image_labels_list,
                       QObject *parent = nullptr);
    ~ImageTagsListModel() override = default;

    enum Role
    {
        TagIdRole = Qt::UserRole + 1,
        NameRole,
        SelectedImagesStatsRole,
        CurrentImageStatsRole,
        SelectedLabelsStatsRole,
    };
    Q_ENUM(Role)

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool addTagClass(const QString &name);
    int64_t findTagClassId(const QString &name) const;
    bool updateTagClass(int64_t tag_id, const QString &name);
    bool deleteTagClass(int64_t tag_id);

    Q_INVOKABLE bool setImagesTag(const std::vector<int64_t> &image_ids, int64_t tag_id);
    Q_INVOKABLE bool setImageTag(int64_t image_id, int64_t tag_id);
    Q_INVOKABLE bool setLabelsTag(const std::vector<int64_t> &label_ids, int64_t tag_id);
    Q_INVOKABLE bool setLabelTag(int64_t label_id, int64_t tag_id);

    /// 为目标添加 Tag；与 set 接口不同，该接口始终保持目标已设置状态，不执行切换。
    bool addLabelsTag(const std::vector<int64_t> &label_ids, int64_t tag_id);

    bool removeImagesTags(const std::vector<int64_t> &image_ids);
    void removeImagesTagsFromMemory(const std::vector<int64_t> &image_ids);
    void addImagesTagsFromMemory(const std::vector<int64_t> &image_ids,
                                 const std::vector<std::vector<int64_t>> &tag_ids);
    void addLabelsTagsFromMemory(const std::vector<int64_t> &label_ids,
                                 const std::vector<std::vector<int64_t>> &tag_ids);

    std::vector<std::vector<int64_t>> getImagesTagIds(const std::vector<int64_t> &image_ids) const;
    void                              applyTagsToLabels();
    void                              updateStats();

    QString getTagClassName(int64_t tag_id) const;

private:
    enum class TagTarget
    {
        Image,
        Label,
    };

    void init();
    bool initTagClasses();
    bool initTagRelations();

    Tag *getTag(int64_t tag_id);
    int  rowForTag(int64_t tag_id) const;

    bool setTags(const std::vector<int64_t> &target_ids, int64_t tag_id, TagTarget target, bool toggle);
    bool removeTags(const std::vector<int64_t> &target_ids, TagTarget target);
    void removeTagsFromMemory(const std::vector<int64_t> &target_ids, TagTarget target);

    static std::vector<int64_t> getUntaggedIds(const std::vector<int64_t> &target_ids,
                                                const std::set<int64_t> &tagged_ids);

    int64_t  getTagClassId(const QModelIndex &index) const;
    QVariant getTagClassName(const QModelIndex &index) const;
    QVariant getSelectedImagesTagStats(const QModelIndex &index) const;
    QVariant getCurrentImageTagStats(const QModelIndex &index) const;
    QVariant getSelectedLabelsTagStats(const QModelIndex &index) const;

    dltool::database::ProjectDataBase *database_{nullptr};
    ImageInstancesListModel           *image_instances_{nullptr};
    LabelInstancesListModel           *label_instances_{nullptr};
    ImageLabelsListModel              *image_labels_list_{nullptr};

    std::map<int64_t, Tag> tags_;
};

} // namespace dltool::data
