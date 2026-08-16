#include "model/ModelEvaluationModels.h"
#include "model/ModelEvaluationProtocol.h"

#include <algorithm>
#include <cmath>
#include <QMetaMethod>
#include <QMetaObject>
#include <QSet>
#include <utility>

namespace dltool::model {

namespace {

QString statusDisplayText(const evaluation::Status status)
{
    return evaluation::statusDisplayName(status);
}

const QList<qint64> &gtLabelIds(const EvaluationImageRecord &record)
{
    return record.gt_label_ids;
}

const QList<int> &gtClassIds(const EvaluationImageRecord &record)
{
    return record.gt_class_ids;
}

std::pair<int, int> confusionShape(const std::vector<EvaluationConfusionCell> &records)
{
    if (records.empty())
        return {0, 0};

    const QString first_row = records.front().row_key;
    int           columns  = 0;
    while (columns < static_cast<int>(records.size())
           && records.at(static_cast<size_t>(columns)).row_key == first_row)
        ++columns;
    if (columns <= 0 || static_cast<int>(records.size()) % columns != 0)
        return {0, 0};
    return {static_cast<int>(records.size()) / columns, columns};
}

bool sameConfusionCell(const EvaluationConfusionCell &lhs, const EvaluationConfusionCell &rhs)
{
    return lhs.row_key == rhs.row_key && lhs.column_key == rhs.column_key && lhs.row_label == rhs.row_label
           && lhs.column_label == rhs.column_label && lhs.count == rhs.count && lhs.row_class_id == rhs.row_class_id
           && lhs.column_class_id == rhs.column_class_id && lhs.cell_kind == rhs.cell_kind
           && lhs.tooltip == rhs.tooltip && lhs.selectable == rhs.selectable && lhs.diagonal == rhs.diagonal
           && lhs.error == rhs.error;
}

bool sameConfusionLayout(const std::vector<EvaluationConfusionCell> &lhs,
                         const std::vector<EvaluationConfusionCell> &rhs)
{
    if (lhs.size() != rhs.size())
        return false;
    for (size_t index = 0; index < lhs.size(); ++index)
    {
        if (lhs.at(index).row_key != rhs.at(index).row_key
            || lhs.at(index).column_key != rhs.at(index).column_key)
            return false;
    }
    return true;
}

const QList<int> &predClassIds(const EvaluationImageRecord &record)
{
    return record.pred_class_ids;
}

}

void rebuildImageDerivedValues(EvaluationImageRecord &record)
{
    record.gt_label_ids.clear();
    record.gt_class_ids.clear();
    record.pred_class_ids.clear();
    record.max_prediction_score = 0.0;
    record.has_gt = !record.gt.isEmpty();
    record.has_pred = !record.predictions.isEmpty();
    bool has_prediction_score = false;

    for (const EvaluationGroundTruthRecord &ground_truth : record.gt)
    {
        if (ground_truth.label_id >= 0)
            record.gt_label_ids.push_back(ground_truth.label_id);
        if (ground_truth.class_id >= 0 && !record.gt_class_ids.contains(ground_truth.class_id))
            record.gt_class_ids.push_back(ground_truth.class_id);
    }
    for (const EvaluationPredictionRecord &prediction : record.predictions)
    {
        if (prediction.class_id >= 0 && !record.pred_class_ids.contains(prediction.class_id))
            record.pred_class_ids.push_back(prediction.class_id);
        if (!has_prediction_score || prediction.score > record.max_prediction_score)
        {
            record.max_prediction_score = prediction.score;
            has_prediction_score        = true;
        }
    }
}

EvaluationMetricModel::EvaluationMetricModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int EvaluationMetricModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(records_.size());
}

QVariant EvaluationMetricModel::data(const QModelIndex &index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};
    const auto &record = records_.at(static_cast<size_t>(index.row()));
    switch (role)
    {
    case Qt::DisplayRole:
    case LabelRole: return record.label;
    case KeyRole: return record.key;
    case ClassNameRole: return record.class_name;
    case ClassIdRole: return record.class_id;
    case PrecisionRole: return record.precision;
    case RecallRole: return record.recall;
    case F1Role: return record.f1;
    case PrecisionTextRole:
        return record.precision_defined ? QString::number(record.precision, 'f', 3) : QString("—");
    case RecallTextRole:
        return record.recall_defined ? QString::number(record.recall, 'f', 3) : QString("—");
    case F1TextRole:
        return record.f1_defined ? QString::number(record.f1, 'f', 3) : QString("—");
    case TpRole: return record.tp;
    case FpRole: return record.fp;
    case FnRole: return record.fn;
    default: return {};
    }
}

QHash<int, QByteArray> EvaluationMetricModel::roleNames() const
{
    return {{KeyRole, "key"}, {LabelRole, "label"}, {ClassNameRole, "className"}, {ClassIdRole, "classId"},
            {PrecisionRole, "precision"}, {RecallRole, "recall"}, {F1Role, "f1"},
            {PrecisionTextRole, "precisionText"}, {RecallTextRole, "recallText"}, {F1TextRole, "f1Text"},
            {TpRole, "tp"}, {FpRole, "fp"}, {FnRole, "fn"}};
}

void EvaluationMetricModel::setRecords(std::vector<EvaluationMetricRecord> records)
{
    beginResetModel();
    records_ = std::move(records);
    endResetModel();
}

const std::vector<EvaluationMetricRecord> &EvaluationMetricModel::records() const
{
    return records_;
}

EvaluationMetricSortProxyModel::EvaluationMetricSortProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    setSortRole(EvaluationMetricModel::F1Role);
    sort(0, Qt::DescendingOrder);
}

void EvaluationMetricSortProxyModel::sortBy(const QString &field)
{
    const QString normalized = field.trimmed().toLower();
    if (normalized == QStringLiteral("fp"))
        setSortRole(EvaluationMetricModel::FpRole);
    else if (normalized == QStringLiteral("fn"))
        setSortRole(EvaluationMetricModel::FnRole);
    else if (normalized == QStringLiteral("label"))
        setSortRole(EvaluationMetricModel::LabelRole);
    else
        setSortRole(EvaluationMetricModel::F1Role);
    sort(0, normalized == QStringLiteral("label") ? Qt::AscendingOrder : Qt::DescendingOrder);
}

EvaluationConfusionModel::EvaluationConfusionModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int EvaluationConfusionModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : row_count_;
}

int EvaluationConfusionModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : column_count_;
}

QVariant EvaluationConfusionModel::data(const QModelIndex &index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount() || index.column() < 0
        || index.column() >= columnCount())
        return {};
    const auto &record = records_.at(static_cast<size_t>(index.row() * column_count_ + index.column()));
    switch (role)
    {
    case Qt::DisplayRole:
    case CountRole: return record.count;
    case RowKeyRole: return record.row_key;
    case ColumnKeyRole: return record.column_key;
    case RowLabelRole: return record.row_label;
    case ColumnLabelRole: return record.column_label;
    case RowClassIdRole: return record.row_class_id;
    case ColumnClassIdRole: return record.column_class_id;
    case CellKindRole: return evaluation::cellKindKey(record.cell_kind);
    case CellKindValueRole: return static_cast<int>(record.cell_kind);
    case SelectableRole: return record.selectable;
    case IsDiagonalRole: return record.diagonal;
    case IsErrorRole: return record.error;
    case TooltipRole:
        return record.tooltip.isEmpty()
            ? QString("PRED %1 / GT %2\\n数量：%3").arg(record.row_label, record.column_label).arg(record.count)
            : record.tooltip;
    default: return {};
    }
}

QVariant EvaluationConfusionModel::headerData(const int section, const Qt::Orientation orientation,
                                              const int role) const
{
    if (role != Qt::DisplayRole || section < 0
        || (orientation == Qt::Horizontal ? section >= column_count_ : section >= row_count_))
        return {};
    const auto &record
        = records_.at(static_cast<size_t>(orientation == Qt::Horizontal ? section : section * column_count_));
    return orientation == Qt::Horizontal ? record.column_label : record.row_label;
}

QHash<int, QByteArray> EvaluationConfusionModel::roleNames() const
{
    return {{Qt::DisplayRole, "display"}, {RowKeyRole, "rowKey"}, {ColumnKeyRole, "columnKey"},
            {RowLabelRole, "rowLabel"}, {ColumnLabelRole, "columnLabel"}, {CountRole, "count"},
            {RowClassIdRole, "rowClassId"}, {ColumnClassIdRole, "columnClassId"}, {CellKindRole, "cellKind"},
            {CellKindValueRole, "cellKindValue"},
            {SelectableRole, "selectable"}, {IsDiagonalRole, "isDiagonal"}, {IsErrorRole, "isError"},
            {TooltipRole, "tooltip"}};
}

void EvaluationConfusionModel::setRecords(std::vector<EvaluationConfusionCell> records)
{
    const auto [new_rows, new_columns] = confusionShape(records);
    if (row_count_ == new_rows && column_count_ == new_columns && sameConfusionLayout(records_, records))
    {
        bool changed = false;
        for (size_t index = 0; index < records_.size(); ++index)
        {
            if (!sameConfusionCell(records_.at(index), records.at(index)))
            {
                changed = true;
                break;
            }
        }
        if (!changed)
            return;

        records_ = std::move(records);
        if (row_count_ > 0 && column_count_ > 0)
            emit dataChanged(index(0, 0), index(row_count_ - 1, column_count_ - 1));
        return;
    }

    beginResetModel();
    records_ = std::move(records);
    row_count_    = new_rows;
    column_count_ = new_columns;
    endResetModel();
}

const std::vector<EvaluationConfusionCell> &EvaluationConfusionModel::records() const
{
    return records_;
}

EvaluationInstanceModel::EvaluationInstanceModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int EvaluationInstanceModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(records_.size());
}

QVariant EvaluationInstanceModel::data(const QModelIndex &index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};
    const auto &record = records_.at(static_cast<size_t>(index.row()));
    switch (role)
    {
    case Qt::DisplayRole:
    case ImageNameRole: return record.image_name;
    case EventUuidRole: return record.event_uuid;
    case ImageIdRole: return record.image_id;
    case DatasetIdRole: return record.dataset_id;
    case ImagePathRole: return record.image_path;
    case ImageWidthRole: return record.image_width;
    case ImageHeightRole: return record.image_height;
    case StatusRole: return evaluation::statusKey(record.status);
    case StatusKindRole: return static_cast<int>(record.status);
    case StatusTextRole: return statusDisplayText(record.status);
    case GtClassRole: return record.gt_class;
    case GtClassNameRole: return record.gt_class;
    case PredClassRole: return record.pred_class;
    case PredClassNameRole: return record.pred_class;
    case GtClassIdRole: return record.gt_class_id;
    case PredClassIdRole: return record.pred_class_id;
    case ScoreRole: return record.score;
    case PredScoreRole: return record.score;
    case IouRole: return record.iou;
    case GtGeometryRole: return record.gt_geometry;
    case PredGeometryRole: return record.pred_geometry;
    case GtBoundsRole: return record.gt_bounds;
    case PredBoundsRole: return record.pred_bounds;
    case CropBoundsRole: return record.crop_bounds;
    case GtOverlayBoundsRole: return record.gt_overlay_bounds;
    case PredOverlayBoundsRole: return record.pred_overlay_bounds;
    case GtOverlayPointsRole: return record.gt_overlay_points;
    case PredOverlayPointsRole: return record.pred_overlay_points;
    case GtMaskUrlRole: return record.gt_mask_url;
    case PredMaskUrlRole: return record.pred_mask_url;
    case GtLabelIdRole: return record.gt_label_id;
    case GtInstanceIdRole: return record.gt_instance_id;
    case PredInstanceIdRole: return record.pred_instance_id;
    case GtClassColorRole: return record.gt_class_color;
    case PredClassColorRole: return record.pred_class_color;
    case ThumbnailUrlRole: return record.thumbnail_url;
    case SelectedRole: return record.selected;
    default: return {};
    }
}

QHash<int, QByteArray> EvaluationInstanceModel::roleNames() const
{
    return {{EventUuidRole, "eventUuid"}, {ImageIdRole, "imageId"}, {DatasetIdRole, "datasetId"},
            {ImageNameRole, "imageName"}, {ImagePathRole, "imagePath"}, {ImageWidthRole, "imageWidth"},
            {ImageHeightRole, "imageHeight"}, {StatusRole, "status"}, {StatusKindRole, "statusKind"},
            {StatusTextRole, "statusText"}, {GtClassRole, "gtClass"}, {GtClassNameRole, "gtClassName"},
            {PredClassRole, "predClass"}, {PredClassNameRole, "predClassName"},
            {GtClassIdRole, "gtClassId"}, {PredClassIdRole, "predClassId"}, {ScoreRole, "score"},
            {PredScoreRole, "predScore"}, {IouRole, "iou"},
            {GtGeometryRole, "gtGeometry"}, {PredGeometryRole, "predGeometry"},
            {GtBoundsRole, "gtBounds"}, {PredBoundsRole, "predBounds"}, {CropBoundsRole, "cropBounds"},
            {GtOverlayBoundsRole, "gtOverlayBounds"}, {PredOverlayBoundsRole, "predOverlayBounds"},
            {GtOverlayPointsRole, "gtOverlayPoints"}, {PredOverlayPointsRole, "predOverlayPoints"},
            {GtMaskUrlRole, "gtMaskUrl"}, {PredMaskUrlRole, "predMaskUrl"},
            {GtLabelIdRole, "gtLabelId"}, {GtInstanceIdRole, "gtInstanceId"},
            {PredInstanceIdRole, "predInstanceId"}, {GtClassColorRole, "gtClassColor"},
            {PredClassColorRole, "predClassColor"}, {ThumbnailUrlRole, "thumbnailUrl"},
            {SelectedRole, "selected"}};
}

void EvaluationInstanceModel::setRecords(std::vector<EvaluationInstanceRecord> records)
{
    beginResetModel();
    records_ = std::move(records);
    endResetModel();
}

void EvaluationInstanceModel::setSelectedEvent(const QString &eventUuid)
{
    const QString value = eventUuid.trimmed();
    for (int row = 0; row < rowCount(); ++row)
    {
        const bool selected = !value.isEmpty() && records_.at(static_cast<size_t>(row)).event_uuid == value;
        if (records_.at(static_cast<size_t>(row)).selected == selected)
            continue;
        records_[static_cast<size_t>(row)].selected = selected;
        const QModelIndex index = this->index(row, 0);
        emit dataChanged(index, index, {SelectedRole});
    }
}

const std::vector<EvaluationInstanceRecord> &EvaluationInstanceModel::records() const
{
    return records_;
}

const EvaluationInstanceRecord *EvaluationInstanceModel::recordAt(const int row) const
{
    return row >= 0 && row < rowCount() ? &records_.at(static_cast<size_t>(row)) : nullptr;
}

EvaluationImageModel::EvaluationImageModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int EvaluationImageModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(records_.size());
}

QVariant EvaluationImageModel::data(const QModelIndex &index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};
    const auto &record = records_.at(static_cast<size_t>(index.row()));
    switch (role)
    {
    case Qt::DisplayRole:
    case ImageNameRole: return record.name;
    case ImageIdRole: return record.id;
    case DatasetIdRole: return record.dataset_id;
    case ImagePathRole: return record.path;
    case ImageWidthRole: return record.width;
    case ImageHeightRole: return record.height;
    case GtLabelIdsRole:
    {
        QVariantList values;
        for (const qint64 value : record.gt_label_ids)
            values.push_back(value);
        return values;
    }
    case GtClassIdsRole:
    {
        QVariantList values;
        for (const int value : record.gt_class_ids)
            values.push_back(value);
        return values;
    }
    case PredClassIdsRole:
    {
        QVariantList values;
        for (const int value : record.pred_class_ids)
            values.push_back(value);
        return values;
    }
    case ScoreRole: return record.max_prediction_score;
    case HasGtRole: return record.has_gt;
    case HasPredRole: return record.has_pred;
    default: return {};
    }
}

QHash<int, QByteArray> EvaluationImageModel::roleNames() const
{
    return {{ImageIdRole, "imageId"},       {DatasetIdRole, "datasetId"},
            {ImageNameRole, "imageName"},   {ImagePathRole, "imagePath"},
            {ImageWidthRole, "imageWidth"}, {ImageHeightRole, "imageHeight"},
            {GtLabelIdsRole, "gtLabelIds"}, {GtClassIdsRole, "gtClassIds"},
            {PredClassIdsRole, "predClassIds"}, {ScoreRole, "score"}, {HasGtRole, "hasGt"},
            {HasPredRole, "hasPred"}};
}

void EvaluationImageModel::setRecords(std::vector<EvaluationImageRecord> records)
{
    beginResetModel();
    for (EvaluationImageRecord &record : records)
        rebuildImageDerivedValues(record);
    records_ = std::move(records);
    endResetModel();
}

const std::vector<EvaluationImageRecord> &EvaluationImageModel::records() const
{
    return records_;
}

const EvaluationImageRecord *EvaluationImageModel::recordAt(const int row) const
{
    return row >= 0 && row < rowCount() ? &records_.at(static_cast<size_t>(row)) : nullptr;
}

namespace {

const EvaluationImageRecord *imageRecordFromModel(const QAbstractItemModel *model, QModelIndex index)
{
    while (model != nullptr && index.isValid())
    {
        if (const auto *proxy = qobject_cast<const QAbstractProxyModel *>(model))
        {
            index = proxy->mapToSource(index);
            model = proxy->sourceModel();
            continue;
        }
        const auto *image_model = qobject_cast<const EvaluationImageModel *>(model);
        return image_model != nullptr ? image_model->recordAt(index.row()) : nullptr;
    }
    return nullptr;
}

const EvaluationInstanceRecord *instanceRecordFromModel(const QAbstractItemModel *model, QModelIndex index)
{
    while (model != nullptr && index.isValid())
    {
        if (const auto *proxy = qobject_cast<const QAbstractProxyModel *>(model))
        {
            index = proxy->mapToSource(index);
            model = proxy->sourceModel();
            continue;
        }
        const auto *instance_model = qobject_cast<const EvaluationInstanceModel *>(model);
        return instance_model != nullptr ? instance_model->recordAt(index.row()) : nullptr;
    }
    return nullptr;
}

bool hasInvokable(QObject *object, const char *method, const int parameter_count)
{
    if (object == nullptr)
        return false;
    const QMetaObject *meta_object = object->metaObject();
    for (int index = 0; index < meta_object->methodCount(); ++index)
    {
        const QMetaMethod meta_method = meta_object->method(index);
        if (meta_method.name() == method && meta_method.parameterCount() == parameter_count)
            return true;
    }
    return false;
}

bool globalFilterIsActive(QObject *object, bool *invoked = nullptr)
{
    bool active = true;
    const bool ok = hasInvokable(object, "isActive", 0)
        && QMetaObject::invokeMethod(object, "isActive", Qt::DirectConnection, Q_RETURN_ARG(bool, active));
    if (invoked != nullptr)
        *invoked = ok;
    return active;
}

bool invokeBool(QObject *object, const char *method, const qint64 value, bool *invoked = nullptr)
{
    bool accepted = true;
    const bool ok = hasInvokable(object, method, 1)
        && QMetaObject::invokeMethod(object, method, Qt::DirectConnection, Q_RETURN_ARG(bool, accepted),
                                     Q_ARG(qint64, value));
    if (invoked != nullptr)
        *invoked = ok;
    return accepted;
}

bool invokeBool(QObject *object, const char *method, bool *invoked)
{
    bool accepted = true;
    const bool ok = hasInvokable(object, method, 0)
        && QMetaObject::invokeMethod(object, method, Qt::DirectConnection, Q_RETURN_ARG(bool, accepted));
    if (invoked != nullptr)
        *invoked = ok;
    return accepted;
}

}

EvaluationImageFilterProxyModel::EvaluationImageFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    /**
     * @brief 各筛选设置器显式调用 invalidateFilter()。
     *
     * 保持代理的动态筛选关闭，避免评估结果和选择更新期间发生重入筛选。
     */
    setDynamicSortFilter(false);
}

void EvaluationImageFilterProxyModel::setGlobalFilter(QObject *filter)
{
    if (global_filter_ == filter)
        return;
    if (global_filter_ != nullptr)
        disconnect(global_filter_, nullptr, this, nullptr);
    global_filter_ = filter;
    if (global_filter_ != nullptr)
        connect(global_filter_, SIGNAL(filterChanged()), this, SLOT(onExternalFilterChanged()));
    invalidateFilter();
    emit filterChanged();
}

bool EvaluationImageFilterProxyModel::acceptsRecord(const EvaluationImageRecord &record) const
{
    if (global_filter_ == nullptr)
        return true;

    bool active_invoked = false;
    if (!globalFilterIsActive(global_filter_, &active_invoked) && active_invoked)
        return true;

    bool invoked = false;
    if (!invokeBool(global_filter_, "acceptsImage", record.id, &invoked))
        return invoked ? false : true;

    /**
     * @brief 标签搜索和自定义标签条件必须作用于图像所属的 GT 标签。
     *
     * acceptsImage() 无法读取当前评估记录中的标签 ID，因此不能单独完成判断。
     */
    const QList<qint64> &label_ids = gtLabelIds(record);
    if (!label_ids.isEmpty())
    {
        bool label_method_invoked = false;
        bool any_label_accepted = false;
        for (const qint64 label_id : label_ids)
        {
            bool accepted = false;
            bool invoked_label = false;
            accepted = invokeBool(global_filter_, "acceptsLabel", label_id, &invoked_label);
            label_method_invoked = label_method_invoked || invoked_label;
            any_label_accepted = any_label_accepted || (invoked_label && accepted);
        }
        if (label_method_invoked && !any_label_accepted)
            return false;
    }

    /**
     * @brief 全局图像筛选覆盖数据集、标签、图像标签类别、文件名和自定义条件。
     *
     * LabelClass 属于实例级筛选，因此图像行需按自身 GT 标签执行同一套规则。
     */
    bool class_enabled = false;
    bool class_enabled_invoked = false;
    class_enabled = invokeBool(global_filter_, "isLabelClassFilterEnabled", &class_enabled_invoked);
    if (!class_enabled_invoked || !class_enabled)
        return true;
    bool inverted = false;
    bool inverted_invoked = false;
    inverted = invokeBool(global_filter_, "isLabelClassFilterInverted", &inverted_invoked);
    if (!inverted_invoked)
        inverted = false;

    /**
     * @brief LabelClass 是面向 GT 的图像筛选。
     *
     * 正向选择要求至少一个 GT 类别命中，反选要求所有 GT 类别均不被排除；
     * 纯 FP 图像没有 GT 类别，不能把 PRED 当作 GT 来显示。
     */
    const QList<int> &relevant_classes = gtClassIds(record);

    QList<bool> accepted_labels;
    bool class_method_invoked = false;
    for (const int class_id : relevant_classes)
    {
        bool class_invoked = false;
        const bool accepted = invokeBool(global_filter_, "acceptsLabelClassId", class_id, &class_invoked);
        class_method_invoked = class_method_invoked || class_invoked;
        if (class_invoked)
            accepted_labels.push_back(accepted);
    }
    if (!class_method_invoked && !relevant_classes.isEmpty())
        return true;
    if (accepted_labels.isEmpty())
        return inverted;
    if (inverted)
        return std::all_of(accepted_labels.cbegin(), accepted_labels.cend(), [](bool value) { return value; });
    return std::any_of(accepted_labels.cbegin(), accepted_labels.cend(), [](bool value) { return value; });
}

bool EvaluationImageFilterProxyModel::filterAcceptsRow(const int source_row,
                                                        const QModelIndex &source_parent) const
{
    const auto *source = qobject_cast<const EvaluationImageModel *>(sourceModel());
    if (source == nullptr)
        return false;
    const EvaluationImageRecord *record = imageRecordFromModel(source, source->index(source_row, 0, source_parent));
    return record != nullptr && acceptsRecord(*record);
}

void EvaluationImageFilterProxyModel::onExternalFilterChanged()
{
    invalidateFilter();
    emit filterChanged();
}

EvaluationGlobalFilterProxyModel::EvaluationGlobalFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(false);
}

QVariantList EvaluationGlobalFilterProxyModel::datasetIds() const
{
    return dataset_ids_;
}

void EvaluationGlobalFilterProxyModel::setDatasetIds(const QVariantList &ids)
{
    if (dataset_ids_ == ids)
        return;
    dataset_ids_ = ids;
    invalidateFilter();
    emit filterChanged();
}

QVariantList EvaluationGlobalFilterProxyModel::classIds() const
{
    return class_ids_;
}

void EvaluationGlobalFilterProxyModel::setClassIds(const QVariantList &ids)
{
    if (class_ids_ == ids)
        return;
    class_ids_ = ids;
    invalidateFilter();
    emit filterChanged();
}

void EvaluationGlobalFilterProxyModel::setGlobalFilter(QObject *filter)
{
    if (global_filter_ == filter)
        return;
    if (global_filter_ != nullptr)
        disconnect(global_filter_, nullptr, this, nullptr);
    global_filter_ = filter;
    if (global_filter_ != nullptr)
        connect(global_filter_, SIGNAL(filterChanged()), this, SLOT(onExternalFilterChanged()));
    invalidateFilter();
    emit filterChanged();
}

bool EvaluationGlobalFilterProxyModel::acceptsGlobalLabel(const EvaluationInstanceRecord &record) const
{
    if (global_filter_ == nullptr)
        return true;

    bool active_invoked = false;
    if (!globalFilterIsActive(global_filter_, &active_invoked) && active_invoked)
        return true;

    /**
     * @brief GT 标签 ID 是标签搜索结果及其他标签级谓词的权威对象。
     *
     * 纯 FP 事件没有 GT 标签，不为此类条件构造虚假的匹配。
     */
    if (record.gt_label_id >= 0)
    {
        bool invoked_label = false;
        const bool accepted_label = invokeBool(global_filter_, "acceptsLabel", record.gt_label_id, &invoked_label);
        if (invoked_label && !accepted_label)
            return false;
    }

    bool filter_enabled = false;
    bool enabled_invoked = false;
    filter_enabled = invokeBool(global_filter_, "isLabelClassFilterEnabled", &enabled_invoked);
    if (!enabled_invoked || !filter_enabled)
        return true;
    bool inverted = false;
    bool inverted_invoked = false;
    inverted = invokeBool(global_filter_, "isLabelClassFilterInverted", &inverted_invoked);
    if (!inverted_invoked)
        inverted = false;

    /**
     * @brief 匹配、漏检和类别错误事件始终优先使用 GT 类别。
     *
     * 若把 PRED 也用于判断，选中类别的误检可能绕过不匹配的 GT；只有纯 FP
     * 事件才回退到预测类别。
     */
    const int class_id = record.gt_class_id >= 0 ? record.gt_class_id : record.pred_class_id;
    bool invoked = false;
    const bool accepted = invokeBool(global_filter_, "acceptsLabelClassId", class_id, &invoked);
    if (!invoked)
        return true;
    if (class_id < 0)
        return inverted;
    return accepted;
}

bool EvaluationGlobalFilterProxyModel::acceptsRecord(const EvaluationInstanceRecord &record) const
{
    bool active_invoked = false;
    const bool external_filter_active = global_filter_ != nullptr
        && (globalFilterIsActive(global_filter_, &active_invoked) || !active_invoked);
    if (external_filter_active && record.image_id >= 0)
    {
        bool invoked = false;
        if (!invokeBool(global_filter_, "acceptsImage", record.image_id, &invoked) && invoked)
            return false;
    }
    if (!acceptsGlobalLabel(record))
        return false;
    if (!dataset_ids_.isEmpty())
    {
        bool match = false;
        for (const QVariant &value : dataset_ids_)
            match = match || value.toLongLong() == record.dataset_id;
        if (!match)
            return false;
    }
    if (!class_ids_.isEmpty())
    {
        bool match = false;
        const int relevant_class_id = record.gt_class_id >= 0 ? record.gt_class_id : record.pred_class_id;
        for (const QVariant &value : class_ids_)
            match = match || value.toInt() == relevant_class_id;
        if (!match)
            return false;
    }
    return true;
}

bool EvaluationGlobalFilterProxyModel::filterAcceptsRow(const int source_row,
                                                        const QModelIndex &source_parent) const
{
    const QAbstractItemModel *source = sourceModel();
    if (source == nullptr)
        return false;
    const EvaluationInstanceRecord *record
        = instanceRecordFromModel(source, source->index(source_row, 0, source_parent));
    return record != nullptr && acceptsRecord(*record);
}

void EvaluationGlobalFilterProxyModel::onExternalFilterChanged()
{
    invalidateFilter();
    emit filterChanged();
}

EvaluationCellFilterProxyModel::EvaluationCellFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(false);
}

QString EvaluationCellFilterProxyModel::status() const { return status_; }

void EvaluationCellFilterProxyModel::setStatus(const QString &status)
{
    const QString value = status.trimmed();
    if (status_ == value)
        return;
    status_ = value;
    invalidateFilter();
    emit filterChanged();
}

QString EvaluationCellFilterProxyModel::matrixRow() const { return matrix_row_; }

void EvaluationCellFilterProxyModel::setMatrixRow(const QString &value)
{
    if (matrix_row_ == value)
        return;
    matrix_row_ = value;
    invalidateFilter();
    emit filterChanged();
}

QString EvaluationCellFilterProxyModel::matrixColumn() const { return matrix_column_; }

void EvaluationCellFilterProxyModel::setMatrixColumn(const QString &value)
{
    if (matrix_column_ == value)
        return;
    matrix_column_ = value;
    invalidateFilter();
    emit filterChanged();
}

QVariantList EvaluationCellFilterProxyModel::predClassIds() const { return pred_class_ids_; }

void EvaluationCellFilterProxyModel::setPredClassIds(const QVariantList &ids)
{
    if (pred_class_ids_ == ids)
        return;
    pred_class_ids_ = ids;
    invalidateFilter();
    emit filterChanged();
}

double EvaluationCellFilterProxyModel::minScore() const
{
    return min_score_;
}

void EvaluationCellFilterProxyModel::setMinScore(const double value)
{
    const double normalized = std::isfinite(value) ? value : -std::numeric_limits<double>::infinity();
    if (qFuzzyCompare(min_score_, normalized)
        || (std::isinf(min_score_) && std::isinf(normalized) && min_score_ == normalized))
        return;
    min_score_ = normalized;
    invalidateFilter();
    emit filterChanged();
}

double EvaluationCellFilterProxyModel::maxScore() const
{
    return max_score_;
}

void EvaluationCellFilterProxyModel::setMaxScore(const double value)
{
    const double normalized = std::isfinite(value) ? value : std::numeric_limits<double>::infinity();
    if (qFuzzyCompare(max_score_, normalized)
        || (std::isinf(max_score_) && std::isinf(normalized) && max_score_ == normalized))
        return;
    max_score_ = normalized;
    invalidateFilter();
    emit filterChanged();
}

bool EvaluationCellFilterProxyModel::acceptsRecord(const EvaluationInstanceRecord &record) const
{
    if (record.score < min_score_ || record.score > max_score_)
        return false;
    if (!status_.isEmpty())
    {
        const evaluation::Status status = evaluation::statusFromKey(status_);
        if (status == evaluation::Status::Unknown || record.status != status)
            return false;
    }
    if (!pred_class_ids_.isEmpty())
    {
        bool match = false;
        for (const QVariant &value : pred_class_ids_)
            match = match || value.toInt() == record.pred_class_id;
        if (!match)
            return false;
    }

    const bool has_row = !matrix_row_.isEmpty();
    const bool has_column = !matrix_column_.isEmpty();
    if (!has_row && !has_column)
        return true;
    const QString matrix_fn = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::FalseNegative);
    const QString matrix_fp = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::FalsePositive);
    const QString matrix_unmatched_fn
        = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::UnmatchedGroundTruth);
    const QString matrix_unmatched_fp
        = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::UnmatchedPrediction);
    const QString matrix_total = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::Total);
    const bool error_record = record.status == evaluation::Status::ClassMismatch
                              || record.status == evaluation::Status::FalsePositive
                              || record.status == evaluation::Status::FalseNegative;
    if (has_row && matrix_row_ != matrix_total)
    {
        if (matrix_row_ == matrix_fn)
        {
            if (!error_record)
                return false;
        }
        else if (matrix_row_ == matrix_unmatched_fn)
        {
            if (record.status != evaluation::Status::FalseNegative)
                return false;
        }
        else if (matrix_row_ == matrix_unmatched_fp)
        {
            if (record.status != evaluation::Status::FalsePositive)
                return false;
        }
        else if (record.pred_class_id != matrix_row_.toInt())
            return false;
    }
    if (has_column && matrix_column_ != matrix_total)
    {
        if (matrix_column_ == matrix_fp)
        {
            if (!error_record)
                return false;
        }
        else if (matrix_column_ == matrix_unmatched_fp)
        {
            if (record.status != evaluation::Status::FalsePositive)
                return false;
        }
        else if (matrix_column_ == matrix_unmatched_fn)
        {
            if (record.status != evaluation::Status::FalseNegative)
                return false;
        }
        else if (record.gt_class_id != matrix_column_.toInt())
            return false;
    }
    return true;
}

bool EvaluationCellFilterProxyModel::filterAcceptsRow(const int source_row,
                                                       const QModelIndex &source_parent) const
{
    const QAbstractItemModel *source = sourceModel();
    if (source == nullptr)
        return false;
    const EvaluationInstanceRecord *record
        = instanceRecordFromModel(source, source->index(source_row, 0, source_parent));
    return record != nullptr && acceptsRecord(*record);
}

EvaluationChartModel::EvaluationChartModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int EvaluationChartModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : records_.size();
}

QVariant EvaluationChartModel::data(const QModelIndex &index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};
    const QVariantMap &record = records_.at(index.row());
    switch (role)
    {
    case Qt::DisplayRole:
    case TitleRole: return record.value(evaluation::fieldName(evaluation::Field::Title));
    case KindRole: return record.value(evaluation::fieldName(evaluation::Field::Kind));
    case DataRole: return record.value(evaluation::fieldName(evaluation::Field::Data));
    case OptionsRole: return record.value(evaluation::fieldName(evaluation::Field::Options));
    default: return {};
    }
}

QHash<int, QByteArray> EvaluationChartModel::roleNames() const
{
    return {{KindRole, "kind"}, {TitleRole, "title"}, {DataRole, "data"}, {OptionsRole, "options"}};
}

void EvaluationChartModel::setRecords(QList<QVariantMap> records)
{
    if (records_ == records)
        return;

    const int old_size = records_.size();
    const int new_size = records.size();
    int       common   = 0;
    while (common < old_size && common < new_size && records_.at(common) == records.at(common))
        ++common;

    /**
     * @brief 图表仅在尾部增删时保留公共前缀的持久索引。
     */
    if (common == std::min(old_size, new_size))
    {
        if (new_size > old_size)
        {
            beginInsertRows({}, old_size, new_size - 1);
            for (int index = old_size; index < new_size; ++index)
                records_.push_back(std::move(records[index]));
            endInsertRows();
            return;
        }
        if (new_size < old_size)
        {
            beginRemoveRows({}, new_size, old_size - 1);
            records_.resize(new_size);
            endRemoveRows();
            return;
        }
    }

    if (old_size == new_size)
    {
        records_ = std::move(records);
        if (new_size > 0)
            emit dataChanged(index(0), index(new_size - 1), {KindRole, TitleRole, DataRole, OptionsRole});
        return;
    }

    beginResetModel();
    records_ = std::move(records);
    endResetModel();
}

const QList<QVariantMap> &EvaluationChartModel::records() const
{
    return records_;
}

QVariantMap EvaluationChartModel::descriptor(const int row) const
{
    return row >= 0 && row < records_.size() ? records_.at(row) : QVariantMap{};
}

}
