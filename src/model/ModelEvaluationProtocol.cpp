#include "model/ModelEvaluationProtocol.h"

namespace dltool::model::evaluation {

namespace {

QString normalized(const QString &value)
{
    return value.trimmed().toLower();
}

} // namespace

Method fromProjectMethod(const int method)
{
    return fromProjectMethod(static_cast<core::DeepLearningMethod::Method>(method));
}

Method fromProjectMethod(const core::DeepLearningMethod::Method method)
{
    switch (method)
    {
    case core::DeepLearningMethod::Classification:
    case core::DeepLearningMethod::Detection:
    case core::DeepLearningMethod::Segmentation:
    case core::DeepLearningMethod::AnomalyDetection: return method;
    case core::DeepLearningMethod::Pose:
    case core::DeepLearningMethod::OCR:
    case core::DeepLearningMethod::Unknown:
    default: return Method::Unknown;
    }
}

Method methodFromKey(const QString &key)
{
    const QString value = normalized(key);
    if (value == QStringLiteral("classification"))
        return Method::Classification;
    if (value == QStringLiteral("object_detection"))
        return Method::Detection;
    if (value == QStringLiteral("segmentation"))
        return Method::Segmentation;
    if (value == QStringLiteral("anomaly_detection"))
        return Method::AnomalyDetection;
    return Method::Unknown;
}

QString methodKey(const Method method)
{
    switch (method)
    {
    case Method::Classification: return QStringLiteral("classification");
    case Method::Detection: return QStringLiteral("object_detection");
    case Method::Segmentation: return QStringLiteral("segmentation");
    case Method::AnomalyDetection: return QStringLiteral("anomaly_detection");
    case Method::Unknown:
    case Method::Pose:
    case Method::OCR: return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

QString statusKey(const Status status)
{
    switch (status)
    {
    case Status::TruePositive: return QStringLiteral("true_positive");
    case Status::TrueNegative: return QStringLiteral("true_negative");
    case Status::ClassMismatch: return QStringLiteral("class_mismatch");
    case Status::FalsePositive: return QStringLiteral("false_positive");
    case Status::FalseNegative: return QStringLiteral("false_negative");
    case Status::Ignored: return QStringLiteral("ignored");
    case Status::Unknown: return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

Status statusFromKey(const QString &key)
{
    const QString value = normalized(key);
    if (value == statusKey(Status::TruePositive))
        return Status::TruePositive;
    if (value == statusKey(Status::TrueNegative))
        return Status::TrueNegative;
    if (value == statusKey(Status::ClassMismatch))
        return Status::ClassMismatch;
    if (value == statusKey(Status::FalsePositive))
        return Status::FalsePositive;
    if (value == statusKey(Status::FalseNegative))
        return Status::FalseNegative;
    if (value == statusKey(Status::Ignored))
        return Status::Ignored;
    return Status::Unknown;
}

QString statusDisplayName(const Status status)
{
    switch (status)
    {
    case Status::TruePositive: return QString("正确匹配");
    case Status::TrueNegative: return QString("正常");
    case Status::ClassMismatch: return QString("类别错误");
    case Status::FalsePositive: return QString("误检");
    case Status::FalseNegative: return QString("漏检");
    case Status::Ignored: return QString("已忽略");
    case Status::Unknown: return QString("未知");
    }
    return QString("未知");
}

QString matchingStrategyKey(const MatchingStrategy strategy)
{
    switch (strategy)
    {
    case MatchingStrategy::HungarianIoU: return QStringLiteral("hungarian_iou");
    case MatchingStrategy::GreedyIoU: return QStringLiteral("greedy_iou");
    }
    return QStringLiteral("greedy_iou");
}

MatchingStrategy matchingStrategyFromKey(const QString &key)
{
    const QString value = normalized(key);
    if (value == matchingStrategyKey(MatchingStrategy::HungarianIoU))
        return MatchingStrategy::HungarianIoU;
    return MatchingStrategy::GreedyIoU;
}

QVariantMap normalizedEvaluationConfig(const QVariantMap &source)
{
    // Keep the complete user-owned evaluation group in the in-memory snapshot.
    // The evaluator consumes only the fields it understands below, while
    // preserving the remaining evaluation-only parameters lets a change to
    // e.g. a metric selector trigger C++ re-evaluation without rerunning
    // inference.  Inference parameters are kept outside this map and are
    // compared by ModelTaskPreparation.
    QVariantMap normalized = source;
    normalized.insert(fieldName(Field::ConfidenceThreshold),
                      source.value(fieldName(Field::ConfidenceThreshold), kDefaultConfidenceThreshold).toDouble());
    normalized.insert(fieldName(Field::IouThreshold),
                      source.value(fieldName(Field::IouThreshold), kDefaultIouThreshold).toDouble());
    normalized.insert(fieldName(Field::MatchingStrategy),
                      matchingStrategyKey(matchingStrategyFromKey(
                          source.value(fieldName(Field::MatchingStrategy),
                                       matchingStrategyKey(MatchingStrategy::GreedyIoU))
                              .toString())));
    return normalized;
}

QString metricSetKey(const MetricSet metric_set)
{
    switch (metric_set)
    {
    case MetricSet::Official: return QStringLiteral("official_metrics");
    case MetricSet::Diagnostic: return QStringLiteral("diagnostic_metrics");
    }
    return QStringLiteral("diagnostic_metrics");
}

MetricSet metricSetFromKey(const QString &key)
{
    return normalized(key) == metricSetKey(MetricSet::Official) ? MetricSet::Official : MetricSet::Diagnostic;
}

QString cellKindKey(const CellKind kind)
{
    switch (kind)
    {
    case CellKind::Match: return QStringLiteral("match");
    case CellKind::ClassMismatch: return QStringLiteral("class_mismatch");
    case CellKind::FalsePositive: return QStringLiteral("false_positive");
    case CellKind::FalseNegative: return QStringLiteral("false_negative");
    case CellKind::PredTotal: return QStringLiteral("pred_total");
    case CellKind::GtTotal: return QStringLiteral("gt_total");
    case CellKind::FalsePositiveTotal: return QStringLiteral("false_positive_total");
    case CellKind::FalseNegativeTotal: return QStringLiteral("false_negative_total");
    case CellKind::All: return QStringLiteral("all");
    case CellKind::NotApplicable: return QStringLiteral("not_applicable");
    }
    return QStringLiteral("not_applicable");
}

CellKind cellKindFromKey(const QString &key)
{
    const QString value = normalized(key);
    for (const CellKind kind : {CellKind::Match, CellKind::ClassMismatch, CellKind::FalsePositive,
                                CellKind::FalseNegative, CellKind::PredTotal, CellKind::GtTotal,
                                CellKind::FalsePositiveTotal, CellKind::FalseNegativeTotal, CellKind::All,
                                CellKind::NotApplicable})
    {
        if (value == cellKindKey(kind))
            return kind;
    }
    return CellKind::NotApplicable;
}

QString matrixAxisKey(const MatrixAxisKey key)
{
    switch (key)
    {
    case MatrixAxisKey::FalseNegative: return QStringLiteral("FN");
    case MatrixAxisKey::FalsePositive: return QStringLiteral("FP");
    case MatrixAxisKey::Total: return QStringLiteral("TOTAL");
    }
    return {};
}

QString viewStateKey(const ViewState state)
{
    switch (state)
    {
    case ViewState::NotRun: return QStringLiteral("NotRun");
    case ViewState::Loading: return QStringLiteral("Loading");
    case ViewState::Running: return QStringLiteral("Running");
    case ViewState::Failed: return QStringLiteral("Failed");
    case ViewState::MissingResult: return QStringLiteral("MissingResult");
    case ViewState::InvalidResult: return QStringLiteral("InvalidResult");
    case ViewState::Error: return QStringLiteral("Error");
    case ViewState::Ready: return QStringLiteral("Ready");
    }
    return QStringLiteral("NotRun");
}

QString fieldName(const Field field)
{
    switch (field)
    {
    case Field::SchemaVersion: return QStringLiteral("schema_version");
    case Field::ModelUuid: return QStringLiteral("model_uuid");
    case Field::TestTaskUuid: return QStringLiteral("test_task_uuid");
    case Field::Method: return QStringLiteral("method");
    case Field::Status: return QStringLiteral("status");
    case Field::PrimaryMetricSet: return QStringLiteral("primary_metric_set");
    case Field::EvaluatedAt: return QStringLiteral("evaluated_at");
    case Field::EvaluationConfig: return QStringLiteral("evaluation_config");
    case Field::ConfidenceThreshold: return QStringLiteral("confidence_threshold");
    case Field::IouThreshold: return QStringLiteral("iou_threshold");
    case Field::MatchingStrategy: return QStringLiteral("matching_strategy");
    case Field::ClassCatalog: return QStringLiteral("class_catalog");
    case Field::DiagnosticMetrics: return QStringLiteral("diagnostic_metrics");
    case Field::OfficialMetrics: return QStringLiteral("official_metrics");
    case Field::ImageMetricDefinition: return QStringLiteral("image_metric_definition");
    case Field::Capabilities: return QStringLiteral("capabilities");
    case Field::HasInstanceMetrics: return QStringLiteral("has_instance_metrics");
    case Field::HasImageMetrics: return QStringLiteral("has_image_metrics");
    case Field::HasConfusionMatrix: return QStringLiteral("has_confusion_matrix");
    case Field::HasInstanceEvents: return QStringLiteral("has_instance_events");
    case Field::ChartKinds: return QStringLiteral("chart_kinds");
    case Field::ConfusionMatrix: return QStringLiteral("confusion_matrix");
    case Field::Cells: return QStringLiteral("cells");
    case Field::Charts: return QStringLiteral("charts");
    case Field::ImageRecords: return QStringLiteral("image_records");
    case Field::InstanceRecords: return QStringLiteral("instance_records");
    case Field::ImageCount: return QStringLiteral("image_count");
    case Field::PredictionCount: return QStringLiteral("prediction_count");
    case Field::EventCount: return QStringLiteral("event_count");
    case Field::Available: return QStringLiteral("available");
    case Field::Definition: return QStringLiteral("definition");
    case Field::Instance: return QStringLiteral("instance");
    case Field::Overall: return QStringLiteral("overall");
    case Field::PerClass: return QStringLiteral("per_class");
    case Field::Image: return QStringLiteral("image");
    case Field::Precision: return QStringLiteral("precision");
    case Field::Recall: return QStringLiteral("recall");
    case Field::F1: return QStringLiteral("f1");
    case Field::PrecisionDefined: return QStringLiteral("precision_defined");
    case Field::RecallDefined: return QStringLiteral("recall_defined");
    case Field::F1Defined: return QStringLiteral("f1_defined");
    case Field::Tp: return QStringLiteral("tp");
    case Field::Fp: return QStringLiteral("fp");
    case Field::Fn: return QStringLiteral("fn");
    case Field::SampleUnit: return QStringLiteral("sample_unit");
    case Field::Aggregation: return QStringLiteral("aggregation");
    case Field::PositiveDefinition: return QStringLiteral("positive_definition");
    case Field::RecordCount: return QStringLiteral("record_count");
    case Field::Records: return QStringLiteral("records");
    case Field::PredictionId: return QStringLiteral("prediction_id");
    case Field::ImageId: return QStringLiteral("image_id");
    case Field::DatasetId: return QStringLiteral("dataset_id");
    case Field::ImageName: return QStringLiteral("image_name");
    case Field::ImagePath: return QStringLiteral("image_path");
    case Field::ImageWidth: return QStringLiteral("image_width");
    case Field::ImageHeight: return QStringLiteral("image_height");
    case Field::Id: return QStringLiteral("id");
    case Field::Name: return QStringLiteral("name");
    case Field::Color: return QStringLiteral("color");
    case Field::GtInstances: return QStringLiteral("gt_instances");
    case Field::Predictions: return QStringLiteral("predictions");
    case Field::LabelId: return QStringLiteral("label_id");
    case Field::ClassId: return QStringLiteral("class_id");
    case Field::ClassName: return QStringLiteral("class_name");
    case Field::Geometry: return QStringLiteral("geometry");
    case Field::X: return QStringLiteral("x");
    case Field::Y: return QStringLiteral("y");
    case Field::Cx: return QStringLiteral("cx");
    case Field::Cy: return QStringLiteral("cy");
    case Field::Score: return QStringLiteral("score");
    case Field::EventUuid: return QStringLiteral("event_uuid");
    case Field::Iou: return QStringLiteral("iou");
    case Field::GtLabelId: return QStringLiteral("gt_label_id");
    case Field::GtClassId: return QStringLiteral("gt_class_id");
    case Field::GtClassName: return QStringLiteral("gt_class_name");
    case Field::GtGeometry: return QStringLiteral("gt_geometry");
    case Field::PredInstanceId: return QStringLiteral("pred_instance_id");
    case Field::PredClassId: return QStringLiteral("pred_class_id");
    case Field::PredClassName: return QStringLiteral("pred_class_name");
    case Field::PredGeometry: return QStringLiteral("pred_geometry");
    case Field::CropBounds: return QStringLiteral("crop_bounds");
    case Field::GtOverlayBounds: return QStringLiteral("gt_overlay_bounds");
    case Field::PredOverlayBounds: return QStringLiteral("pred_overlay_bounds");
    case Field::GtOverlayPoints: return QStringLiteral("gt_overlay_points");
    case Field::PredOverlayPoints: return QStringLiteral("pred_overlay_points");
    case Field::GtMaskUrl: return QStringLiteral("gt_mask_url");
    case Field::PredMaskUrl: return QStringLiteral("pred_mask_url");
    case Field::RowKey: return QStringLiteral("row_key");
    case Field::ColumnKey: return QStringLiteral("column_key");
    case Field::RowLabel: return QStringLiteral("row_label");
    case Field::ColumnLabel: return QStringLiteral("column_label");
    case Field::RowClassId: return QStringLiteral("row_class_id");
    case Field::ColumnClassId: return QStringLiteral("column_class_id");
    case Field::Count: return QStringLiteral("count");
    case Field::CellKind: return QStringLiteral("cell_kind");
    case Field::Selectable: return QStringLiteral("selectable");
    case Field::IsDiagonal: return QStringLiteral("is_diagonal");
    case Field::IsError: return QStringLiteral("is_error");
    case Field::Kind: return QStringLiteral("kind");
    case Field::ChartId: return QStringLiteral("chart_id");
    case Field::FilterKind: return QStringLiteral("filter_kind");
    case Field::Title: return QStringLiteral("title");
    case Field::Data: return QStringLiteral("data");
    case Field::Options: return QStringLiteral("options");
    case Field::Labels: return QStringLiteral("labels");
    case Field::Datasets: return QStringLiteral("datasets");
    case Field::Label: return QStringLiteral("label");
    case Field::Images: return QStringLiteral("images");
    case Field::Samples: return QStringLiteral("samples");
    case Field::LabelClassId: return QStringLiteral("label_class_id");
    case Field::LabelClassName: return QStringLiteral("label_class_name");
    case Field::Yolo: return QStringLiteral("yolo");
    case Field::LabelIndex: return QStringLiteral("label_index");
    case Field::Path: return QStringLiteral("path");
    case Field::Width: return QStringLiteral("width");
    case Field::Height: return QStringLiteral("height");
    case Field::Type: return QStringLiteral("type");
    case Field::CoordinateSystem: return QStringLiteral("coordinate_system");
    case Field::Format: return QStringLiteral("format");
    case Field::Values: return QStringLiteral("values");
    case Field::Points: return QStringLiteral("points");
    case Field::Bounds: return QStringLiteral("bounds");
    case Field::ArtifactPath: return QStringLiteral("artifact_path");
    }
    return {};
}

bool isAnomaly(const Method method)
{
    return method == Method::AnomalyDetection;
}

bool hasInstanceMetrics(const Method method)
{
    return method == Method::Detection || method == Method::Segmentation;
}

bool hasImageMetrics(const Method method)
{
    return hasInstanceMetrics(method) || isAnomaly(method);
}

bool hasConfusionMatrix(const Method method)
{
    return hasInstanceMetrics(method) || isAnomaly(method);
}

bool hasInstanceEvents(const Method method)
{
    return hasInstanceMetrics(method);
}

} // namespace dltool::model::evaluation
