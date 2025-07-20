#pragma once

#include <QAbstractListModel>
#include <QtQml>
#include <functional>

namespace dltool::data {
class ProjectDataBase;
class LabelData_t;
} // namespace dltool::data

using LabelData        = std::unique_ptr<dltool::data::LabelData_t>;
using LabelDataFactory = std::function<LabelData()>;

namespace dltool::project {

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
public:
    LabelInstancesListModel(data::ProjectDataBase *database, ImageInstancesListModel *image_instances,
                            LabelClassesListModel *label_classes, LabelDataFactory factory, QObject *parent = nullptr);

    ~LabelInstancesListModel();

    enum Role
    {
        LabelIdRole = Qt::UserRole + 1,
        ImageIdRole,
        LabelClassIdRole,
        DataRole,
        SelectedRole,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    QHash<int, QByteArray> roleNames() const override;

    LabelInstance *getLabelInstance(const int64_t label_id);

    void addLabels(std::vector<int64_t> &label_ids, const std::vector<int64_t> &image_ids,
                   const std::vector<int64_t> &label_class_ids, const std::vector<QVariantMap> &data);

    void deleteLabels(const std::vector<int64_t> &label_ids);

    std::vector<std::vector<int64_t>> getImagesLabelIds(const std::vector<int64_t> &image_ids) const;

    QItemSelectionModel *selection() const
    {
        return selection_;
    }

    std::vector<int64_t> getLabelIds(const std::vector<int64_t> &image_ids) const;
    std::vector<int64_t> getImageIds(const std::vector<int64_t> &label_ids) const;

private:
    void init();

    int      getLabelId(const QModelIndex &index) const;
    int      getImageId(const QModelIndex &index) const;
    int      getLabelClassId(const QModelIndex &index) const;
    QVariant getData(const QModelIndex &index) const;

    data::ProjectDataBase   *database_{nullptr};
    ImageInstancesListModel *image_instances_{nullptr};
    LabelClassesListModel   *label_classes_{nullptr};

    LabelDataFactory factory_;

    std::map<int64_t, LabelInstance *> label_instances_;

    std::vector<int64_t> label_ids_;

    QItemSelectionModel *selection_{nullptr};
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
    void deleteLabels(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_ids);

    QItemSelectionModel *selection() const
    {
        return selection_;
    }

    Q_INVOKABLE void hover(const QPoint &pos);
    Q_INVOKABLE void select(const QPoint &pos);
    Q_INVOKABLE void shiftSelect(int current_index, int previous_index, QItemSelectionModel::SelectionFlags command);
    Q_INVOKABLE void selectAll();

    void onCurrentImageChanged();

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

    QModelIndex hovered_index_;
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
                          LabelClassesListModel                                       *label_classes,
                          const std::pair<std::vector<QString>, std::vector<QString>> &columns,
                          QObject                                                     *parent = nullptr);

    ~ImageLabelsTableModel();

    enum Role
    {
        TitleRole = Qt::UserRole + 1,
        DataRole,
        SelectedRole,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    QHash<int, QByteArray> roleNames() const override;

    void addLabels(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_ids);
    void deleteLabels(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_ids);

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

private:
    void init();

    void resetModel();

    void updateSelection(const QItemSelection &selected, const QItemSelection &deselected);

    QVariant getData(const QModelIndex &index) const;
    QVariant getData(LabelInstance *instance, const int col) const;
    QVariant getClassData(LabelInstance *instance) const;
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

} // namespace dltool::project
