#pragma once

#include <QAbstractListModel>
#include <QRect>
#include <QtQml>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace dltool::database {
class ProjectDataBase;
} // namespace dltool::database

namespace dltool::data {

class DatasetsListModel;
class LabelClassesListModel;
class LabelInstancesListModel;
class ImageInstancesViewModel;

/**
 * @brief 已写入数据库、等待发布到图像源模型的图像实体。
 */
struct LoadedImageInstance
{
    int64_t           dataset_id{-1};         ///< 数据集 ID。
    int64_t           image_id{-1};           ///< 图像 ID。
    QString           path;                   ///< 图像路径。
    int64_t           label_class_id{-1};     ///< 图像级类别 ID。
    std::set<int64_t> tag_ids;                ///< 图像关联的 Tag ID。
};

class ImageInstance : public QObject
{
public:
    ImageInstance(const int64_t dataset_id, const int64_t image_id, const QString &path,
                  const int64_t image_label_class_id = -1, QObject *parent = nullptr);
    ~ImageInstance();

    int64_t datasetId() const
    {
        return dataset_id_;
    }

    void setDatasetId(const int64_t dataset_id)
    {
        dataset_id_ = dataset_id;
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

    int64_t imageLabelClassId() const
    {
        return image_label_class_id_;
    }

    void setImageLabelClassId(const int64_t label_class_id)
    {
        image_label_class_id_ = label_class_id;
    }

    void addTagIds(const std::vector<int64_t> &tag_ids)
    {
        tag_ids_.insert(tag_ids.begin(), tag_ids.end());
    }

    void addTagId(const int64_t tag_id)
    {
        tag_ids_.insert(tag_id);
    }

    void removeTagIds(const std::vector<int64_t> &tag_ids)
    {
        for (const auto &tag_id : tag_ids)
        {
            tag_ids_.erase(tag_id);
        }
    }

    void removeTagId(const int64_t tag_id)
    {
        tag_ids_.erase(tag_id);
    }

    void clearTagIds()
    {
        tag_ids_.clear();
    }

    std::set<int64_t> &tagIds()
    {
        return tag_ids_;
    }

    const std::set<int64_t> &tagIds() const
    {
        return tag_ids_;
    }

    void addLabelIds(const std::vector<int64_t> &label_ids)
    {
        label_ids_.insert(label_ids.begin(), label_ids.end());
    }

    void addLabelId(const int64_t label_id)
    {
        label_ids_.insert(label_id);
    }

    void removeLabelIds(const std::vector<int64_t> &label_ids)
    {
        for (const auto &label_id : label_ids)
        {
            label_ids_.erase(label_id);
        }
    }

    void removeLabelId(const int64_t label_id)
    {
        label_ids_.erase(label_id);
    }

    std::set<int64_t> &labelIds()
    {
        return label_ids_;
    }

    const std::set<int64_t> &labelIds() const
    {
        return label_ids_;
    }

    void setLabelIds(const std::set<int64_t> &label_ids)
    {
        label_ids_ = label_ids;
    }

    bool hasLabels() const
    {
        return !label_ids_.empty() || image_label_class_id_ >= 0;
    }

    QSize imageSize() const;

    QRectF imageRect() const;

private:
    int64_t dataset_id_;

    int64_t image_id_;

    QString path_;

    QString name_;

    int64_t image_label_class_id_{-1};

    std::set<int64_t> tag_ids_;

    std::set<int64_t> label_ids_;

    mutable QSize  image_size_;
    mutable QRectF image_rect_;
};

class ImageInstancesListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ImageInstancesSourceModel)
    QML_UNCREATABLE("Can not create ImageInstancesSourceModel directly!")

public:
    ImageInstancesListModel(dltool::database::ProjectDataBase *database, QObject *parent = nullptr);
    ~ImageInstancesListModel();

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

    enum Role
    {
        ImageIdRole = Qt::UserRole + 1,
        NameRole,
        PathRole,
        SelectedRole,
        IsCurrentRole,
        HasLabelsRole,
        ImageLabelClassIdRole,
        DatasetIdRole,
    };
    Q_ENUM(Role)

    QHash<int, QByteArray> roleNames() const override;

    bool addImages(const int64_t dataset_id, const std::vector<QString> &paths, std::vector<int64_t> &image_ids,
                   bool defer_model_update = false);
    bool addImages(const std::vector<int64_t> &dataset_ids, const std::vector<QString> &paths,
                   std::vector<int64_t> &image_ids, bool defer_model_update = false);
    bool addImages(const int64_t dataset_id, const QString &image_idr, std::vector<int64_t> &image_ids);

    /**
     * @brief 将已经写入数据库的图像发布到内存模型。
     *
     * 这些接口不访问数据库，供后台数据库事务完成后在 GUI 线程批量更新模型。
     */
    void addImagesFromMemory(const int64_t dataset_id, const std::vector<QString> &paths,
                             const std::vector<int64_t> &image_ids, bool defer_model_update = false);
    void addImagesFromMemory(const std::vector<int64_t> &dataset_ids, const std::vector<QString> &paths,
                             const std::vector<int64_t> &image_ids, bool defer_model_update = false);

    /**
     * @brief 发布已经写入数据库的完整图像实体。
     * @param images 待发布图像实体。
     * @param defer_model_update 是否延迟视图通知。
     */
    void addImagesFromMemory(std::vector<LoadedImageInstance> &images, bool defer_model_update = false);

    bool updateImagesDataset(const std::vector<int64_t> &image_ids, const int64_t dataset_id);
    bool updateImagesDataset(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &dataset_ids);
    void updateImagesDatasetFromMemory(const std::vector<int64_t> &image_ids,
                                       const std::vector<int64_t> &dataset_ids, bool notify_model = true);
    void updateImagesDatasetFromMemory(const std::vector<int64_t> &image_ids, int64_t dataset_id,
                                       bool notify_model = true);
    bool deleteImages(const std::vector<int64_t> &image_ids);
    bool deleteImages(const int64_t dataset_id, std::vector<int64_t> &image_ids);

    /**
     * @brief Return the in-memory image IDs belonging to one or more datasets.
     */
    std::vector<int64_t> getImageIdsForDatasets(const std::vector<int64_t> &dataset_ids) const;

    /**
     * @brief Remove already-deleted images from the model without accessing the database.
     *
     * A model reset keeps a large, non-contiguous deletion from generating thousands of
     * removeRows() calls on the GUI thread.
     */
    void removeImagesFromMemory(const std::vector<int64_t> &image_ids);

    static std::vector<QString> getImagePaths(const QString &image_idr);

    static std::vector<QString> getFiles(const QString &path, const QStringList &name_filters, bool recursive);

    ImageInstance *getImageInstance(int64_t image_id) const;

    /**
     * @brief 返回全部图像实体，供 data 模块内部只读遍历。
     * @return 按图像 ID 倒序排列的实体索引。
     */
    const std::map<int64_t, ImageInstance *, std::greater<int64_t>> &getAllImageInstances() const
    {
        return full_image_instances_;
    }

    int totalCount() const
    {
        return static_cast<int>(full_image_instances_.size());
    }

    /**
     * @brief 一次性发布延迟写入内存的图像。
     */
    void refreshModelFromMemory();

    void addImagesLabelIds(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_ids,
                           bool notify_model = true);

    /**
     * @brief 按标注源模型的关系索引同步图像的标注关系。
     * @param image_ids 需要同步的图像 ID。
     * @param label_instances 标注源模型。
     * @param notify_model 是否通知图像角色更新。
     */
    void syncLabelRelations(const std::vector<int64_t> &image_ids,
                            const LabelInstancesListModel *label_instances, bool notify_model = true);

    /**
     * @brief 按标注源模型的关系索引同步全部图像的标注关系。
     * @param label_instances 标注源模型。
     * @param notify_model 是否通知图像角色更新。
     */
    void syncAllLabelRelations(const LabelInstancesListModel *label_instances, bool notify_model = true);

    void deleteImagesLabelIds(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_ids);

    bool                 setImageLabelClassId(const int64_t image_id, const int64_t label_class_id);
    bool                 setImageLabelClassIds(const std::vector<int64_t> &image_ids,
                                               const std::vector<int64_t> &label_class_ids);

    void setImageLabelClassIdsFromMemory(const std::vector<int64_t> &image_ids,
                                         const std::vector<int64_t> &label_class_ids, bool notify_model = true);

    static std::vector<uint8_t> extraDataForImageLabelClassId(int64_t label_class_id);
    static int64_t imageLabelClassIdFromExtraData(const std::vector<uint8_t> &extra_data);

    QString           getImageName(const int64_t image_id) const;
    QString           getImagePath(const int64_t image_id) const;
    int64_t           getImageLabelClassId(const int64_t image_id) const;
    int64_t           getImageDatasetId(const int64_t image_id) const;
    const std::set<int64_t> &getImageTagIds(const int64_t image_id) const;

private:
    void init();
    void rebuildImageIds();

    /**
     * @brief 发布尚未映射到模型行的新增图像。
     *
     * 数据库自增 ID 的正常路径下，新图像总是位于源模型首部，因此只发出一次
     * beginInsertRows/endInsertRows。只有检测到外部写入造成的非单调 ID 时，才回退到重置。
     */
    void publishPendingImages();

    int64_t getImageId(const QModelIndex &index) const;

    /**
     * @brief 将排序后的索引列表合并成连续的范围
     * @param sorted_rows 已排序的行索引列表
     * @return 范围列表，每个范围是 pair<起始行, 数量>
     * @note 例如：[1, 2, 3, 5, 6, 8] -> [(1,3), (5,2), (8,1)]
     */
    std::vector<std::pair<int, int>> mergeConsecutiveRanges(const std::vector<int> &sorted_rows) const;

    /**
     * @brief 根据图像ID列表查找它们在 image_ids_ 中的行索引
     * @param image_ids 要查找的图像ID列表
     * @return 对应的行索引列表（未排序）
     */
    std::vector<int> findRowsByImageIds(const std::vector<int64_t> &image_ids) const;

    QVariant getImageName(const QModelIndex &index) const;
    QVariant getImagePath(const QModelIndex &index) const;
    QVariant getHasLabels(const QModelIndex &index) const;
    QVariant getImageLabelClassId(const QModelIndex &index) const;

    void notifyImageRowsChanged(const std::vector<int64_t> &image_ids, const QList<int> &roles);

    dltool::database::ProjectDataBase *database_{nullptr};

    /**
     * @brief 全部图像实例 {image_id, ImageInstance}，按 image_id 降序保存。
     */
    std::map<int64_t, ImageInstance *, std::greater<int64_t>> full_image_instances_;

    /**
     * @brief 原始图像 ID 顺序。
     *
     * 这是 QAbstractListModel 从行号映射到实体的唯一 ID 列表；筛选和排序
     * 由外层代理模型维护，不在此处复制第二份可见列表。
     */
    std::vector<int64_t> image_ids_;
};

class ImageInfoListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ImageInfoModel)
    QML_UNCREATABLE("Can not create ImageInfoModel directly!")
public:
    ImageInfoListModel(DatasetsListModel *datasets, ImageInstancesViewModel *image_instances,
                       LabelClassesListModel *label_classes, LabelInstancesListModel *label_instances,
                       QObject *parent = nullptr);
    ~ImageInfoListModel();

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    enum Role
    {
        TitleRole = Qt::UserRole + 1,
        ValueRole,
    };

    QHash<int, QByteArray> roleNames() const override;

    void onCurrentImageChanged();

    void updateLabelInfo();

private:
    void resetModel();

    QVariant getTitle(const QModelIndex &index) const;
    QVariant getValue(const QModelIndex &index) const;

    DatasetsListModel       *datasets_{nullptr};
    ImageInstancesViewModel *image_instances_{nullptr};
    LabelClassesListModel   *label_classes_{nullptr};
    LabelInstancesListModel *label_instances_{nullptr};
};
} // namespace dltool::data
