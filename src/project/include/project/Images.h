#pragma once

#include <QAbstractListModel>
#include <QtQml>
#include <functional>
#include <map>
#include <unordered_set>

namespace dltool::data {
class ProjectDataBase;
}

namespace dltool::project {

class ImageInstance : public QObject
{
public:
    ImageInstance(const int64_t dataset_id, const int64_t image_id, const QString &path, QObject *parent = nullptr);
    ~ImageInstance();

    int64_t datasetId() const
    {
        return dataset_id_;
    }

    int64_t imageId() const
    {
        return image_id_;
    }

    QString name() const
    {
        return name_;
    }

    QString path() const
    {
        return path_;
    }

    void addTagIds(const std::vector<int64_t> &tag_ids)
    {
        tag_ids_.insert(tag_ids.begin(), tag_ids.end());
    }

    void deleteTagIds(const std::vector<int64_t> &tag_ids)
    {
        for (const auto &tag_id : tag_ids)
        {
            tag_ids_.erase(tag_id);
        }
    }

    std::unordered_set<int64_t> tagIds() const
    {
        return tag_ids_;
    }

private:
    int64_t dataset_id_;

    int64_t image_id_;

    QString path_;

    QString name_;

    std::unordered_set<int64_t> tag_ids_;
};

class ImageInstancesListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ImageInstancesList)
    QML_UNCREATABLE("Can not create ImageInstancesList directly!")
    Q_PROPERTY(QItemSelectionModel *selection READ selection CONSTANT)
    Q_PROPERTY(int curImageId READ getCurImageId NOTIFY curImageIdChanged)
    Q_PROPERTY(QString curImagePath READ curImagePath NOTIFY curImagePathChanged)
    Q_PROPERTY(int count READ count NOTIFY statsChanged)
public:
    ImageInstancesListModel(data::ProjectDataBase *database, QObject *parent = nullptr);
    ~ImageInstancesListModel();

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    enum Role
    {
        ImageIdRole = Qt::UserRole + 1,
        NameRole,
        PathRole,
        SelectedRole,
    };

    QHash<int, QByteArray> roleNames() const override;

    bool addImageInstances(const int64_t dataset_id, const std::vector<QString> &paths);
    bool addImageInstances(const int64_t dataset_id, const QString &image_idr);
    bool deleteImageInstances(const std::vector<int64_t> &image_ids);
    bool deleteImageInstances(const int64_t dataset_id);

    static std::vector<QString> getImagePaths(const QString &image_idr);

    static std::vector<QString> getFiles(const QString &path, const QStringList &name_filters, bool recursive);

    QItemSelectionModel *selection() const
    {
        return selection_;
    }

    Q_INVOKABLE void shiftSelect(int current_index, int previous_index, QItemSelectionModel::SelectionFlags command);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void deleteSelected();

    QVariantMap getImageInstanceInfo(const int64_t image_id);

    QString curImagePath() const;

    int count() const
    {
        return image_instances_model_.size();
    }

    int getCurImageId() const;

    Q_INVOKABLE std::vector<int64_t> getSelectedImagesId() const;

    std::vector<ImageInstance *> getImageInstances(const std::vector<int64_t> &images_id) const;

private:
    void init();

    int getImageId(const QModelIndex &index) const;

    QVariant getImageName(const QModelIndex &index) const;
    QVariant getImagePath(const QModelIndex &index) const;
    QVariant getSelected(const QModelIndex &index) const;

    void updateSelection(const QItemSelection &selected, const QItemSelection &deselected);

    void onCurrentChanged(const QModelIndex &current, const QModelIndex &previous);

    void resetModel();

    data::ProjectDataBase *database_{nullptr};

    /**
     * @brief 图像实例 {image_id, ImageInstance}，按照 image_id 排序
     */
    std::map<int64_t, ImageInstance *, std::greater<int64_t>> image_instances_;

    std::vector<ImageInstance *> image_instances_model_;

    QItemSelectionModel *selection_{nullptr};

signals:
    void statsChanged();
    void curImageIdChanged();
    void curImagePathChanged();
};

} // namespace dltool::project
