#include "data/DataSelectionTreeModel.h"

#include "data/Datasets.h"
#include "data/Images.h"
#include "data/LabelClasses.h"
#include "data/Labels.h"

#include <QSignalBlocker>
#include <algorithm>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace dltool::data {

struct DataSelectionTreeModel::TargetItem
{
    NodeType type{FlatNode};
    qint64   item_id{-1};
    qint64   dataset_id{-1};
    qint64   label_class_id{-1};
    QString  name;
    QString  color;
    int      source_row{-1};
};

struct DataSelectionTreeModel::Node
{
    NodeType type{FlatNode};
    qint64   item_id{-1};
    qint64   dataset_id{-1};
    qint64   label_class_id{-1};
    QString  name;
    QString  color;
    int      source_row{-1};
    Node    *parent{nullptr};
    std::vector<std::unique_ptr<Node>> children;

    int row() const
    {
        if (parent == nullptr)
            return 0;
        const auto &siblings = parent->children;
        for (int i = 0; i < static_cast<int>(siblings.size()); ++i)
        {
            if (siblings[static_cast<size_t>(i)].get() == this)
                return i;
        }
        return 0;
    }
};

namespace {

QVariantList sortedVariantList(const std::set<qint64> &ids)
{
    QVariantList values;
    values.reserve(static_cast<int>(ids.size()));
    for (qint64 id : ids) values.push_back(id);
    return values;
}

} // namespace

DataSelectionTreeModel::DataSelectionTreeModel(QObject *parent)
    : QAbstractItemModel(parent)
    , root_(std::make_unique<Node>())
{
}

DataSelectionTreeModel::~DataSelectionTreeModel() = default;

QModelIndex DataSelectionTreeModel::index(int row, int column, const QModelIndex &parent_index) const
{
    ensureTreeSynced();
    if (column != 0 || row < 0)
        return {};

    Node *parent_node = nodeFromIndex(parent_index);
    if (parent_node == nullptr || row >= static_cast<int>(parent_node->children.size()))
        return {};

    return createIndex(row, column, parent_node->children[static_cast<size_t>(row)].get());
}

QModelIndex DataSelectionTreeModel::parent(const QModelIndex &child) const
{
    ensureTreeSynced();
    if (!child.isValid())
        return {};

    const Node *child_node = nodeFromIndex(child);
    if (child_node == nullptr || child_node->parent == nullptr || child_node->parent == rootNode())
        return {};

    return createIndex(child_node->parent->row(), 0, child_node->parent);
}

int DataSelectionTreeModel::rowCount(const QModelIndex &parent_index) const
{
    ensureTreeSynced();
    const Node *parent_node = nodeFromIndex(parent_index);
    return parent_node == nullptr ? 0 : static_cast<int>(parent_node->children.size());
}

int DataSelectionTreeModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return 1;
}

QVariant DataSelectionTreeModel::data(const QModelIndex &model_index, int role) const
{
    ensureTreeSynced();
    const Node *node = nodeFromIndex(model_index);
    if (node == nullptr || node == rootNode())
        return {};

    switch (role)
    {
    case Qt::DisplayRole:
    case NameRole:
        return node->name;
    case ItemIdRole:
        return node->item_id;
    case DatasetIdRole:
        return node->dataset_id;
    case LabelClassIdRole:
        return node->label_class_id;
    case ColorRole:
        return node->color;
    case SelectedRole:
        return isNodeSelected(node->dataset_id, node->label_class_id);
    case PartiallySelectedRole:
        return isNodePartiallySelected(node->dataset_id, node->label_class_id);
    case NodeTypeRole:
        return node->type;
    case SourceRowRole:
        return node->source_row;
    default:
        return {};
    }
}

bool DataSelectionTreeModel::setData(const QModelIndex &model_index, const QVariant &value, int role)
{
    ensureTreeSynced();
    const Node *node = nodeFromIndex(model_index);
    if (node == nullptr || node == rootNode())
        return false;

    if (role != SelectedRole && role != Qt::CheckStateRole)
        return false;

    const bool selected = role == Qt::CheckStateRole ? value.toInt() == Qt::Checked : value.toBool();
    setNodeSelected(node->dataset_id, node->label_class_id, selected);
    return true;
}

Qt::ItemFlags DataSelectionTreeModel::flags(const QModelIndex &model_index) const
{
    Qt::ItemFlags item_flags = QAbstractItemModel::flags(model_index);
    if (model_index.isValid())
        item_flags |= Qt::ItemIsUserCheckable;
    return item_flags;
}

QHash<int, QByteArray> DataSelectionTreeModel::roleNames() const
{
    return {
        {          ItemIdRole,             "item_id"},
        {         DatasetIdRole,          "dataset_id"},
        {      LabelClassIdRole,      "label_class_id"},
        {            NameRole,                "name"},
        {           ColorRole,               "color"},
        {        SelectedRole,            "selected"},
        {PartiallySelectedRole, "partially_selected"},
        {        NodeTypeRole,           "node_type"},
        {       SourceRowRole,          "source_row"},
    };
}

QAbstractItemModel *DataSelectionTreeModel::sourceModel() const
{
    return source_model_;
}

void DataSelectionTreeModel::setSourceModel(QAbstractItemModel *source_model)
{
    if (source_model_ == source_model && datasets_model_ == nullptr)
        return;

    disconnectSourceModels();
    source_model_          = source_model;
    datasets_model_        = nullptr;
    label_classes_model_   = nullptr;
    image_instances_model_ = nullptr;
    label_instances_model_ = nullptr;
    connectSourceModel(source_model_);
    rebuildTree();
    emit sourceModelChanged();
}

void DataSelectionTreeModel::setDatasetClassSourceModels(DatasetsListModel *datasets_model,
                                                         LabelClassesListModel *label_classes_model,
                                                         ImageInstancesListModel *image_instances_model,
                                                         LabelInstancesListModel *label_instances_model)
{
    if (datasets_model_ == datasets_model && label_classes_model_ == label_classes_model
        && image_instances_model_ == image_instances_model && label_instances_model_ == label_instances_model)
    {
        return;
    }

    disconnectSourceModels();
    source_model_          = datasets_model;
    datasets_model_        = datasets_model;
    label_classes_model_   = label_classes_model;
    image_instances_model_ = image_instances_model;
    label_instances_model_ = label_instances_model;
    connectSourceModel(datasets_model_);
    connectSourceModel(label_classes_model_);
    connectSourceModel(image_instances_model_);
    connectSourceModel(label_instances_model_);
    rebuildTree();
    emit sourceModelChanged();
}

int DataSelectionTreeModel::idRole() const
{
    return id_role_;
}

void DataSelectionTreeModel::setIdRole(int role)
{
    if (id_role_ == role)
        return;

    id_role_ = role;
    rebuildTree();
    emit rolesChanged();
}

int DataSelectionTreeModel::nameRole() const
{
    return name_role_;
}

void DataSelectionTreeModel::setNameRole(int role)
{
    if (name_role_ == role)
        return;

    name_role_ = role;
    rebuildTree();
    emit rolesChanged();
}

int DataSelectionTreeModel::colorRole() const
{
    return color_role_;
}

void DataSelectionTreeModel::setColorRole(int role)
{
    if (color_role_ == role)
        return;

    color_role_ = role;
    rebuildTree();
    emit rolesChanged();
}

int DataSelectionTreeModel::selectedCount() const
{
    ensureTreeSynced();
    return selectedDatasetIds().size();
}

int DataSelectionTreeModel::selectedLabelClassCount() const
{
    ensureTreeSynced();
    return selectedLabelClassIds().size();
}

QVariantList DataSelectionTreeModel::selectedIds() const
{
    ensureTreeSynced();
    return selectedDatasetIds();
}

QVariantList DataSelectionTreeModel::selectedDatasetIds() const
{
    ensureTreeSynced();
    if (datasets_model_ == nullptr)
        return sortedVariantList(selected_flat_ids_);

    std::set<qint64> dataset_ids = selected_dataset_ids_;
    for (const auto &[dataset_id, _] : selected_label_classes_) dataset_ids.insert(dataset_id);
    return sortedVariantList(dataset_ids);
}

QVariantList DataSelectionTreeModel::selectedLabelClassIds() const
{
    ensureTreeSynced();
    if (datasets_model_ == nullptr)
        return sortedVariantList(selected_flat_ids_);

    std::set<qint64> label_class_ids;
    for (const auto &[_, label_class_id] : selected_label_classes_) label_class_ids.insert(label_class_id);
    return sortedVariantList(label_class_ids);
}

QVariantList DataSelectionTreeModel::selectedDatasetClassScope() const
{
    ensureTreeSynced();
    QVariantList scope;
    if (datasets_model_ == nullptr)
    {
        for (const qint64 item_id : selected_flat_ids_)
        {
            QVariantMap item;
            item.insert(QStringLiteral("dataset_id"), item_id);
            item.insert(QStringLiteral("label_class_id"), -1);
            scope.append(item);
        }
        return scope;
    }

    for (const std::unique_ptr<Node> &dataset_node : root_->children)
    {
        const qint64 dataset_id = dataset_node->dataset_id;
        if (dataset_id < 0 || (!isDatasetFullySelected(dataset_id) && !hasSelectedLabelClass(dataset_id)))
            continue;

        if (isDatasetFullySelected(dataset_id))
        {
            QVariantMap item;
            item.insert(QStringLiteral("dataset_id"), dataset_id);
            item.insert(QStringLiteral("label_class_id"), -1);
            scope.append(item);
            continue;
        }

        for (const std::unique_ptr<Node> &class_node : dataset_node->children)
        {
            if (selected_label_classes_.find({dataset_id, class_node->label_class_id})
                == selected_label_classes_.end())
                continue;

            QVariantMap item;
            item.insert(QStringLiteral("dataset_id"), dataset_id);
            item.insert(QStringLiteral("label_class_id"), class_node->label_class_id);
            scope.append(item);
        }
    }
    return scope;
}

void DataSelectionTreeModel::setSelectedIds(const QVariantList &ids)
{
    ensureTreeSynced();
    std::set<qint64> next_ids;
    for (const QVariant &value : ids)
    {
        bool         ok = false;
        const qint64 id = value.toLongLong(&ok);
        if (ok && id >= 0)
            next_ids.insert(id);
    }

    if (datasets_model_ == nullptr)
    {
        if (selected_flat_ids_ == next_ids)
            return;
        selected_flat_ids_ = std::move(next_ids);
    }
    else
    {
        if (selected_dataset_ids_ == next_ids)
            return;
        selected_dataset_ids_ = std::move(next_ids);
        selected_label_classes_.clear();
        for (const std::unique_ptr<Node> &dataset_node : root_->children)
        {
            if (selected_dataset_ids_.find(dataset_node->dataset_id) == selected_dataset_ids_.end())
                continue;
            for (const std::unique_ptr<Node> &class_node : dataset_node->children)
                selected_label_classes_.insert({dataset_node->dataset_id, class_node->label_class_id});
        }
    }

    pruneMissingSelectedIds();
    emitAllRowsChanged();
    emit selectionChanged();
}

void DataSelectionTreeModel::setSelected(int row, bool selected)
{
    setSelectedId(itemIdAt(row), selected);
}

void DataSelectionTreeModel::setSelectedId(qint64 item_id, bool selected)
{
    ensureTreeSynced();
    if (item_id < 0)
        return;

    if (datasets_model_ != nullptr)
    {
        setDatasetSelected(item_id, selected);
        return;
    }

    const bool changed = selected ? selected_flat_ids_.insert(item_id).second : selected_flat_ids_.erase(item_id) > 0;
    if (!changed)
        return;

    for (const std::unique_ptr<Node> &node : root_->children)
    {
        if (node->item_id == item_id)
        {
            emitNodeChanged(node.get());
            break;
        }
    }
    emit selectionChanged();
}

void DataSelectionTreeModel::setNodeSelected(qint64 dataset_id, qint64 label_class_id, bool selected)
{
    ensureTreeSynced();
    if (datasets_model_ == nullptr)
    {
        setSelectedId(dataset_id >= 0 ? dataset_id : label_class_id, selected);
        return;
    }

    if (dataset_id < 0)
        return;

    if (label_class_id >= 0)
        setLabelClassSelected(dataset_id, label_class_id, selected);
    else
        setDatasetSelected(dataset_id, selected);
}

void DataSelectionTreeModel::toggleRow(int row)
{
    const qint64 id = itemIdAt(row);
    if (id >= 0)
        setSelectedId(id, !isSelectedId(id));
}

void DataSelectionTreeModel::toggleNode(qint64 dataset_id, qint64 label_class_id)
{
    setNodeSelected(dataset_id, label_class_id, !isNodeSelected(dataset_id, label_class_id));
}

bool DataSelectionTreeModel::isSelected(int row) const
{
    return isSelectedId(itemIdAt(row));
}

bool DataSelectionTreeModel::isSelectedId(qint64 item_id) const
{
    ensureTreeSynced();
    if (item_id < 0)
        return false;

    if (datasets_model_ != nullptr)
        return isDatasetFullySelected(item_id);
    return selected_flat_ids_.find(item_id) != selected_flat_ids_.end();
}

bool DataSelectionTreeModel::isNodeSelected(qint64 dataset_id, qint64 label_class_id) const
{
    ensureTreeSynced();
    if (datasets_model_ == nullptr)
        return selected_flat_ids_.find(dataset_id >= 0 ? dataset_id : label_class_id) != selected_flat_ids_.end();

    if (dataset_id < 0)
        return false;
    if (label_class_id >= 0)
        return selected_label_classes_.find({dataset_id, label_class_id}) != selected_label_classes_.end();
    return isDatasetFullySelected(dataset_id);
}

bool DataSelectionTreeModel::isNodePartiallySelected(qint64 dataset_id, qint64 label_class_id) const
{
    ensureTreeSynced();
    if (datasets_model_ == nullptr || dataset_id < 0 || label_class_id >= 0)
        return false;
    return isDatasetPartiallySelected(dataset_id);
}

void DataSelectionTreeModel::clearSelection()
{
    ensureTreeSynced();
    if (selected_flat_ids_.empty() && selected_dataset_ids_.empty() && selected_label_classes_.empty())
        return;

    selected_flat_ids_.clear();
    selected_dataset_ids_.clear();
    selected_label_classes_.clear();
    emitAllRowsChanged();
    emit selectionChanged();
}

void DataSelectionTreeModel::selectAll()
{
    ensureTreeSynced();
    if (datasets_model_ == nullptr)
    {
        std::set<qint64> next_ids;
        for (const std::unique_ptr<Node> &node : root_->children)
        {
            if (node->item_id >= 0)
                next_ids.insert(node->item_id);
        }
        if (selected_flat_ids_ == next_ids)
            return;
        selected_flat_ids_ = std::move(next_ids);
    }
    else
    {
        std::set<qint64>        next_dataset_ids;
        std::set<LabelClassKey> next_label_classes;
        for (const std::unique_ptr<Node> &dataset_node : root_->children)
        {
            if (dataset_node->dataset_id < 0)
                continue;
            next_dataset_ids.insert(dataset_node->dataset_id);
            for (const std::unique_ptr<Node> &class_node : dataset_node->children)
            {
                if (class_node->label_class_id >= 0)
                    next_label_classes.insert({dataset_node->dataset_id, class_node->label_class_id});
            }
        }
        if (selected_dataset_ids_ == next_dataset_ids && selected_label_classes_ == next_label_classes)
            return;
        selected_dataset_ids_    = std::move(next_dataset_ids);
        selected_label_classes_ = std::move(next_label_classes);
    }

    emitAllRowsChanged();
    emit selectionChanged();
}

qint64 DataSelectionTreeModel::itemIdAt(int row) const
{
    ensureTreeSynced();
    if (row < 0 || row >= static_cast<int>(root_->children.size()))
        return -1;
    return root_->children[static_cast<size_t>(row)]->item_id;
}

DataSelectionTreeModel::Node *DataSelectionTreeModel::nodeFromIndex(const QModelIndex &model_index) const
{
    if (!model_index.isValid())
        return root_.get();
    return static_cast<Node *>(model_index.internalPointer());
}

DataSelectionTreeModel::Node *DataSelectionTreeModel::rootNode() const
{
    return root_.get();
}

QModelIndex DataSelectionTreeModel::indexForNode(const Node *node) const
{
    if (node == nullptr || node == rootNode() || node->parent == nullptr)
        return {};
    return createIndex(node->row(), 0, const_cast<Node *>(node));
}

void DataSelectionTreeModel::rebuildTree()
{
    tree_sync_pending_ = false;
    beginResetModel();
    root_->children.clear();
    if (datasets_model_ != nullptr)
        rebuildDatasetClassTree();
    else
        rebuildFlatTree();
    const bool selection_changed = pruneMissingSelectedIds();
    endResetModel();
    if (selection_changed)
        emit selectionChanged();
}

void DataSelectionTreeModel::rebuildFlatTree()
{
    if (source_model_ == nullptr)
        return;

    const int row_count = source_model_->rowCount();
    root_->children.reserve(static_cast<size_t>(row_count));
    for (int row = 0; row < row_count; ++row)
    {
        bool          ok = false;
        const qint64 id = sourceData(row, id_role_).toLongLong(&ok);
        if (!ok || id < 0)
            continue;

        auto node        = std::make_unique<Node>();
        node->type       = FlatNode;
        node->item_id    = id;
        node->name       = sourceData(row, name_role_).toString();
        node->color      = color_role_ >= 0 ? sourceData(row, color_role_).toString() : QString();
        node->source_row = row;
        node->parent     = root_.get();
        root_->children.push_back(std::move(node));
    }
}

void DataSelectionTreeModel::rebuildDatasetClassTree()
{
    if (datasets_model_ == nullptr)
        return;

    std::map<qint64, std::set<qint64>> dataset_class_ids;
    auto addDatasetClass = [&dataset_class_ids](qint64 dataset_id, qint64 label_class_id)
    {
        if (dataset_id >= 0 && label_class_id >= 0)
            dataset_class_ids[dataset_id].insert(label_class_id);
    };

    if (image_instances_model_ != nullptr)
    {
        for (const auto &[image_id, image] : image_instances_model_->getAllImageInstances())
        {
            if (image != nullptr)
            {
                addDatasetClass(image->datasetId(), image->imageLabelClassId());
            }
        }
    }

    if (label_instances_model_ != nullptr && image_instances_model_ != nullptr)
    {
        for (const auto &[label_id, label_instance] : label_instances_model_->getAllLabelInstances())
        {
            Q_UNUSED(label_id)
            if (label_instance == nullptr)
                continue;
            const qint64 image_id       = label_instance->imageId();
            const qint64 dataset_id     = image_instances_model_->getImageDatasetId(image_id);
            const qint64 label_class_id = label_instance->labelClassId();
            addDatasetClass(dataset_id, label_class_id);
        }
    }

    const int dataset_count = datasets_model_->rowCount();
    root_->children.reserve(static_cast<size_t>(dataset_count));
    for (int dataset_row = 0; dataset_row < dataset_count; ++dataset_row)
    {
        const QModelIndex dataset_index = datasets_model_->index(dataset_row, 0);
        const qint64      dataset_id
            = datasets_model_->data(dataset_index, DatasetsListModel::DatasetIdRole).toLongLong();
        if (dataset_id < 0)
            continue;

        auto dataset_node        = std::make_unique<Node>();
        dataset_node->type       = DatasetNode;
        dataset_node->item_id    = dataset_id;
        dataset_node->dataset_id = dataset_id;
        dataset_node->name       = datasets_model_->data(dataset_index, DatasetsListModel::NameRole).toString();
        dataset_node->source_row = dataset_row;
        dataset_node->parent     = root_.get();

        const auto class_ids_it = dataset_class_ids.find(dataset_id);
        if (class_ids_it != dataset_class_ids.end() && label_classes_model_ != nullptr)
        {
            for (int class_row = 0; class_row < label_classes_model_->rowCount(); ++class_row)
            {
                const QModelIndex class_index = label_classes_model_->index(class_row, 0);
                const qint64      label_class_id
                    = label_classes_model_->data(class_index, LabelClassesListModel::LabelClassIdRole).toLongLong();
                if (class_ids_it->second.find(label_class_id) == class_ids_it->second.end())
                    continue;

                auto class_node            = std::make_unique<Node>();
                class_node->type           = LabelClassNode;
                class_node->item_id        = label_class_id;
                class_node->dataset_id     = dataset_id;
                class_node->label_class_id = label_class_id;
                class_node->name = label_classes_model_->data(class_index, LabelClassesListModel::NameRole).toString();
                class_node->color
                    = label_classes_model_->data(class_index, LabelClassesListModel::ColorRole).toString();
                class_node->source_row = class_row;
                class_node->parent     = dataset_node.get();
                dataset_node->children.push_back(std::move(class_node));
            }
        }

        root_->children.push_back(std::move(dataset_node));
    }
}

void DataSelectionTreeModel::scheduleTreeSync()
{
    if (tree_sync_pending_)
        return;
    tree_sync_pending_ = true;
    QMetaObject::invokeMethod(
        this,
        [this]()
        {
            if (tree_sync_pending_)
                syncTree();
        },
        Qt::QueuedConnection);
}

void DataSelectionTreeModel::ensureTreeSynced() const
{
    if (!tree_sync_pending_)
        return;
    const_cast<DataSelectionTreeModel *>(this)->syncTree();
}

void DataSelectionTreeModel::syncTree()
{
    tree_sync_pending_ = false;
    if (datasets_model_ != nullptr)
        syncDatasetClassTree();
    else
        syncFlatTree();

    const bool selection_changed = pruneMissingSelectedIds();
    if (selection_changed)
        emit selectionChanged();
}

void DataSelectionTreeModel::syncFlatTree()
{
    if (source_model_ == nullptr)
    {
        if (!root_->children.empty())
        {
            beginRemoveRows(QModelIndex(), 0, static_cast<int>(root_->children.size()) - 1);
            root_->children.clear();
            endRemoveRows();
        }
        return;
    }

    const int row_count = source_model_->rowCount();
    std::vector<TargetItem> target_items;
    std::unordered_set<qint64> seen_ids;
    target_items.reserve(static_cast<size_t>(row_count));

    for (int row = 0; row < row_count; ++row)
    {
        bool ok = false;
        const qint64 id = sourceData(row, id_role_).toLongLong(&ok);
        if (!ok || id < 0)
            continue;
        if (!seen_ids.insert(id).second)
            continue;

        TargetItem item;
        item.type = FlatNode;
        item.item_id = id;
        item.name = sourceData(row, name_role_).toString();
        item.color = color_role_ >= 0 ? sourceData(row, color_role_).toString() : QString();
        item.source_row = row;
        target_items.push_back(std::move(item));
    }

    reconcileChildren(root_.get(), QModelIndex(), target_items);
}

void DataSelectionTreeModel::syncDatasetClassTree()
{
    if (datasets_model_ == nullptr)
    {
        if (!root_->children.empty())
        {
            beginRemoveRows(QModelIndex(), 0, static_cast<int>(root_->children.size()) - 1);
            root_->children.clear();
            endRemoveRows();
        }
        return;
    }

    const int dataset_count = datasets_model_->rowCount();
    std::vector<TargetItem> target_datasets;
    std::unordered_set<qint64> seen_datasets;
    target_datasets.reserve(static_cast<size_t>(dataset_count));

    for (int r = 0; r < dataset_count; ++r)
    {
        const QModelIndex idx = datasets_model_->index(r, 0);
        const qint64 id = datasets_model_->data(idx, DatasetsListModel::DatasetIdRole).toLongLong();
        if (id < 0)
            continue;
        if (!seen_datasets.insert(id).second)
            continue;

        TargetItem item;
        item.type = DatasetNode;
        item.item_id = id;
        item.dataset_id = id;
        item.name = datasets_model_->data(idx, DatasetsListModel::NameRole).toString();
        item.source_row = r;
        target_datasets.push_back(std::move(item));
    }

    reconcileChildren(root_.get(), QModelIndex(), target_datasets);

    std::map<qint64, std::set<qint64>> dataset_class_ids;
    auto addDatasetClass = [&dataset_class_ids](qint64 dataset_id, qint64 label_class_id)
    {
        if (dataset_id >= 0 && label_class_id >= 0)
            dataset_class_ids[dataset_id].insert(label_class_id);
    };

    if (image_instances_model_ != nullptr)
    {
        for (const auto &[image_id, image] : image_instances_model_->getAllImageInstances())
        {
            if (image != nullptr)
            {
                addDatasetClass(image->datasetId(), image->imageLabelClassId());
            }
        }
    }

    if (label_instances_model_ != nullptr && image_instances_model_ != nullptr)
    {
        for (const auto &[label_id, label_instance] : label_instances_model_->getAllLabelInstances())
        {
            Q_UNUSED(label_id)
            if (label_instance == nullptr)
                continue;
            const qint64 image_id       = label_instance->imageId();
            const qint64 dataset_id     = image_instances_model_->getImageDatasetId(image_id);
            const qint64 label_class_id = label_instance->labelClassId();
            addDatasetClass(dataset_id, label_class_id);
        }
    }

    for (const auto &dataset_node : root_->children)
    {
        const qint64 dataset_id = dataset_node->dataset_id;
        const auto class_ids_it = dataset_class_ids.find(dataset_id);
        std::vector<TargetItem> target_classes;
        std::unordered_set<qint64> seen_classes;

        if (class_ids_it != dataset_class_ids.end() && label_classes_model_ != nullptr)
        {
            const int class_count = label_classes_model_->rowCount();
            target_classes.reserve(class_ids_it->second.size());
            for (int class_row = 0; class_row < class_count; ++class_row)
            {
                const QModelIndex class_index = label_classes_model_->index(class_row, 0);
                const qint64 label_class_id
                    = label_classes_model_->data(class_index, LabelClassesListModel::LabelClassIdRole).toLongLong();
                if (label_class_id < 0 || class_ids_it->second.find(label_class_id) == class_ids_it->second.end())
                    continue;
                if (!seen_classes.insert(label_class_id).second)
                    continue;

                TargetItem class_item;
                class_item.type = LabelClassNode;
                class_item.item_id = label_class_id;
                class_item.dataset_id = dataset_id;
                class_item.label_class_id = label_class_id;
                class_item.name = label_classes_model_->data(class_index, LabelClassesListModel::NameRole).toString();
                class_item.color = label_classes_model_->data(class_index, LabelClassesListModel::ColorRole).toString();
                class_item.source_row = class_row;
                target_classes.push_back(std::move(class_item));
            }
        }

        const QModelIndex dataset_index = indexForNode(dataset_node.get());
        reconcileChildren(dataset_node.get(), dataset_index, target_classes);
    }
}

void DataSelectionTreeModel::reconcileChildren(Node *parent_node, const QModelIndex &parent_index,
                                               const std::vector<TargetItem> &target_items)
{
    if (parent_node == nullptr)
        return;

    std::unordered_set<qint64> target_ids;
    target_ids.reserve(target_items.size());
    for (const auto &item : target_items)
    {
        target_ids.insert(item.item_id);
    }

    int i = static_cast<int>(parent_node->children.size()) - 1;
    while (i >= 0)
    {
        if (target_ids.find(parent_node->children[static_cast<size_t>(i)]->item_id) == target_ids.end())
        {
            int end = i;
            while (i >= 0 && target_ids.find(parent_node->children[static_cast<size_t>(i)]->item_id) == target_ids.end())
            {
                --i;
            }
            int start = i + 1;
            beginRemoveRows(parent_index, start, end);
            parent_node->children.erase(parent_node->children.begin() + start, parent_node->children.begin() + end + 1);
            endRemoveRows();
        }
        else
        {
            --i;
        }
    }

    std::unordered_map<qint64, int> target_index_map;
    target_index_map.reserve(target_items.size());
    for (int t = 0; t < static_cast<int>(target_items.size()); ++t)
    {
        target_index_map[target_items[static_cast<size_t>(t)].item_id] = t;
    }

    std::vector<int> current_target_indices;
    current_target_indices.reserve(parent_node->children.size());
    for (const auto &child : parent_node->children)
    {
        current_target_indices.push_back(target_index_map[child->item_id]);
    }

    bool in_order = true;
    for (size_t k = 1; k < current_target_indices.size(); ++k)
    {
        if (current_target_indices[k] <= current_target_indices[k - 1])
        {
            in_order = false;
            break;
        }
    }

    if (!in_order && !current_target_indices.empty())
    {
        const int n = static_cast<int>(current_target_indices.size());
        std::vector<int> dp(n, 1);
        std::vector<int> prev(n, -1);
        int max_len = 1, best_end = 0;
        for (int a = 0; a < n; ++a)
        {
            for (int b = 0; b < a; ++b)
            {
                if (current_target_indices[a] > current_target_indices[b] && dp[b] + 1 > dp[a])
                {
                    dp[a] = dp[b] + 1;
                    prev[a] = b;
                }
            }
            if (dp[a] > max_len)
            {
                max_len = dp[a];
                best_end = a;
            }
        }
        std::unordered_set<int> keep_indices;
        for (int curr = best_end; curr != -1; curr = prev[curr])
        {
            keep_indices.insert(curr);
        }
        int k = n - 1;
        while (k >= 0)
        {
            if (keep_indices.find(k) == keep_indices.end())
            {
                int end = k;
                while (k >= 0 && keep_indices.find(k) == keep_indices.end())
                {
                    --k;
                }
                int start = k + 1;
                beginRemoveRows(parent_index, start, end);
                parent_node->children.erase(parent_node->children.begin() + start, parent_node->children.begin() + end + 1);
                endRemoveRows();
            }
            else
            {
                --k;
            }
        }
    }

    int cur_idx = 0;
    for (int t = 0; t < static_cast<int>(target_items.size()); ++t)
    {
        const auto &target = target_items[static_cast<size_t>(t)];
        if (cur_idx < static_cast<int>(parent_node->children.size())
            && parent_node->children[static_cast<size_t>(cur_idx)]->item_id == target.item_id)
        {
            Node *node = parent_node->children[static_cast<size_t>(cur_idx)].get();
            bool changed = false;
            if (node->name != target.name)
            {
                node->name = target.name;
                changed = true;
            }
            if (node->color != target.color)
            {
                node->color = target.color;
                changed = true;
            }
            if (node->source_row != target.source_row)
            {
                node->source_row = target.source_row;
                changed = true;
            }
            if (changed)
            {
                const QModelIndex idx = createIndex(cur_idx, 0, node);
                emit dataChanged(idx, idx, {Qt::DisplayRole, NameRole, ColorRole, SourceRowRole});
            }
            ++cur_idx;
        }
        else
        {
            int insert_count = 0;
            while ((t + insert_count) < static_cast<int>(target_items.size()))
            {
                if (cur_idx < static_cast<int>(parent_node->children.size())
                    && parent_node->children[static_cast<size_t>(cur_idx)]->item_id
                        == target_items[static_cast<size_t>(t + insert_count)].item_id)
                {
                    break;
                }
                ++insert_count;
            }

            beginInsertRows(parent_index, cur_idx, cur_idx + insert_count - 1);
            for (int ins = 0; ins < insert_count; ++ins)
            {
                const auto &ins_target = target_items[static_cast<size_t>(t + ins)];
                auto new_node = std::make_unique<Node>();
                new_node->type = ins_target.type;
                new_node->item_id = ins_target.item_id;
                new_node->dataset_id = ins_target.dataset_id;
                new_node->label_class_id = ins_target.label_class_id;
                new_node->name = ins_target.name;
                new_node->color = ins_target.color;
                new_node->source_row = ins_target.source_row;
                new_node->parent = parent_node;
                parent_node->children.insert(parent_node->children.begin() + cur_idx + ins, std::move(new_node));

                if (ins_target.type == LabelClassNode && ins_target.dataset_id >= 0 && ins_target.label_class_id >= 0)
                {
                    if (selected_dataset_ids_.find(ins_target.dataset_id) != selected_dataset_ids_.end())
                    {
                        selected_label_classes_.insert({ins_target.dataset_id, ins_target.label_class_id});
                    }
                }
            }
            endInsertRows();

            cur_idx += insert_count;
            t += insert_count - 1;
        }
    }
}

bool DataSelectionTreeModel::isMetadataOnlyChange(QAbstractItemModel *model, const QList<int> &roles) const
{
    if (roles.isEmpty())
        return false;

    if (datasets_model_ != nullptr)
    {
        if (model == datasets_model_)
        {
            const bool has_meta = roles.contains(Qt::DisplayRole) || roles.contains(DatasetsListModel::NameRole);
            const bool has_struct = roles.contains(DatasetsListModel::DatasetIdRole);
            return has_meta && !has_struct;
        }
        if (model == label_classes_model_)
        {
            const bool has_meta = roles.contains(Qt::DisplayRole) || roles.contains(LabelClassesListModel::NameRole)
                || roles.contains(LabelClassesListModel::ColorRole);
            const bool has_struct = roles.contains(LabelClassesListModel::LabelClassIdRole)
                || roles.contains(LabelClassesListModel::OrdinalIndexRole);
            return has_meta && !has_struct;
        }
        return false;
    }

    const bool has_meta = roles.contains(Qt::DisplayRole) || roles.contains(name_role_)
        || (color_role_ >= 0 && roles.contains(color_role_));
    const bool has_struct = roles.contains(id_role_);
    return has_meta && !has_struct;
}

void DataSelectionTreeModel::handleMetadataChanged(QAbstractItemModel *model, const QModelIndex &topLeft,
                                                    const QModelIndex &bottomRight, const QList<int> &roles)
{
    Q_UNUSED(roles);
    if (datasets_model_ != nullptr)
    {
        if (model == datasets_model_)
        {
            for (int row = topLeft.row(); row <= bottomRight.row(); ++row)
            {
                const QModelIndex src_idx = datasets_model_->index(row, 0);
                const qint64 dataset_id = datasets_model_->data(src_idx, DatasetsListModel::DatasetIdRole).toLongLong();
                const QString name = datasets_model_->data(src_idx, DatasetsListModel::NameRole).toString();
                for (const auto &dataset_node : root_->children)
                {
                    if (dataset_node->dataset_id == dataset_id)
                    {
                        dataset_node->name = name;
                        const QModelIndex idx = indexForNode(dataset_node.get());
                        emit dataChanged(idx, idx, {Qt::DisplayRole, NameRole});
                        break;
                    }
                }
            }
        }
        else if (model == label_classes_model_)
        {
            for (int row = topLeft.row(); row <= bottomRight.row(); ++row)
            {
                const QModelIndex src_idx = label_classes_model_->index(row, 0);
                const qint64 label_class_id
                    = label_classes_model_->data(src_idx, LabelClassesListModel::LabelClassIdRole).toLongLong();
                const QString name = label_classes_model_->data(src_idx, LabelClassesListModel::NameRole).toString();
                const QString color = label_classes_model_->data(src_idx, LabelClassesListModel::ColorRole).toString();
                for (const auto &dataset_node : root_->children)
                {
                    for (const auto &class_node : dataset_node->children)
                    {
                        if (class_node->label_class_id == label_class_id)
                        {
                            class_node->name = name;
                            class_node->color = color;
                            const QModelIndex idx = indexForNode(class_node.get());
                            emit dataChanged(idx, idx, {Qt::DisplayRole, NameRole, ColorRole});
                        }
                    }
                }
            }
        }
        return;
    }

    for (int row = topLeft.row(); row <= bottomRight.row(); ++row)
    {
        bool ok = false;
        const qint64 id = sourceData(row, id_role_).toLongLong(&ok);
        const QString name = sourceData(row, name_role_).toString();
        const QString color = color_role_ >= 0 ? sourceData(row, color_role_).toString() : QString();

        for (const auto &node : root_->children)
        {
            if (node->source_row == row || (ok && id >= 0 && node->item_id == id))
            {
                node->name = name;
                node->color = color;
                node->source_row = row;
                const QModelIndex idx = indexForNode(node.get());
                emit dataChanged(idx, idx, {Qt::DisplayRole, NameRole, ColorRole});
                break;
            }
        }
    }
}

void DataSelectionTreeModel::connectSourceModel(QAbstractItemModel *model)
{
    if (model == nullptr)
        return;

    connect(model, &QAbstractItemModel::modelReset, this, &DataSelectionTreeModel::scheduleTreeSync);
    connect(model, &QAbstractItemModel::rowsInserted, this, &DataSelectionTreeModel::scheduleTreeSync);
    connect(model, &QAbstractItemModel::rowsRemoved, this, &DataSelectionTreeModel::scheduleTreeSync);
    connect(model, &QAbstractItemModel::rowsMoved, this, &DataSelectionTreeModel::scheduleTreeSync);
    connect(model, &QAbstractItemModel::layoutChanged, this, &DataSelectionTreeModel::scheduleTreeSync);
    connect(model, &QAbstractItemModel::dataChanged, this,
            [this, model](const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles)
            {
                if (!dataChangeAffectsTree(model, roles))
                    return;

                if (isMetadataOnlyChange(model, roles))
                {
                    handleMetadataChanged(model, topLeft, bottomRight, roles);
                }
                else
                {
                    scheduleTreeSync();
                }
            });
}

bool DataSelectionTreeModel::dataChangeAffectsTree(QAbstractItemModel *model, const QList<int> &roles) const
{
    if (roles.isEmpty())
        return true;

    if (datasets_model_ != nullptr)
    {
        if (model == datasets_model_)
        {
            return roles.contains(Qt::DisplayRole) || roles.contains(DatasetsListModel::DatasetIdRole)
                || roles.contains(DatasetsListModel::NameRole);
        }
        if (model == label_classes_model_)
        {
            return roles.contains(Qt::DisplayRole) || roles.contains(LabelClassesListModel::LabelClassIdRole)
                || roles.contains(LabelClassesListModel::NameRole)
                || roles.contains(LabelClassesListModel::ColorRole)
                || roles.contains(LabelClassesListModel::OrdinalIndexRole);
        }
        if (model == image_instances_model_)
        {
            return roles.contains(ImageInstancesListModel::DatasetIdRole)
                || roles.contains(ImageInstancesListModel::ImageLabelClassIdRole);
        }
        if (model == label_instances_model_)
        {
            return roles.contains(LabelInstancesListModel::ImageIdRole)
                || roles.contains(LabelInstancesListModel::LabelClassIdRole);
        }
        return false;
    }

    return roles.contains(Qt::DisplayRole) || roles.contains(id_role_) || roles.contains(name_role_)
        || (color_role_ >= 0 && roles.contains(color_role_));
}

void DataSelectionTreeModel::disconnectSourceModels()
{
    tree_sync_pending_ = false;
    if (source_model_ != nullptr)
        disconnect(source_model_, nullptr, this, nullptr);
    if (datasets_model_ != nullptr && datasets_model_ != source_model_)
        disconnect(datasets_model_, nullptr, this, nullptr);
    if (label_classes_model_ != nullptr && label_classes_model_ != source_model_)
        disconnect(label_classes_model_, nullptr, this, nullptr);
    if (image_instances_model_ != nullptr && image_instances_model_ != source_model_)
        disconnect(image_instances_model_, nullptr, this, nullptr);
    if (label_instances_model_ != nullptr && label_instances_model_ != source_model_)
        disconnect(label_instances_model_, nullptr, this, nullptr);
}

bool DataSelectionTreeModel::pruneMissingSelectedIds()
{
    bool changed = false;
    if (datasets_model_ == nullptr)
    {
        std::set<qint64> current_ids;
        for (const std::unique_ptr<Node> &node : root_->children)
        {
            if (node->item_id >= 0)
                current_ids.insert(node->item_id);
        }
        for (auto it = selected_flat_ids_.begin(); it != selected_flat_ids_.end();)
        {
            if (current_ids.find(*it) == current_ids.end())
            {
                it      = selected_flat_ids_.erase(it);
                changed = true;
            }
            else
            {
                ++it;
            }
        }
        return changed;
    }

    std::set<qint64>        current_dataset_ids;
    std::set<LabelClassKey> current_label_classes;
    for (const std::unique_ptr<Node> &dataset_node : root_->children)
    {
        current_dataset_ids.insert(dataset_node->dataset_id);
        for (const std::unique_ptr<Node> &class_node : dataset_node->children)
            current_label_classes.insert({dataset_node->dataset_id, class_node->label_class_id});
    }

    for (auto it = selected_dataset_ids_.begin(); it != selected_dataset_ids_.end();)
    {
        if (current_dataset_ids.find(*it) == current_dataset_ids.end())
        {
            it      = selected_dataset_ids_.erase(it);
            changed = true;
        }
        else
        {
            ++it;
        }
    }
    for (auto it = selected_label_classes_.begin(); it != selected_label_classes_.end();)
    {
        if (current_label_classes.find(*it) == current_label_classes.end())
        {
            it      = selected_label_classes_.erase(it);
            changed = true;
        }
        else
        {
            ++it;
        }
    }
    return changed;
}

void DataSelectionTreeModel::emitNodeChanged(const Node *node)
{
    const QModelIndex model_index = indexForNode(node);
    if (model_index.isValid())
        emit dataChanged(model_index, model_index, {SelectedRole, PartiallySelectedRole});
}

void DataSelectionTreeModel::emitAllRowsChanged()
{
    for (const std::unique_ptr<Node> &dataset_node : root_->children)
    {
        emitNodeChanged(dataset_node.get());
        if (!dataset_node->children.empty())
        {
            const QModelIndex parent_index = indexForNode(dataset_node.get());
            emit dataChanged(index(0, 0, parent_index),
                             index(static_cast<int>(dataset_node->children.size()) - 1, 0, parent_index),
                             {SelectedRole, PartiallySelectedRole});
        }
    }
}

bool DataSelectionTreeModel::isDatasetFullySelected(qint64 dataset_id) const
{
    if (dataset_id < 0)
        return false;

    for (const std::unique_ptr<Node> &dataset_node : root_->children)
    {
        if (dataset_node->dataset_id != dataset_id)
            continue;
        if (dataset_node->children.empty())
            return selected_dataset_ids_.find(dataset_id) != selected_dataset_ids_.end();
        return std::all_of(dataset_node->children.begin(), dataset_node->children.end(),
                           [this, dataset_id](const std::unique_ptr<Node> &class_node)
                           {
                               return selected_label_classes_.find({dataset_id, class_node->label_class_id})
                                      != selected_label_classes_.end();
                           });
    }
    return false;
}

bool DataSelectionTreeModel::isDatasetPartiallySelected(qint64 dataset_id) const
{
    if (dataset_id < 0)
        return false;

    if (isDatasetFullySelected(dataset_id))
        return false;
    return selected_dataset_ids_.find(dataset_id) != selected_dataset_ids_.end() || hasSelectedLabelClass(dataset_id);
}

void DataSelectionTreeModel::setDatasetSelected(qint64 dataset_id, bool selected)
{
    if (dataset_id < 0)
        return;

    const bool was_selected = isDatasetFullySelected(dataset_id) || isDatasetPartiallySelected(dataset_id);
    if (selected)
        selected_dataset_ids_.insert(dataset_id);
    else
        selected_dataset_ids_.erase(dataset_id);

    for (const std::unique_ptr<Node> &dataset_node : root_->children)
    {
        if (dataset_node->dataset_id != dataset_id)
            continue;
        for (const std::unique_ptr<Node> &class_node : dataset_node->children)
        {
            const LabelClassKey key{dataset_id, class_node->label_class_id};
            if (selected)
                selected_label_classes_.insert(key);
            else
                selected_label_classes_.erase(key);
        }
        const bool is_selected = isDatasetFullySelected(dataset_id) || isDatasetPartiallySelected(dataset_id);
        if (was_selected != is_selected || !dataset_node->children.empty())
        {
            emitNodeChanged(dataset_node.get());
            for (const std::unique_ptr<Node> &class_node : dataset_node->children) emitNodeChanged(class_node.get());
            emit selectionChanged();
        }
        return;
    }
}

void DataSelectionTreeModel::setLabelClassSelected(qint64 dataset_id, qint64 label_class_id, bool selected)
{
    if (dataset_id < 0 || label_class_id < 0)
        return;

    const LabelClassKey key{dataset_id, label_class_id};
    const bool changed
        = selected ? selected_label_classes_.insert(key).second : selected_label_classes_.erase(key) > 0;
    if (!changed)
        return;

    if (selected || hasSelectedLabelClass(dataset_id))
        selected_dataset_ids_.insert(dataset_id);
    else
        selected_dataset_ids_.erase(dataset_id);

    for (const std::unique_ptr<Node> &dataset_node : root_->children)
    {
        if (dataset_node->dataset_id != dataset_id)
            continue;
        emitNodeChanged(dataset_node.get());
        for (const std::unique_ptr<Node> &class_node : dataset_node->children)
        {
            if (class_node->label_class_id == label_class_id)
            {
                emitNodeChanged(class_node.get());
                break;
            }
        }
        break;
    }
    emit selectionChanged();
}

bool DataSelectionTreeModel::hasSelectedLabelClass(qint64 dataset_id) const
{
    return std::any_of(selected_label_classes_.begin(), selected_label_classes_.end(),
                       [dataset_id](const LabelClassKey &key) { return key.first == dataset_id; });
}

QVariant DataSelectionTreeModel::sourceData(int row, int role) const
{
    if (source_model_ == nullptr || row < 0 || row >= source_model_->rowCount())
        return {};
    return source_model_->data(source_model_->index(row, 0), role);
}

} // namespace dltool::data
