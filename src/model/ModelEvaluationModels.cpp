#include "model/ModelEvaluationModels.h"
#include "model/ModelEvaluationProtocol.h"

#include <algorithm>
#include <cmath>
#include <QMetaMethod>
#include <QMetaObject>
#include <QSet>

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

const QList<int> &predClassIds(const EvaluationImageRecord &record)
{
    return record.pred_class_ids;
}

void cacheImageDerivedValues(EvaluationImageRecord &record)
{
    record.gt_label_ids.clear();
    record.gt_class_ids.clear();
    record.pred_class_ids.clear();
    record.max_prediction_score = 0.0;
    record.has_gt = !record.gt_instances.isEmpty();
    record.has_pred = !record.predictions.isEmpty();

    for (const EvaluationGroundTruthRecord &ground_truth : record.gt_instances)
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
        record.max_prediction_score = std::max(record.max_prediction_score, prediction.score);
    }
}

} // namespace

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
    return parent.isValid() ? 0 : dimension_;
}

int EvaluationConfusionModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : dimension_;
}

QVariant EvaluationConfusionModel::data(const QModelIndex &index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount() || index.column() < 0
        || index.column() >= columnCount())
        return {};
    const auto &record = records_.at(static_cast<size_t>(index.row() * dimension_ + index.column()));
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
    if (role != Qt::DisplayRole || section < 0 || section >= dimension_)
        return {};
    const auto &record = records_.at(static_cast<size_t>(orientation == Qt::Horizontal ? section : section * dimension_));
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
    beginResetModel();
    records_ = std::move(records);
    dimension_ = 0;
    if (!records_.empty())
    {
        dimension_ = static_cast<int>(std::sqrt(static_cast<double>(records_.size())));
        while (dimension_ * dimension_ < static_cast<int>(records_.size()))
            ++dimension_;
        if (dimension_ * dimension_ != static_cast<int>(records_.size()))
            dimension_ = 0;
    }
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
    case ImageNameRole: return record.image_name;
    case ImageIdRole: return record.image_id;
    case DatasetIdRole: return record.dataset_id;
    case ImagePathRole: return record.image_path;
    case ImageWidthRole: return record.image_width;
    case ImageHeightRole: return record.image_height;
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
        cacheImageDerivedValues(record);
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
    setDynamicSortFilter(true);
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
    if (!invokeBool(global_filter_, "acceptsImage", record.image_id, &invoked))
        return invoked ? false : true;

    // Label search/custom label conditions are label-level predicates.  The
    // image proxy must apply them to the GT labels that belong to the image;
    // acceptsImage() alone cannot see the current evaluation record's label IDs.
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

    // GlobalFilter::acceptsImage() covers Dataset, Tag, ImageLabelClass,
    // filename and Custom. LabelClass is an instance-level filter, so image
    // rows explicitly apply its GT labels with the same accept/reject rules.
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

    // LabelClass is a GT-oriented image filter.  Positive selection keeps an
    // image when at least one GT class is selected; inversion keeps it only
    // when none of its GT classes is excluded.  Pure-FP images have no GT
    // class and must not be made visible by treating PRED as GT.
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
    setDynamicSortFilter(true);
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

    // A GT label ID is the authoritative object for LabelSearchResult and
    // other label-level custom predicates.  Pure FP events have no GT label
    // and deliberately do not get a synthetic match for this condition.
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

    // Prefer GT for every matched/missed/mismatched event.  Including PRED
    // here would let a false positive from a selected class pass an event
    // whose GT belongs to a different class.  Only a pure FP falls back to
    // the predicted class.
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
    setDynamicSortFilter(true);
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
    const QString matrix_total = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::Total);
    if (matrix_row_ == matrix_fn && matrix_column_ == matrix_fp)
        return false;
    if (has_row && matrix_row_ != matrix_total)
    {
        if (matrix_row_ == matrix_fn)
        {
            if (record.status != evaluation::Status::FalseNegative)
                return false;
        }
        else if (record.pred_class_id != matrix_row_.toInt())
            return false;
    }
    if (has_column && matrix_column_ != matrix_total)
    {
        if (matrix_column_ == matrix_fp)
        {
            if (record.status != evaluation::Status::FalsePositive)
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

} // namespace dltool::model
