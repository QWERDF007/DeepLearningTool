#pragma once

#include <QAbstractListModel>
#include <QRect>
#include <QtQml>
#include <functional>
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

    void removeTagIds(const std::vector<int64_t> &tag_ids)
    {
        for (const auto &tag_id : tag_ids)
        {
            tag_ids_.erase(tag_id);
        }
    }

    std::vector<int64_t> removeAllTagIds()
    {
        std::vector<int64_t> tag_ids(tag_ids_.begin(), tag_ids_.end());
        tag_ids_.clear();
        return tag_ids;
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

    void removeLabelIds(const std::vector<int64_t> &label_ids)
    {
        for (const auto &label_id : label_ids)
        {
            label_ids_.erase(label_id);
        }
    }

    std::vector<int64_t> removeAllLabelIds()
    {
        std::vector<int64_t> label_ids(label_ids_.begin(), label_ids_.end());
        label_ids_.clear();
        return label_ids;
    }

    std::set<int64_t> &labelIds()
    {
        return label_ids_;
    }

    const std::set<int64_t> &labelIds() const
    {
        return label_ids_;
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
    QML_NAMED_ELEMENT(ImageInstancesModel)
    QML_UNCREATABLE("Can not create ImageInstancesModel directly!")
    Q_PROPERTY(QItemSelectionModel *selection READ selection CONSTANT)
    Q_PROPERTY(int currentImageId READ getCurrentImageId NOTIFY currentImageChanged)
    Q_PROPERTY(QString currentImageName READ currentImageName NOTIFY currentImageChanged)
    Q_PROPERTY(QString currentImagePath READ currentImagePath NOTIFY currentImageChanged)
    Q_PROPERTY(int currentImageLabelClassId READ currentImageLabelClassId NOTIFY currentImageChanged)
    Q_PROPERTY(int count READ count NOTIFY statsChanged)
    Q_PROPERTY(int lastIndex READ lastIndex WRITE setLastIndex NOTIFY lastSelectedIndexChanged)
public:
    ImageInstancesListModel(dltool::database::ProjectDataBase *database, QObject *parent = nullptr);
    ~ImageInstancesListModel();

    enum class ImageSortOrder
    {
        AddedTime = 0,
        FileName   = 1,
    };
    Q_ENUM(ImageSortOrder)

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
    };
    Q_ENUM(Role)

    QHash<int, QByteArray> roleNames() const override;

    bool addImages(const int64_t dataset_id, const std::vector<QString> &paths, std::vector<int64_t> &image_ids);
    bool addImages(const std::vector<int64_t> &dataset_ids, const std::vector<QString> &paths,
                   std::vector<int64_t> &image_ids);
    bool addImages(const int64_t dataset_id, const QString &image_idr, std::vector<int64_t> &image_ids);
    bool updateImagesDataset(const std::vector<int64_t> &image_ids, const int64_t dataset_id);
    bool updateImagesDataset(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &dataset_ids);
    bool deleteImages(const std::vector<int64_t> &image_ids);
    bool deleteImages(const int64_t dataset_id, std::vector<int64_t> &image_ids);

    std::vector<int64_t> getDatasetIds(const std::vector<int64_t> &image_ids) const;

    std::vector<std::vector<int64_t>> getLabelIds(const std::vector<int64_t> &image_ids) const;

    static std::vector<QString> getImagePaths(const QString &image_idr);

    static std::vector<QString> getFiles(const QString &path, const QStringList &name_filters, bool recursive);

    QItemSelectionModel *selection() const
    {
        return selection_;
    }

    Q_INVOKABLE void shiftSelect(int current_index, int previous_index, QItemSelectionModel::SelectionFlags command);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE int  findRowByImageId(int64_t image_id) const;
    Q_INVOKABLE void setImageSortOrder(int sort_order);

    QString currentImageName() const;
    QString currentImagePath() const;
    int     currentImageLabelClassId() const;

    int count() const
    {
        return static_cast<int>(image_ids_.size());
    }

    int getCurrentImageId() const;

    Q_INVOKABLE std::vector<int64_t> getSelectedImagesId() const;

    ImageInstance               *getImageInstance(const int64_t image_id);
    std::vector<ImageInstance *> getImageInstances(const std::vector<int64_t> &image_ids);

    int lastIndex() const
    {
        return last_index_;
    }

    void setLastIndex(int last_index);

    // NEW: Filter support methods
    void applyFilter(const std::function<bool(int64_t)> &filter_func);
    void clearFilter();

    bool isFilterActive() const
    {
        return is_filtered_;
    }

    int filteredCount() const
    {
        return static_cast<int>(filtered_image_ids_.size());
    }

    int totalCount() const
    {
        return static_cast<int>(full_image_instances_.size());
    }

    void addImagesLabelIds(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_ids);
    void addImagesLabelIds(const std::vector<int64_t> &image_ids, const std::vector<std::vector<int64_t>> &label_ids);
    void setImagesLabelIds(const std::vector<int64_t> &image_ids, const std::vector<std::vector<int64_t>> &label_ids);

    void deleteImagesLabelIds(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_ids);

    void addImagesTagIds(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &tag_ids);
    void addImagesTagIds(const std::vector<int64_t> &image_ids, const std::vector<std::vector<int64_t>> &tag_ids);

    std::vector<int64_t> getAllImageIds() const;
    bool                 setImageLabelClassId(const int64_t image_id, const int64_t label_class_id);
    bool                 setImageLabelClassIds(const std::vector<int64_t> &image_ids,
                                               const std::vector<int64_t> &label_class_ids);

    std::vector<int64_t> getImagesDatasetIds(const std::vector<int64_t> &image_ids) const;

    void getAllDatasetsImagesLabels(std::vector<int64_t> &dataset_ids, std::vector<int64_t> &image_ids,
                                    std::vector<std::vector<int64_t>> &images_label_ids) const;

    QString           getImageName(const int64_t image_id) const;
    QString           getImagePath(const int64_t image_id) const;
    int64_t           getImageLabelClassId(const int64_t image_id) const;
    int64_t           getImageDatasetId(const int64_t image_id) const;
    std::set<int64_t> getImageTagIds(const int64_t image_id) const;

private:
    void init();

    void sortImageIds(std::vector<int64_t> &image_ids) const;
    void rebuildImageIds();
    void restoreSelection(const std::vector<int64_t> &selected_image_ids, int64_t current_image_id);

    int getImageId(const QModelIndex &index) const;

    /**
     * @brief NEW: Helper to rebuild filtered_image_ids_ based on filter function
     */
    void rebuildFilteredList(const std::function<bool(int64_t)> &filter_func);

    /**
     * @brief 删除图像后自动选择下一个合适的图像
     * @param current_row 删除前当前选中的行索引
     * @param new_count 删除后剩余的图像数量
     */
    void selectNextAfterDeletion(int current_row, int new_count);

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
    QVariant getSelected(const QModelIndex &index) const;
    QVariant getIsCurrent(const QModelIndex &index) const;
    QVariant getHasLabels(const QModelIndex &index) const;
    QVariant getImageLabelClassId(const QModelIndex &index) const;

    void notifyHasLabelsChanged(int64_t image_id);
    void notifyImageLabelClassChanged(int64_t image_id);

    void updateSelection(const QItemSelection &selected, const QItemSelection &deselected);

    void onCurrentChanged(const QModelIndex &current, const QModelIndex &previous);

    void resetModel();

    dltool::database::ProjectDataBase *database_{nullptr};

    /**
     * @brief 全部图像实例 {image_id, ImageInstance}，按 image_id 降序保存。
     *
     * 显示顺序由 image_ids_ 和 sort_order_ 控制。
     */
    std::map<int64_t, ImageInstance *, std::greater<int64_t>> full_image_instances_;

    /**
     * @brief 图像 ID 列表，用于显示顺序
     * When filtering is active, this points to filtered_image_ids_
     * When filtering is inactive, this contains all image IDs from full_image_instances_
     */
    std::vector<int64_t> image_ids_;

    /**
     * @brief NEW: Filtered image IDs (subset of full_image_instances_ keys)
     */
    std::vector<int64_t> filtered_image_ids_;

    /**
     * @brief NEW: Flag indicating if filter is active
     */
    bool is_filtered_{false};

    ImageSortOrder sort_order_{ImageSortOrder::AddedTime};

    QItemSelectionModel *selection_{nullptr};

    /**
     * @brief 上次选中的图像索引, 用于多选时记录上次选中的图像索引
     */
    int last_index_{-1};

signals:
    void statsChanged();
    void currentImageChanged();
    void lastSelectedIndexChanged();
};

class ImageInfoListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ImageInfoModel)
    QML_UNCREATABLE("Can not create ImageInfoModel directly!")
public:
    ImageInfoListModel(DatasetsListModel *datasets, ImageInstancesListModel *image_instances,
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
    ImageInstancesListModel *image_instances_{nullptr};
    LabelClassesListModel   *label_classes_{nullptr};
    LabelInstancesListModel *label_instances_{nullptr};
};
} // namespace dltool::data
