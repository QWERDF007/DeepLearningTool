#pragma once

#include <QAbstractListModel>
#include <QtQml>
#include <set>

namespace dltool::database {
class ProjectDataBase;
} // namespace dltool::database

namespace dltool::data {
class LabelData_t;
class LabelDataHelper_t;
} // namespace dltool::data

using LabelData       = std::unique_ptr<dltool::data::LabelData_t>;
using LabelDataHelper = std::unique_ptr<dltool::data::LabelDataHelper_t>;

namespace dltool::data {

class ImageInstancesListModel;
class LabelClassesListModel;

class LabelInstance : public QObject
{
public:
    LabelInstance(const int64_t label_id, const int64_t image_id, const int64_t label_class_id, LabelData data,
                  QObject *parent = nullptr);

    virtual ~LabelInstance();

    int64_t labelId() const
    {
        return label_id_;
    }

    int64_t imageId() const
    {
        return image_id_;
    }

    int64_t labelClassId() const
    {
        return label_class_id_;
    }

    void setLabelClassId(const int64_t label_class_id)
    {
        label_class_id_ = label_class_id;
    }

    const LabelData &data() const
    {
        return data_;
    }

private:
    int64_t label_id_;
    int64_t image_id_;
    int64_t label_class_id_;

    LabelData data_{nullptr};
};

class LabelInstancesListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(LabelInstancesModel)
    QML_UNCREATABLE("Can not create LabelInstancesModel directly!")
    Q_PROPERTY(QItemSelectionModel *selection READ selection CONSTANT)
    Q_PROPERTY(int lastIndex READ lastIndex WRITE setLastIndex NOTIFY lastIndexChanged)
public:
    LabelInstancesListModel(dltool::database::ProjectDataBase *database, ImageInstancesListModel *image_instances,
                            LabelClassesListModel *label_classes, LabelDataHelper label_data_helper,
                            QObject *parent = nullptr);

    ~LabelInstancesListModel();

    enum Role
    {
        LabelIdRole = Qt::UserRole + 1,
        ImageIdRole,
        LabelClassIdRole,
        LabelClassNameRole,
        LabelClassColorRole,
        DataRole,
        SelectedRole,
    };
    Q_ENUM(Role)

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    QHash<int, QByteArray> roleNames() const override;

    LabelInstance *getLabelInstance(const int64_t label_id);

    void addLabels(std::vector<int64_t> &label_ids, const std::vector<int64_t> &image_ids,
                   const std::vector<int64_t> &label_class_ids, const std::vector<QVariantMap> &data);

    void updateLabelsData(const std::vector<int64_t> &label_ids, const std::vector<int64_t> &image_ids,
                          const std::vector<QVariantMap> &data);

    void updateLabelsClass(const std::vector<int64_t> &label_ids, const std::vector<int64_t> &label_class_ids);

    void labelClassUpdated(const int64_t label_class_id);

    void deleteLabels(const std::vector<int64_t> &label_ids);

    std::vector<std::vector<int64_t>> getImagesLabelIds(const std::vector<int64_t> &image_ids) const;

    QItemSelectionModel *selection() const
    {
        return selection_;
    }

    Q_INVOKABLE void shiftSelect(int current_index, int previous_index, QItemSelectionModel::SelectionFlags command);
    Q_INVOKABLE void selectAll();

    int lastIndex() const
    {
        return last_index_;
    }

    void setLastIndex(int last_index);

    int64_t              getImageId(const int64_t label_id) const;
    std::vector<int64_t> getImageIds(const std::vector<int64_t> &label_ids) const;
    std::vector<int64_t> getLabelIds(const int64_t label_class_id) const;
    int64_t              getLabelClassId(const int64_t label_id) const;
    std::vector<int64_t> getLabelClassIds(const std::vector<int64_t> &label_ids) const;

    const LabelDataHelper &helper() const
    {
        return label_data_helper_;
    }

    // NEW: Filter support methods
    void applyFilter(const std::function<bool(int64_t)> &image_filter_func);
    void applyFilter(const std::function<bool(int64_t)> &image_filter_func,
                     const std::function<bool(int64_t)> &label_class_filter_func);
    void clearFilter();

    bool isFilterActive() const
    {
        return is_filtered_;
    }

    int filteredCount() const
    {
        return static_cast<int>(filtered_label_ids_.size());
    }

    int totalCount() const
    {
        return static_cast<int>(full_label_instances_.size());
    }

    // NEW: Access to unfiltered data for statistics
    const std::map<int64_t, LabelInstance *> &getAllLabelInstances() const
    {
        return full_label_instances_;
    }

private:
    void init();

    int      getLabelId(const QModelIndex &index) const;
    int      getImageId(const QModelIndex &index) const;
    int      getLabelClassId(const QModelIndex &index) const;
    QString  getLabelClassName(const QModelIndex &index) const;
    QString  getLabelClassColor(const QModelIndex &index) const;
    QVariant getData(const QModelIndex &index) const;

    /**
     * @brief NEW: Helper to rebuild filtered_label_ids_ based on image filter function
     */
    void rebuildFilteredList(const std::function<bool(int64_t)> &image_filter_func,
                             const std::function<bool(int64_t)> &label_class_filter_func);

    dltool::database::ProjectDataBase *database_{nullptr};
    ImageInstancesListModel           *image_instances_{nullptr};
    LabelClassesListModel             *label_classes_{nullptr};

    LabelDataHelper label_data_helper_{nullptr};

    /**
     * @brief MODIFIED: Renamed from label_instances_ to indicate it's the full dataset
     */
    std::map<int64_t, LabelInstance *> full_label_instances_;

    /**
     * @brief 标注 ID 列表，用于显示顺序
     * When filtering is active, this contains filtered_label_ids_
     * When filtering is inactive, this contains all label IDs from full_label_instances_
     */
    std::vector<int64_t> label_ids_;

    /**
     * @brief NEW: Filtered label IDs (subset of full_label_instances_ keys)
     */
    std::vector<int64_t> filtered_label_ids_;

    /**
     * @brief NEW: Flag indicating if filter is active
     */
    bool is_filtered_{false};

    QItemSelectionModel *selection_{nullptr};

    /**
     * @brief 上次选中的标注索引, 用于多选时记录上次选中的标注索引
     */
    int last_index_{-1};

signals:
    void lastIndexChanged();
};

class ImageLabelsListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ImageLabelsListModel)
    QML_UNCREATABLE("Can not create ImageLabelsListModel directly!")
    Q_PROPERTY(QItemSelectionModel *selection READ selection CONSTANT)
public:
    ImageLabelsListModel(ImageInstancesListModel *image_instances, LabelInstancesListModel *label_instances,
                         LabelClassesListModel *label_classes, QObject *parent = nullptr);

    ~ImageLabelsListModel() {}

    enum Role
    {
        LabelIdRole = Qt::UserRole + 1,
        ImageIdRole,
        LabelClassIdRole,
        DataRole,
        LabelClassColorRole,
        SelectedRole,
        HoveredRole,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    QHash<int, QByteArray> roleNames() const override;

    void addLabels(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_ids);
    void updateLabels(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_ids);
    void deleteLabels(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_ids);

    void labelClassUpdated(const int64_t label_class_id);

    QItemSelectionModel *selection() const
    {
        return selection_;
    }

    /**
     * @brief 获取鼠标位置所在的标注索引
     * @param pos 鼠标位置
     * @return 标注索引
     */
    Q_INVOKABLE std::vector<int> getIndicesAt(const QPointF &pos) const;

    /**
     * @brief 获取选中项中最顶层的索引
     * @return 标注索引
     */
    Q_INVOKABLE int getTopSelectedIndex() const;

    /**
     * @brief 获取第一个选中项的下一项的索引, 如果没有选中项, 则返回第一个
     * @param indices 标注索引列表
     * @return 标注索引
     */
    Q_INVOKABLE QModelIndex chooseIndex(const std::vector<int> &indices) const;

    /**
     * @brief 测试鼠标是否在标注的内部
     */
    Q_INVOKABLE bool isInside(const QPointF &pos, const int index) const;

    /**
     * @brief 测试鼠标是否命中标注的某个手柄, 判定先后顺序为 顶点, 边, 内部
     * @param pos 鼠标位置
     * @param index 标注索引
     * @param scale 图像缩放比例
     * @return 手柄信息
     */
    Q_INVOKABLE QVariantMap hitTestHandle(const QPointF &pos, const int index, const double scale) const;

    /**
     * @brief 设置鼠标悬停的标注索引
     */
    Q_INVOKABLE void setHovered(const std::vector<int> &indices);

    Q_INVOKABLE void shiftSelect(int current_index, int previous_index, QItemSelectionModel::SelectionFlags command);
    Q_INVOKABLE void selectAll();

    void onCurrentImageChanged();

    Q_INVOKABLE std::vector<int64_t> getSelectedLabelIds() const;

    /**
     * @brief 获取标注数据, 由具体的实现类来实现, 一般包含 index, label_id, x, y, width, height, color
     * @param index 标注索引
     * @return 标注数据
     */
    Q_INVOKABLE QVariantMap getData(const int index) const;

    Q_INVOKABLE QVariantMap getEditedData(const QVariantMap &data, const QPointF &start, const QPointF &end);

    /**
     * @brief 获取指定图像的标注摘要信息
     * @param image_id 图像 ID
     * @return 标注摘要字符串，格式："类别1: 数量1\n类别2: 数量2"，无标注时返回空字符串
     */
    Q_INVOKABLE QString getLabelSummaryForImage(int64_t image_id) const;

    /**
     * @brief 获取指定图像的第一个标注类别颜色
     * @param image_id 图像 ID
     * @return 颜色字符串（如 "#FF0000"），无标注时返回空字符串
     */
    Q_INVOKABLE QString getLabelColorForImage(int64_t image_id) const;

private:
    void init();

    void resetModel();

    void updateSelection(const QItemSelection &selected, const QItemSelection &deselected);

    int      getLabelId(const QModelIndex &index) const;
    int      getImageId(const QModelIndex &index) const;
    int      getLabelClassId(const QModelIndex &index) const;
    QVariant getData(const QModelIndex &index) const;
    QVariant getColor(const QModelIndex &index) const;
    QVariant getSelected(const QModelIndex &index) const;
    QVariant getHovered(const QModelIndex &index) const;

    ImageInstancesListModel *image_instances_{nullptr};
    LabelInstancesListModel *label_instances_{nullptr};
    LabelClassesListModel   *label_classes_{nullptr};

    std::vector<int64_t> label_ids_;

    QItemSelectionModel *selection_{nullptr};

    std::set<int> hovered_indices_;
};

class ImageLabelsTableModel : public QAbstractTableModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ImageLabelsTableModel)
    QML_UNCREATABLE("Can not create ImageLabelsTableModel directly!")
    Q_PROPERTY(QItemSelectionModel *selection READ selection CONSTANT)
    Q_PROPERTY(int lastIndex READ lastIndex WRITE setLastIndex NOTIFY lastSelectedIndexChanged)
public:
    ImageLabelsTableModel(ImageInstancesListModel *image_instances, LabelInstancesListModel *label_instances,
                          LabelClassesListModel *label_classes, QObject *parent = nullptr);

    ~ImageLabelsTableModel();

    enum Role
    {
        TitleRole = Qt::UserRole + 1,
        DataRole,
        ClassDataRole,
        SelectedRole,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    QHash<int, QByteArray> roleNames() const override;

    void addLabels(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_ids);
    void updateLabels(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_ids);
    void deleteLabels(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_ids);

    void labelClassUpdated(const int64_t label_class_id);

    QItemSelectionModel *selection() const
    {
        return selection_;
    }

    Q_INVOKABLE void shiftSelect(int current_index, int previous_index, QItemSelectionModel::SelectionFlags command);
    Q_INVOKABLE void selectAll();

    int lastIndex() const
    {
        return last_index_;
    }

    void setLastIndex(int last_index);

    void onCurrentImageChanged();

    Q_INVOKABLE std::vector<int64_t> getSelectedLabelIds() const;

    /**
     * @brief Find row index by label_id
     * @param label_id The label ID to search for
     * @return Row index if found, -1 if not found
     */
    Q_INVOKABLE int findRowByLabelId(int64_t label_id) const;

private:
    void init();

    void resetModel();

    void updateSelection(const QItemSelection &selected, const QItemSelection &deselected);

    QVariant getData(const QModelIndex &index) const;
    QVariant getClassData(const QModelIndex &index) const;
    QVariant getSelected(const QModelIndex &index) const;

    ImageInstancesListModel *image_instances_{nullptr};
    LabelInstancesListModel *label_instances_{nullptr};
    LabelClassesListModel   *label_classes_{nullptr};

    std::vector<int64_t> label_ids_;

    mutable std::vector<QString> column_headers_;
    mutable std::vector<QString> column_keys_;

    QItemSelectionModel *selection_{nullptr};

    int last_index_{-1};

signals:
    void lastSelectedIndexChanged();
};

} // namespace dltool::data
