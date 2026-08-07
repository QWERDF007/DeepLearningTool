#pragma once

#include "dltool/model/Export.h"
#include "core/CoreDef.h"

#include <QString>
#include <QVariantMap>

namespace dltool::model::evaluation {

/**
 * @brief 评估层使用的统一方法类型。
 *
 * 评估直接复用项目级方法枚举，避免在任务请求、评估服务和结果协议之间
 * 维护第二套方法类型。Unknown 仅用于未初始化或不受评估协议支持的方法。
 */
using Method = core::DeepLearningMethod::Method;

enum class Status
{
    Unknown = 0,
    TruePositive,
    TrueNegative,
    ClassMismatch,
    FalsePositive,
    FalseNegative,
    Ignored,
};

enum class MatchingStrategy
{
    GreedyIoU = 0,
    HungarianIoU,
};

enum class MetricSet
{
    Diagnostic = 0,
    Official,
};

enum class CellKind
{
    Match = 0,
    ClassMismatch,
    FalsePositive,
    FalseNegative,
    PredTotal,
    GtTotal,
    FalsePositiveTotal,
    FalseNegativeTotal,
    All,
    NotApplicable,
};

enum class MatrixAxisKey
{
    FalseNegative = 0,
    FalsePositive,
    Total,
};

/**
 * @brief 评估展示状态。
 *
 * 状态仍以字符串形式暴露给现有 QML 属性，但所有 C++ 状态转换都通过
 * 此枚举完成，避免任务管理器和展示模型各自维护一套字面量。
 */
enum class ViewState
{
    NotRun = 0,
    Loading,
    Running,
    Failed,
    MissingResult,
    InvalidResult,
    Error,
    Ready,
};

/**
 * @brief 稳定评估协议字段。
 *
 * 业务代码只通过该枚举访问内存评估快照中的固定字段；字段字符串只在
 * 协议边界的映射函数中维护。评估快照不写入任何报告文件。
 */
enum class Field
{
    SchemaVersion,
    ModelUuid,
    TestTaskUuid,
    Method,
    Status,
    PrimaryMetricSet,
    EvaluatedAt,
    EvaluationConfig,
    ConfidenceThreshold,
    IouThreshold,
    MatchingStrategy,
    ClassCatalog,
    DiagnosticMetrics,
    OfficialMetrics,
    ImageMetricDefinition,
    Capabilities,
    HasInstanceMetrics,
    HasImageMetrics,
    HasConfusionMatrix,
    HasInstanceEvents,
    ChartKinds,
    ConfusionMatrix,
    Cells,
    Charts,
    ImageRecords,
    InstanceRecords,
    ImageCount,
    PredictionCount,
    EventCount,
    Available,
    Definition,
    Instance,
    Overall,
    PerClass,
    Image,
    Precision,
    Recall,
    F1,
    PrecisionDefined,
    RecallDefined,
    F1Defined,
    Tp,
    Fp,
    Fn,
    SampleUnit,
    Aggregation,
    PositiveDefinition,
    RecordCount,
    Records,
    PredictionId,
    ImageId,
    DatasetId,
    ImageName,
    ImagePath,
    ImageWidth,
    ImageHeight,
    Id,
    Name,
    Color,
    GtInstances,
    Predictions,
    LabelId,
    ClassId,
    ClassName,
    IsAnomaly,
    Geometry,
    X,
    Y,
    Cx,
    Cy,
    Score,
    EventUuid,
    Iou,
    GtLabelId,
    GtClassId,
    GtClassName,
    GtGeometry,
    PredInstanceId,
    PredClassId,
    PredClassName,
    PredGeometry,
    CropBounds,
    GtOverlayBounds,
    PredOverlayBounds,
    GtOverlayPoints,
    PredOverlayPoints,
    GtMaskUrl,
    PredMaskUrl,
    RowKey,
    ColumnKey,
    RowLabel,
    ColumnLabel,
    RowClassId,
    ColumnClassId,
    Count,
    CellKind,
    Selectable,
    IsDiagonal,
    IsError,
    Kind,
    ChartId,
    FilterKind,
    Title,
    Data,
    Options,
    Labels,
    Datasets,
    Label,
    Images,
    Samples,
    LabelClassId,
    LabelClassName,
    Yolo,
    LabelIndex,
    Path,
    Width,
    Height,
    Type,
    CoordinateSystem,
    Format,
    Values,
    Points,
    Bounds,
    ArtifactPath,
};

// Evaluation snapshots are process-local and are intentionally not persisted.
// This is a destructive protocol change: previously generated reports are
// intentionally not read or migrated.
constexpr double kDefaultConfidenceThreshold = 0.5;
constexpr double kDefaultIouThreshold = 0.5;

MODEL_API Method fromProjectMethod(int method);
MODEL_API Method fromProjectMethod(core::DeepLearningMethod::Method method);
MODEL_API Method methodFromKey(const QString &key);
MODEL_API QString methodKey(Method method);

MODEL_API QString statusKey(Status status);
MODEL_API Status statusFromKey(const QString &key);
MODEL_API QString statusDisplayName(Status status);

MODEL_API QString matchingStrategyKey(MatchingStrategy strategy);
MODEL_API MatchingStrategy matchingStrategyFromKey(const QString &key);

/**
 * @brief 将可编辑测试参数中的评估配置规范化为唯一的结果配置。
 *
 * C++ 评估输入和内存缓存使用同一组默认值及规范化的 matching_strategy，
 * 避免在不同流程中各自解释评估参数。
 */
MODEL_API QVariantMap normalizedEvaluationConfig(const QVariantMap &source);

MODEL_API QString metricSetKey(MetricSet metric_set);
MODEL_API MetricSet metricSetFromKey(const QString &key);
MODEL_API QString cellKindKey(CellKind kind);
MODEL_API CellKind cellKindFromKey(const QString &key);
MODEL_API QString matrixAxisKey(MatrixAxisKey key);
MODEL_API QString viewStateKey(ViewState state);
MODEL_API QString fieldName(Field field);

MODEL_API bool isAnomaly(Method method);
MODEL_API bool hasInstanceMetrics(Method method);
MODEL_API bool hasImageMetrics(Method method);
MODEL_API bool hasConfusionMatrix(Method method);
MODEL_API bool hasInstanceEvents(Method method);

} // namespace dltool::model::evaluation
