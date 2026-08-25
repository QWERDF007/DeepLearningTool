#pragma once

#include "core/CoreDef.h"
#include "dltool/model/Export.h"

#include <QJSEngine>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantMap>
#include <QtQml>

namespace dltool::model::evaluation {

Q_NAMESPACE_EXPORT(MODEL_API)
QML_NAMED_ELEMENT(EvaluationProtocol)

/**
 * @brief 评估层使用的统一深度学习方法枚举别名。
 *
 * 评估直接复用项目级方法枚举，避免在任务请求、评估服务和结果协议之间
 * 维护第二套方法类型。Unknown 仅用于未初始化或不受评估协议支持的方法。
 */
using Method = core::DeepLearningMethod::Method;

/**
 * @brief 实例或图像匹配结果状态枚举。
 */
enum class Status
{
    Unknown = 0,   ///< 未知状态。
    TruePositive,  ///< 真正例（正确匹配检出）。
    TrueNegative,  ///< 真负例（正确排除）。
    ClassMismatch, ///< 几何重叠达标但类别预测错误。
    FalsePositive, ///< 假正例（多检/误检）。
    FalseNegative, ///< 假负例（漏检）。
    Ignored,       ///< 忽略的样本。
};
Q_ENUM_NS(Status)

/**
 * @brief 预测实例与真值标注之间的匹配算法策略。
 */
enum class MatchingStrategy
{
    GreedyIoU = 0, ///< 贪婪 IoU 优先匹配（按得分/IoU 降序排序后贪婪配对）。
    HungarianIoU,  ///< 匈牙利二分图最大权匹配算法。
};
Q_ENUM_NS(MatchingStrategy)

/**
 * @brief 指标集合类型。
 */
enum class MetricSet
{
    Diagnostic = 0, ///< 诊断型指标集（包含 TP/FP/FN、细粒度错误分类等）。
    Official,       ///< 官方基准指标集（如 COCO mAP、VOC mAP）。
};
Q_ENUM_NS(MetricSet)

/**
 * @brief 混淆矩阵单元格类型枚举。
 */
enum class CellKind
{
    Match = 0,          ///< 对角线正确匹配单元格。
    ClassMismatch,      ///< 类别分类错误单元格。
    FalsePositive,      ///< 假正例单元格（背景误检）。
    FalseNegative,      ///< 假负例单元格（目标漏检）。
    PredTotal,          ///< 预测总计汇总行。
    GtTotal,            ///< GT 标注总计汇总列。
    FalsePositiveTotal, ///< 误检总计行。
    FalseNegativeTotal, ///< 漏检总计列。
    All,                ///< 全局总数单元格。
    NotApplicable,      ///< 不适用或空白单元格。
};
Q_ENUM_NS(CellKind)

/**
 * @brief 混淆矩阵轴关键项枚举。
 */
enum class MatrixAxisKey
{
    FalseNegative = 0,    ///< 漏检（FN）轴。
    FalsePositive,        ///< 误检（FP）轴。
    UnmatchedGroundTruth, ///< 未匹配的真值标注。
    UnmatchedPrediction,  ///< 未匹配的模型预测。
    Total,                ///< 合计轴。
};
Q_ENUM_NS(MatrixAxisKey)

/**
 * @brief 图表渲染类型。
 *
 * Chart.js 的 chart type 只支持协议枚举中的值；QML 统一通过 chartKindKey() 映射。
 */
enum class ChartKind
{
    Unknown = 0, ///< 未知图表类型。
    Line,        ///< 折线图（如 PR 曲线、ROC 曲线）。
    Bar,         ///< 柱状图/直方图（如分数分布）。
    Pie,         ///< 饼图/环形图（如准确率与召回率比例）。
};
Q_ENUM_NS(ChartKind)

/**
 * @brief Chart.js 坐标轴 ID。
 *
 * 异常分数分布图使用固定轴 ID，QML 与 C++ 都通过映射函数获取。
 */
enum class ChartAxisId
{
    Unknown = 0, ///< 未知坐标轴。
    ScoreAxis,   ///< 分数轴（横轴）。
    CountAxis,   ///< 计数频次轴（纵轴）。
};
Q_ENUM_NS(ChartAxisId)

/**
 * @brief 方法图表的稳定标识枚举。
 */
enum class ChartId
{
    Unknown = 0,              ///< 未知图表标识。
    AnomalyScoreDistribution, ///< 异常检测分数分布直方图。
    PrecisionRecall,          ///< 精确率-召回率（PR）曲线图。
    PerClassMetrics,          ///< 按类别多指标对比图。
};
Q_ENUM_NS(ChartId)

/**
 * @brief 图表数据集的系列语义。
 *
 * series_kind 是稳定协议字段，QML 图例/工具提示按该枚举区分处理。
 */
enum class SeriesKind
{
    Unknown = 0, ///< 未知系列。
    Good,        ///< 良品/正常样本系列。
    Anomaly,     ///< 异常/缺陷样本系列。
    Average,     ///< 平均值/总体系列（如 mAP）。
    Class,       ///< 单类别系列。
};
Q_ENUM_NS(SeriesKind)

/**
 * @brief 图表过滤语义枚举。
 *
 * 实例网格按 filter_kind 联动方法图表。
 */
enum class FilterKind
{
    Unknown = 0,     ///< 未知过滤类型。
    ImageScore,      ///< 图像级分数区间过滤。
    PrecisionRecall, ///< PR 曲线操作点置信度联动过滤。
    PerClassMetrics, ///< 单类别指标联动过滤。
};
Q_ENUM_NS(FilterKind)

/**
 * @brief 评估界面使用的集中显示文案枚举。
 *
 * 协议 key 与显示文本分离，展示层只消费 C++ 生成的 label。
 */
enum class DisplayText
{
    Good = 0, ///< "良品 / 正常"
    Anomaly,  ///< "缺陷 / 异常"
    Total,    ///< "合计 / 总数"
};
Q_ENUM_NS(DisplayText)

/**
 * @brief 评估展示状态枚举。
 */
enum class ViewState
{
    NotRun = 0,    ///< 尚未运行本次评估。
    Loading,       ///< 正在加载/读取评估输入数据。
    Running,       ///< 正在执行后台评测计算。
    Failed,        ///< 任务执行或评估计算失败。
    MissingResult, ///< 预测结果文件或标注数据缺失。
    InvalidResult, ///< 结果数据格式无效或不完整。
    Error,         ///< 运行时异常错误。
    Ready,         ///< 评估成功且数据就绪。
};
Q_ENUM_NS(ViewState)

/**
 * @brief 稳定评估协议字段枚举。
 *
 * 业务代码只通过该枚举访问内存评估快照中的固定字段。
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
    AnomalyScoreMapPath,
    AnomalyModelPolygons,
    AnomalyImagePolygons,
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

/** @brief 默认置信度过滤阈值（0.5）。 */
constexpr double kDefaultConfidenceThreshold = 0.5;
/** @brief 默认 IoU 判定阈值（0.5）。 */
constexpr double kDefaultIouThreshold = 0.5;

MODEL_API Method  fromProjectMethod(int method);
MODEL_API Method  fromProjectMethod(core::DeepLearningMethod::Method method);
MODEL_API Method  methodFromKey(const QString &key);
MODEL_API QString methodKey(Method method);

MODEL_API QString statusKey(Status status);
MODEL_API Status  statusFromKey(const QString &key);
MODEL_API QString statusDisplayName(Status status);

MODEL_API QString          matchingStrategyKey(MatchingStrategy strategy);
MODEL_API MatchingStrategy matchingStrategyFromKey(const QString &key);

/**
 * @brief 将可编辑测试参数中的评估配置规范化为唯一的结果配置。
 * @param source 原始评估参数字典。
 * @return 规范化后的参数字典。
 */
MODEL_API QVariantMap normalizedEvaluationConfig(const QVariantMap &source);

MODEL_API QString     metricSetKey(MetricSet metric_set);
MODEL_API MetricSet   metricSetFromKey(const QString &key);
MODEL_API QString     cellKindKey(CellKind kind);
MODEL_API CellKind    cellKindFromKey(const QString &key);
MODEL_API QString     chartKindKey(ChartKind kind);
MODEL_API ChartKind   chartKindFromKey(const QString &key);
MODEL_API QString     chartAxisIdKey(ChartAxisId id);
MODEL_API ChartAxisId chartAxisIdFromKey(const QString &key);
MODEL_API QString     chartIdKey(ChartId id);
MODEL_API ChartId     chartIdFromKey(const QString &key);
MODEL_API QString     seriesKindKey(SeriesKind kind);
MODEL_API SeriesKind  seriesKindFromKey(const QString &key);
MODEL_API QString     filterKindKey(FilterKind kind);
MODEL_API FilterKind  filterKindFromKey(const QString &key);
MODEL_API QString     matrixAxisKey(MatrixAxisKey key);
MODEL_API QString     matrixAxisLabel(MatrixAxisKey key);
MODEL_API QString     displayText(DisplayText text);
MODEL_API QString     viewStateKey(ViewState state);
MODEL_API ViewState   viewStateFromKey(const QString &key);
MODEL_API QString     fieldName(Field field);

MODEL_API bool isAnomaly(Method method);
MODEL_API bool hasInstanceMetrics(Method method);
MODEL_API bool hasImageMetrics(Method method);
MODEL_API bool hasConfusionMatrix(Method method);
MODEL_API bool hasInstanceEvents(Method method);

/**
 * @brief 评估协议 key 映射的 QML 单例。
 *
 * 将 chartIdKey/matrixAxisKey 等字符串映射包装为 Q_INVOKABLE，供 QML 侧
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

} // namespace dltool::model::evaluation
