#pragma once

#include <QAbstractListModel>
#include <QItemSelectionModel>
#include <QtQml>
#include <map>
#include <set>
#include <vector>

namespace dltool::database {
class ProjectDataBase;
} // namespace dltool::database

namespace dltool::data {

class ImageInstancesListModel;
struct LoadedImageInstance;

class Dataset : public QObject
{
public:
    Dataset(const int64_t id, const QString &name, QObject *parent = nullptr)
        : QObject(parent)
        , id_(id)
        , name_(name)

    {
    }

    ~Dataset() {}

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

    const std::set<int64_t> &imageIds() const
    {
        return image_ids_;
    }

    std::set<int64_t> &imageIds()
    {
        return image_ids_;
    }

    void addImageIds(const std::vector<int64_t> &image_ids)
    {
        image_ids_.insert(image_ids.begin(), image_ids.end());
    }

    bool addImageId(const int64_t image_id)
    {
        return image_ids_.insert(image_id).second;
    }

    void removeImageIds(const std::vector<int64_t> &image_ids)
    {
        for (const auto &image_id : image_ids)
        {
            image_ids_.erase(image_id);
        }
    }

    bool removeImageId(const int64_t image_id)
    {
        labelled_image_ids_.erase(image_id);
        return image_ids_.erase(image_id) > 0;
    }

    bool setImageLabelled(const int64_t image_id, const bool labelled)
    {
        if (!image_ids_.contains(image_id))
        {
            return false;
        }
        if (labelled)
        {
            return labelled_image_ids_.insert(image_id).second;
        }
        return labelled_image_ids_.erase(image_id) > 0;
    }

    size_t labelledImageCount() const
    {
        return labelled_image_ids_.size();
    }

    void clearImages()
    {
        image_ids_.clear();
        labelled_image_ids_.clear();
    }

private:
    int64_t id_;

    QString name_;

    std::set<int64_t> image_ids_;
    std::set<int64_t> labelled_image_ids_;
};

class DatasetsListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(DatasetsModel)
    QML_UNCREATABLE("Can not create DatasetsModel directly!")
    Q_PROPERTY(QItemSelectionModel *selection READ selection CONSTANT)
    Q_PROPERTY(int lastIndex READ lastIndex WRITE setLastIndex NOTIFY lastSelectedIndexChanged)
public:
    DatasetsListModel(dltool::database::ProjectDataBase *database, QObject *parent = nullptr);
    ~DatasetsListModel();

    enum Role
    {
        DatasetIdRole = Qt::UserRole + 1,
        NameRole,
        StatsRole,
        ProgressRole,
        SelectedRole,
    };
    Q_ENUM(Role)

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

    QHash<int, QByteArray> roleNames() const override;

    bool insertRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

    bool addDataset(const QString &name);
    bool addDatasets(const std::vector<QString> &names, std::vector<int64_t> &dataset_ids);
    bool updateDataset(const int64_t dataset_id, const QString &name);
    bool deleteDataset(const int64_t dataset_id);
    void addDatasetFromMemory(const int64_t dataset_id, const QString &name);
    void addDatasetsFromMemory(const std::vector<int64_t> &dataset_ids, const std::vector<QString> &names);
    void updateDatasetFromMemory(const int64_t dataset_id, const QString &name);
    void removeDatasetsFromMemory(const std::vector<int64_t> &dataset_ids);

    QItemSelectionModel *selection() const
    {
        return selection_;
    }

    Q_INVOKABLE void shiftSelect(int current_index, int previous_index, QItemSelectionModel::SelectionFlags command);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE std::vector<int64_t> getSelectedDatasetIds() const;

    int lastIndex() const
    {
        return last_index_;
    }

    void setLastIndex(int last_index);

    QList<QString>       getAllDatasetsName() const;
    std::vector<int64_t> getAllDatasetIds() const;

    int     getDatasetId(const QString &dataset_name) const;
    QString getDatasetName(const int dataset_id) const;

    /**
     * @brief 将新加入源模型的图像计入数据集统计。
     * @param images 图像源模型。
     * @param image_ids 新图像 ID。
     */
    void addImagesFromSource(const ImageInstancesListModel *images, const std::vector<int64_t> &image_ids);

    /**
     * @brief 将完整图像实体计入数据集统计。
     * @param images 图像源模型。
     * @param loaded_images 已发布图像实体。
     */
    void addImagesFromSource(const ImageInstancesListModel *images,
                             const std::vector<LoadedImageInstance> &loaded_images);

    /**
     * @brief 在删除源图像前移除其数据集统计。
     * @param images 图像源模型。
     * @param image_ids 待删除图像 ID。
     */
    void removeImagesFromSource(const ImageInstancesListModel *images, const std::vector<int64_t> &image_ids);

    /**
     * @brief 在修改图像归属前更新数据集统计。
     * @param images 图像源模型。
     * @param image_ids 待移动图像 ID。
     * @param target_dataset_id 目标数据集 ID。
     */
    void moveImagesFromSource(const ImageInstancesListModel *images, const std::vector<int64_t> &image_ids,
                              int64_t target_dataset_id);

    /**
     * @brief 根据已更新的图像实体同步其“已标注”状态。
     * @param images 图像源模型。
     * @param image_ids 状态可能变化的图像 ID。
     */
    void syncImageLabelState(const ImageInstancesListModel *images, const std::vector<int64_t> &image_ids);

    /**
     * @brief 使用图像源模型重建数据集图像和标注统计。
     * @param images 图像源模型。
     */
    void rebuildImageStats(const ImageInstancesListModel *images);

private:
    void init();

    int getDatasetId(const QModelIndex &index) const;

    QVariant getName(const QModelIndex &index) const;
    QVariant getStats(const QModelIndex &index) const;
    QVariant getProgress(const QModelIndex &index) const;
    QVariant getSelected(const QModelIndex &index) const;

    void onStatsChanged();
    void updateSelection(const QItemSelection &selected, const QItemSelection &deselected);

    dltool::database::ProjectDataBase *database_{nullptr};

    std::map<int64_t, Dataset *> datasets_;

    QItemSelectionModel *selection_{nullptr};
    int                  last_index_{-1};

signals:
    void statsChanged();
    void lastSelectedIndexChanged();
};

} // namespace dltool::data
