#pragma once

#include "dltool/model/Export.h"
#include "core/CoreDef.h"

#include <QString>
#include <QObject>
#include <QVariantMap>

namespace dltool::model::evaluation {

Q_NAMESPACE_EXPORT(MODEL_API)

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
 * @brief 评估界面使用的集中显示文案。
 *
 * 协议 key 与显示文本分离：row_key/column_key 等稳定值不用于展示，
 * 展示层只消费 C++ 生成的 label，避免 QML 根据 GOOD/Anomaly 等文本猜语义。
 */
enum class DisplayText
{
    Good = 0,
    Anomaly,
    Total,
};

/**
 * @brief 评估展示状态。
 *
 * NotRun 表示尚未进行本次评估或任务已停止，Running/Failed 表示任务本身
 * 的运行状态；MissingResult 表示没有可读取的预测输入，InvalidResult 表示
 * 评估服务返回了不完整的结果快照，Error 表示评估执行失败。Ready 只表示
 * 当前快照已经通过协议校验并可供界面使用。字符串 key 仅保留在协议边界。
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
    SeriesKind,
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
Q_ENUM_NS(Field)

/**
 * @brief 评估快照仅存在于当前进程内，不持久化。
 *
 * 旧版生成的报告不会被读取或迁移。
 */
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
MODEL_API QString displayText(DisplayText text);
MODEL_API QString viewStateKey(ViewState state);
MODEL_API ViewState viewStateFromKey(const QString &key);
MODEL_API QString fieldName(Field field);

MODEL_API bool isAnomaly(Method method);
MODEL_API bool hasInstanceMetrics(Method method);
MODEL_API bool hasImageMetrics(Method method);
MODEL_API bool hasConfusionMatrix(Method method);
MODEL_API bool hasInstanceEvents(Method method);

}
