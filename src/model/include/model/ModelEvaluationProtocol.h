#pragma once

#include "dltool/model/Export.h"
#include "core/CoreDef.h"

#include <QString>
#include <QObject>
#include <QVariantMap>
#include <QQmlEngine>
#include <QJSEngine>
#include <QtQml>

namespace dltool::model::evaluation {

Q_NAMESPACE_EXPORT(MODEL_API)
QML_NAMED_ELEMENT(EvaluationProtocol)

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
Q_ENUM_NS(Status)

enum class MatchingStrategy
{
    GreedyIoU = 0,
    HungarianIoU,
};
Q_ENUM_NS(MatchingStrategy)

enum class MetricSet
{
    Diagnostic = 0,
    Official,
};
Q_ENUM_NS(MetricSet)

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
Q_ENUM_NS(CellKind)

enum class MatrixAxisKey
{
    FalseNegative = 0,
    FalsePositive,
    UnmatchedGroundTruth,
    UnmatchedPrediction,
    Total,
};
Q_ENUM_NS(MatrixAxisKey)

/**
 * @brief 图表渲染类型。
 *
 * Chart.js 的 chart type 只支持协议枚举中的值；QML 不再散落 "line"/"bar"
 * 字面量，统一通过 chartKindKey() 映射。
 */
enum class ChartKind
{
    Unknown = 0,
    Line,
    Bar,
    Pie,
};
Q_ENUM_NS(ChartKind)

/**
 * @brief Chart.js 坐标轴 ID。
 *
 * 异常分数分布图使用固定轴 ID，QML 与 C++ 都通过映射函数获取，
 * 避免两处散落 "score-axis"/"count-axis" 字面量。
 */
enum class ChartAxisId
{
    Unknown = 0,
    ScoreAxis,
    CountAxis,
};
Q_ENUM_NS(ChartAxisId)

/**
 * @brief 方法图表的稳定标识。
 *
 * 旧结果中可能仍保存历史字符串，chartIdFromKey() 负责容错解析。
 */
enum class ChartId
{
    Unknown = 0,
    AnomalyScoreDistribution,
    PrecisionRecall,
    PerClassMetrics,
};
Q_ENUM_NS(ChartId)

/**
 * @brief 图表数据集的系列语义。
 *
 * series_kind 是稳定协议字段，QML 图例/工具提示按该枚举区分处理。
 */
enum class SeriesKind
{
    Unknown = 0,
    Good,
    Anomaly,
    Average,
    Class,
};
Q_ENUM_NS(SeriesKind)

/**
 * @brief 图表过滤语义。
 *
 * 实例网格按 filter_kind 联动方法图表；该枚举覆盖评估协议使用的全部值。
 */
enum class FilterKind
{
    Unknown = 0,
    ImageScore,
    PrecisionRecall,
    PerClassMetrics,
};
Q_ENUM_NS(FilterKind)

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
Q_ENUM_NS(DisplayText)

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
Q_ENUM_NS(ViewState)

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
MODEL_API QString chartKindKey(ChartKind kind);
MODEL_API ChartKind chartKindFromKey(const QString &key);
MODEL_API QString chartAxisIdKey(ChartAxisId id);
MODEL_API ChartAxisId chartAxisIdFromKey(const QString &key);
MODEL_API QString chartIdKey(ChartId id);
MODEL_API ChartId chartIdFromKey(const QString &key);
MODEL_API QString seriesKindKey(SeriesKind kind);
MODEL_API SeriesKind seriesKindFromKey(const QString &key);
MODEL_API QString filterKindKey(FilterKind kind);
MODEL_API FilterKind filterKindFromKey(const QString &key);
MODEL_API QString matrixAxisKey(MatrixAxisKey key);
MODEL_API QString matrixAxisLabel(MatrixAxisKey key);
MODEL_API QString displayText(DisplayText text);
MODEL_API QString viewStateKey(ViewState state);
MODEL_API ViewState viewStateFromKey(const QString &key);
MODEL_API QString fieldName(Field field);

MODEL_API bool isAnomaly(Method method);
MODEL_API bool hasInstanceMetrics(Method method);
MODEL_API bool hasImageMetrics(Method method);
MODEL_API bool hasConfusionMatrix(Method method);
MODEL_API bool hasInstanceEvents(Method method);

/**
 * @brief 评估协议 key 映射的 QML 单例。
 *
 * Q_NAMESPACE 只暴露枚举值，QML 无法调用命名空间自由函数；该类把
 * chartIdKey/matrixAxisKey 等字符串映射包装为 Q_INVOKABLE，供 QML 侧
 * 与模型返回的稳定 key 比较，避免在 QML 中散落字符串字面量。
 */
class MODEL_API EvaluationProtocolKeys : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(EvaluationProtocolKeys)
    QML_SINGLETON

    Q_PROPERTY(QString chartKindLine READ chartKindLine CONSTANT FINAL)
    Q_PROPERTY(QString chartKindBar READ chartKindBar CONSTANT FINAL)
    Q_PROPERTY(QString chartKindPie READ chartKindPie CONSTANT FINAL)
    Q_PROPERTY(QString chartAxisScore READ chartAxisScore CONSTANT FINAL)
    Q_PROPERTY(QString chartAxisCount READ chartAxisCount CONSTANT FINAL)
    Q_PROPERTY(QString chartIdAnomalyScoreDistribution READ chartIdAnomalyScoreDistribution CONSTANT FINAL)
    Q_PROPERTY(QString chartIdPrecisionRecall READ chartIdPrecisionRecall CONSTANT FINAL)
    Q_PROPERTY(QString chartIdPerClassMetrics READ chartIdPerClassMetrics CONSTANT FINAL)
    Q_PROPERTY(QString seriesKindGood READ seriesKindGood CONSTANT FINAL)
    Q_PROPERTY(QString seriesKindAnomaly READ seriesKindAnomaly CONSTANT FINAL)
    Q_PROPERTY(QString seriesKindAverage READ seriesKindAverage CONSTANT FINAL)
    Q_PROPERTY(QString seriesKindClass READ seriesKindClass CONSTANT FINAL)
    Q_PROPERTY(QString filterKindImageScore READ filterKindImageScore CONSTANT FINAL)
    Q_PROPERTY(QString filterKindPrecisionRecall READ filterKindPrecisionRecall CONSTANT FINAL)
    Q_PROPERTY(QString filterKindPerClassMetrics READ filterKindPerClassMetrics CONSTANT FINAL)
    Q_PROPERTY(QString matrixAxisFalseNegative READ matrixAxisFalseNegative CONSTANT FINAL)
    Q_PROPERTY(QString matrixAxisFalsePositive READ matrixAxisFalsePositive CONSTANT FINAL)
    Q_PROPERTY(QString matrixAxisUnmatchedGroundTruth READ matrixAxisUnmatchedGroundTruth CONSTANT FINAL)
    Q_PROPERTY(QString matrixAxisUnmatchedPrediction READ matrixAxisUnmatchedPrediction CONSTANT FINAL)
    Q_PROPERTY(QString matrixAxisTotal READ matrixAxisTotal CONSTANT FINAL)

public:
    explicit EvaluationProtocolKeys(QObject *parent = nullptr);

    QString chartKindLine() const;
    QString chartKindBar() const;
    QString chartKindPie() const;
    QString chartAxisScore() const;
    QString chartAxisCount() const;
    QString chartIdAnomalyScoreDistribution() const;
    QString chartIdPrecisionRecall() const;
    QString chartIdPerClassMetrics() const;
    QString seriesKindGood() const;
    QString seriesKindAnomaly() const;
    QString seriesKindAverage() const;
    QString seriesKindClass() const;
    QString filterKindImageScore() const;
    QString filterKindPrecisionRecall() const;
    QString filterKindPerClassMetrics() const;
    QString matrixAxisFalseNegative() const;
    QString matrixAxisFalsePositive() const;
    QString matrixAxisUnmatchedGroundTruth() const;
    QString matrixAxisUnmatchedPrediction() const;
    QString matrixAxisTotal() const;

    Q_INVOKABLE QString chartKindKey(int kind) const;
    Q_INVOKABLE QString chartAxisIdKey(int id) const;
    Q_INVOKABLE QString chartIdKey(int id) const;
    Q_INVOKABLE QString seriesKindKey(int kind) const;
    Q_INVOKABLE QString filterKindKey(int kind) const;
    Q_INVOKABLE QString matrixAxisKey(int key) const;
    Q_INVOKABLE QString statusKey(int status) const;
    Q_INVOKABLE QString fieldName(int field) const;

    static EvaluationProtocolKeys *create(QQmlEngine *, QJSEngine *);
};

}
