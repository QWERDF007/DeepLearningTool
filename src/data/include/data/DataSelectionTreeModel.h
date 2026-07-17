#pragma once

#include "dltool/data/Export.h"

#include <QAbstractItemModel>
#include <QPointer>
#include <QVariantList>
#include <QtQml>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

namespace dltool::data {

class DatasetsListModel;
class ImageInstancesListModel;
class LabelClassesListModel;
class LabelInstancesListModel;

class DATA_API DataSelectionTreeModel : public QAbstractItemModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(DataSelectionTreeModel)

    Q_PROPERTY(QAbstractItemModel *sourceModel READ sourceModel WRITE setSourceModel NOTIFY sourceModelChanged FINAL)
    Q_PROPERTY(int idRole READ idRole WRITE setIdRole NOTIFY rolesChanged FINAL)
    Q_PROPERTY(int nameRole READ nameRole WRITE setNameRole NOTIFY rolesChanged FINAL)
    Q_PROPERTY(int colorRole READ colorRole WRITE setColorRole NOTIFY rolesChanged FINAL)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged FINAL)
    Q_PROPERTY(int selectedLabelClassCount READ selectedLabelClassCount NOTIFY selectionChanged FINAL)

public:
    enum Role
    {
        ItemIdRole = Qt::UserRole + 1,
        DatasetIdRole,
        LabelClassIdRole,
        NameRole,
        ColorRole,
        SelectedRole,
        PartiallySelectedRole,
        NodeTypeRole,
        SourceRowRole,
    };
    Q_ENUM(Role)

    enum NodeType
    {
        FlatNode = 0,
        DatasetNode,
        LabelClassNode,
    };
    Q_ENUM(NodeType)

    explicit DataSelectionTreeModel(QObject *parent = nullptr);
    ~DataSelectionTreeModel() override;

    QModelIndex            index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex            parent(const QModelIndex &child) const override;
    int                    rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int                    columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant               data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool                   setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags          flags(const QModelIndex &index) const override;
    QHash<int, QByteArray> roleNames() const override;

    QAbstractItemModel *sourceModel() const;
    void                setSourceModel(QAbstractItemModel *source_model);

    Q_INVOKABLE void setDatasetClassSourceModels(DatasetsListModel *datasets_model,
                                                 LabelClassesListModel *label_classes_model,
                                                 ImageInstancesListModel *image_instances_model,
                                                 LabelInstancesListModel *label_instances_model);

    int  idRole() const;
    void setIdRole(int role);

    int  nameRole() const;
    void setNameRole(int role);

    int  colorRole() const;
    void setColorRole(int role);

    int selectedCount() const;
    int selectedLabelClassCount() const;

    Q_INVOKABLE QVariantList selectedIds() const;
    Q_INVOKABLE QVariantList selectedDatasetIds() const;
    Q_INVOKABLE QVariantList selectedLabelClassIds() const;
    Q_INVOKABLE QVariantList selectedDatasetClassScope() const;
    Q_INVOKABLE void         setSelectedIds(const QVariantList &ids);
    Q_INVOKABLE void         setSelected(int row, bool selected);
    Q_INVOKABLE void         setSelectedId(qint64 item_id, bool selected);
    Q_INVOKABLE void         setNodeSelected(qint64 dataset_id, qint64 label_class_id, bool selected);
    Q_INVOKABLE void         toggleRow(int row);
    Q_INVOKABLE void         toggleNode(qint64 dataset_id, qint64 label_class_id);
    Q_INVOKABLE bool         isSelected(int row) const;
    Q_INVOKABLE bool         isSelectedId(qint64 item_id) const;
    Q_INVOKABLE bool         isNodeSelected(qint64 dataset_id, qint64 label_class_id) const;
    Q_INVOKABLE bool         isNodePartiallySelected(qint64 dataset_id, qint64 label_class_id) const;
    Q_INVOKABLE void         clearSelection();
    Q_INVOKABLE void         selectAll();
    Q_INVOKABLE qint64       itemIdAt(int row) const;

signals:
    void sourceModelChanged();
    void rolesChanged();
    void selectionChanged();

private:
    struct Node;
    using LabelClassKey = std::pair<qint64, qint64>;

    Node       *nodeFromIndex(const QModelIndex &index) const;
    Node       *rootNode() const;
    QModelIndex indexForNode(const Node *node) const;

    void rebuildTree();
    void rebuildFlatTree();
    void rebuildDatasetClassTree();
    void connectSourceModel(QAbstractItemModel *model);
    void disconnectSourceModels();
    bool pruneMissingSelectedIds();
    void emitNodeChanged(const Node *node);
    void emitAllRowsChanged();
    bool isDatasetFullySelected(qint64 dataset_id) const;
    bool isDatasetPartiallySelected(qint64 dataset_id) const;
    void setDatasetSelected(qint64 dataset_id, bool selected);
    void setLabelClassSelected(qint64 dataset_id, qint64 label_class_id, bool selected);
    bool hasSelectedLabelClass(qint64 dataset_id) const;
    QVariant sourceData(int row, int role) const;

    std::unique_ptr<Node> root_;

    QPointer<QAbstractItemModel>      source_model_;
    QPointer<DatasetsListModel>       datasets_model_;
    QPointer<LabelClassesListModel>   label_classes_model_;
    QPointer<ImageInstancesListModel> image_instances_model_;
    QPointer<LabelInstancesListModel> label_instances_model_;

    int id_role_{Qt::UserRole + 1};
    int name_role_{Qt::DisplayRole};
    int color_role_{-1};

    std::set<qint64>       selected_flat_ids_;
    std::set<qint64>       selected_dataset_ids_;
    std::set<LabelClassKey> selected_label_classes_;
};

} // namespace dltool::data
