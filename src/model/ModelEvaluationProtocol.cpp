#include "model/ModelEvaluationProtocol.h"

#include <QMetaEnum>

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
    case core::DeepLearningMethod::AnomalyDetection:
        return method;
    case core::DeepLearningMethod::Pose:
    case core::DeepLearningMethod::OCR:
    case core::DeepLearningMethod::Unknown:
    default:
        return Method::Unknown;
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
    case Method::Classification:
        return QStringLiteral("classification");
    case Method::Detection:
        return QStringLiteral("object_detection");
    case Method::Segmentation:
        return QStringLiteral("segmentation");
    case Method::AnomalyDetection:
        return QStringLiteral("anomaly_detection");
    case Method::Unknown:
    case Method::Pose:
    case Method::OCR:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

QString statusKey(const Status status)
{
    switch (status)
    {
    case Status::TruePositive:
        return QStringLiteral("true_positive");
    case Status::TrueNegative:
        return QStringLiteral("true_negative");
    case Status::ClassMismatch:
        return QStringLiteral("class_mismatch");
    case Status::FalsePositive:
        return QStringLiteral("false_positive");
    case Status::FalseNegative:
        return QStringLiteral("false_negative");
    case Status::Ignored:
        return QStringLiteral("ignored");
    case Status::Unknown:
        return QStringLiteral("unknown");
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
    case Status::TruePositive:
        return QString("正确匹配");
    case Status::TrueNegative:
        return QString("正常");
    case Status::ClassMismatch:
        return QString("类别错误");
    case Status::FalsePositive:
        return QString("误检");
    case Status::FalseNegative:
        return QString("漏检");
    case Status::Ignored:
        return QString("已忽略");
    case Status::Unknown:
        return QString("未知");
    }
    return QString("未知");
}

QString matchingStrategyKey(const MatchingStrategy strategy)
{
    switch (strategy)
    {
    case MatchingStrategy::HungarianIoU:
        return QStringLiteral("hungarian_iou");
    case MatchingStrategy::GreedyIoU:
        return QStringLiteral("greedy_iou");
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
    // 保留用户配置组的完整内容，并规范化评估器实际使用的字段。
    // 配置文件中的评估组使用 conf/iou；评估引擎内部使用更明确的
    // confidence_threshold/iou_threshold。异常检测的同一位置由
    // classification_threshold 提供分类阈值。
    QVariantMap normalized = source;
    const double confidence_threshold
        = source.contains(QStringLiteral("classification_threshold"))
            ? source.value(QStringLiteral("classification_threshold")).toDouble()
            : source.value(QStringLiteral("conf"), kDefaultConfidenceThreshold).toDouble();
    normalized.insert(fieldName(Field::ConfidenceThreshold),
                      confidence_threshold);
    normalized.insert(fieldName(Field::IouThreshold),
                      source.value(QStringLiteral("iou"), kDefaultIouThreshold).toDouble());
    normalized.insert(
        fieldName(Field::MatchingStrategy),
        matchingStrategyKey(matchingStrategyFromKey(
            source.value(fieldName(Field::MatchingStrategy), matchingStrategyKey(MatchingStrategy::GreedyIoU))
                .toString())));
    return normalized;
}

QString metricSetKey(const MetricSet metric_set)
{
    switch (metric_set)
    {
    case MetricSet::Official:
        return QStringLiteral("official_metrics");
    case MetricSet::Diagnostic:
        return QStringLiteral("diagnostic_metrics");
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
    case CellKind::Match:
        return QStringLiteral("match");
    case CellKind::ClassMismatch:
        return QStringLiteral("class_mismatch");
    case CellKind::FalsePositive:
        return QStringLiteral("false_positive");
    case CellKind::FalseNegative:
        return QStringLiteral("false_negative");
    case CellKind::PredTotal:
        return QStringLiteral("pred_total");
    case CellKind::GtTotal:
        return QStringLiteral("gt_total");
    case CellKind::FalsePositiveTotal:
        return QStringLiteral("false_positive_total");
    case CellKind::FalseNegativeTotal:
        return QStringLiteral("false_negative_total");
    case CellKind::All:
        return QStringLiteral("all");
    case CellKind::NotApplicable:
        return QStringLiteral("not_applicable");
    }
    return QStringLiteral("not_applicable");
}

CellKind cellKindFromKey(const QString &key)
{
    const QString value = normalized(key);
    for (const CellKind kind :
         {CellKind::Match, CellKind::ClassMismatch, CellKind::FalsePositive, CellKind::FalseNegative,
          CellKind::PredTotal, CellKind::GtTotal, CellKind::FalsePositiveTotal, CellKind::FalseNegativeTotal,
          CellKind::All, CellKind::NotApplicable})
    {
        if (value == cellKindKey(kind))
            return kind;
    }
    return CellKind::NotApplicable;
}

QString chartKindKey(const ChartKind kind)
{
    switch (kind)
    {
    case ChartKind::Line:
        return QStringLiteral("line");
    case ChartKind::Bar:
        return QStringLiteral("bar");
    case ChartKind::Pie:
        return QStringLiteral("pie");
    case ChartKind::Unknown:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

ChartKind chartKindFromKey(const QString &key)
{
    const QString value = normalized(key);
    if (value == chartKindKey(ChartKind::Line))
        return ChartKind::Line;
    if (value == chartKindKey(ChartKind::Bar))
        return ChartKind::Bar;
    if (value == chartKindKey(ChartKind::Pie))
        return ChartKind::Pie;
    return ChartKind::Unknown;
}

QString chartAxisIdKey(const ChartAxisId id)
{
    switch (id)
    {
    case ChartAxisId::ScoreAxis:
        return QStringLiteral("score-axis");
    case ChartAxisId::CountAxis:
        return QStringLiteral("count-axis");
    case ChartAxisId::Unknown:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

ChartAxisId chartAxisIdFromKey(const QString &key)
{
    const QString value = normalized(key);
    if (value == chartAxisIdKey(ChartAxisId::ScoreAxis))
        return ChartAxisId::ScoreAxis;
    if (value == chartAxisIdKey(ChartAxisId::CountAxis))
        return ChartAxisId::CountAxis;
    return ChartAxisId::Unknown;
}

QString chartIdKey(const ChartId id)
{
    switch (id)
    {
    case ChartId::AnomalyScoreDistribution:
        return QStringLiteral("anomaly_score_distribution");
    case ChartId::PrecisionRecall:
        return QStringLiteral("precision_recall");
    case ChartId::ConfidenceDistribution:
        return QStringLiteral("confidence_distribution");
    case ChartId::PerClassMetrics:
        return QStringLiteral("per_class_metrics");
    case ChartId::Unknown:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

ChartId chartIdFromKey(const QString &key)
{
    const QString value = normalized(key);
    if (value == chartIdKey(ChartId::AnomalyScoreDistribution))
        return ChartId::AnomalyScoreDistribution;
    if (value == chartIdKey(ChartId::PrecisionRecall))
        return ChartId::PrecisionRecall;
    if (value == chartIdKey(ChartId::ConfidenceDistribution))
        return ChartId::ConfidenceDistribution;
    if (value == chartIdKey(ChartId::PerClassMetrics))
        return ChartId::PerClassMetrics;
    return ChartId::Unknown;
}

QString seriesKindKey(const SeriesKind kind)
{
    switch (kind)
    {
    case SeriesKind::Good:
        return QStringLiteral("good");
    case SeriesKind::Anomaly:
        return QStringLiteral("anomaly");
    case SeriesKind::Micro:
        return QStringLiteral("micro");
    case SeriesKind::Class:
        return QStringLiteral("class");
    case SeriesKind::Overall:
        return QStringLiteral("overall");
    case SeriesKind::BestThreshold:
        return QStringLiteral("best_threshold");
    case SeriesKind::Unknown:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

SeriesKind seriesKindFromKey(const QString &key)
{
    const QString value = normalized(key);
    if (value == seriesKindKey(SeriesKind::Good))
        return SeriesKind::Good;
    if (value == seriesKindKey(SeriesKind::Anomaly))
        return SeriesKind::Anomaly;
    if (value == seriesKindKey(SeriesKind::Micro))
        return SeriesKind::Micro;
    if (value == seriesKindKey(SeriesKind::Class))
        return SeriesKind::Class;
    if (value == seriesKindKey(SeriesKind::Overall))
        return SeriesKind::Overall;
    if (value == seriesKindKey(SeriesKind::BestThreshold))
        return SeriesKind::BestThreshold;
    return SeriesKind::Unknown;
}

QString filterKindKey(const FilterKind kind)
{
    switch (kind)
    {
    case FilterKind::ImageScore:
        return QStringLiteral("image_score");
    case FilterKind::PrecisionRecall:
        return QStringLiteral("precision_recall");
    case FilterKind::PerClassMetrics:
        return QStringLiteral("per_class_metrics");
    case FilterKind::Unknown:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

FilterKind filterKindFromKey(const QString &key)
{
    const QString value = normalized(key);
    if (value == filterKindKey(FilterKind::ImageScore))
        return FilterKind::ImageScore;
    if (value == filterKindKey(FilterKind::PrecisionRecall))
        return FilterKind::PrecisionRecall;
    if (value == filterKindKey(FilterKind::PerClassMetrics))
        return FilterKind::PerClassMetrics;
    return FilterKind::Unknown;
}

QString matrixAxisKey(const MatrixAxisKey key)
{
    switch (key)
    {
    case MatrixAxisKey::FalseNegative:
        return QStringLiteral("FN");
    case MatrixAxisKey::FalsePositive:
        return QStringLiteral("FP");
    case MatrixAxisKey::UnmatchedGroundTruth:
        return QStringLiteral("UNMATCHED_GT");
    case MatrixAxisKey::UnmatchedPrediction:
        return QStringLiteral("UNMATCHED_PRED");
    case MatrixAxisKey::Total:
        return QStringLiteral("TOTAL");
    }
    return {};
}

QString matrixAxisLabel(const MatrixAxisKey key)
{
    switch (key)
    {
    case MatrixAxisKey::FalseNegative:
        return QStringLiteral("FN");
    case MatrixAxisKey::FalsePositive:
        return QStringLiteral("FP");
    case MatrixAxisKey::UnmatchedGroundTruth:
        return QStringLiteral("漏检");
    case MatrixAxisKey::UnmatchedPrediction:
        return QStringLiteral("误检");
    case MatrixAxisKey::Total:
        return displayText(DisplayText::Total);
    }
    return {};
}

QString displayText(const DisplayText text)
{
    switch (text)
    {
    case DisplayText::Good:
        return QStringLiteral("正常");
    case DisplayText::Anomaly:
        return QStringLiteral("异常");
    case DisplayText::Total:
        return QStringLiteral("合计");
    }
    return {};
}

QString viewStateKey(const ViewState state)
{
    switch (state)
    {
    case ViewState::NotRun:
        return QStringLiteral("NotRun");
    case ViewState::Loading:
        return QStringLiteral("Loading");
    case ViewState::Running:
        return QStringLiteral("Running");
    case ViewState::Failed:
        return QStringLiteral("Failed");
    case ViewState::MissingResult:
        return QStringLiteral("MissingResult");
    case ViewState::InvalidResult:
        return QStringLiteral("InvalidResult");
    case ViewState::Error:
        return QStringLiteral("Error");
    case ViewState::Ready:
        return QStringLiteral("Ready");
    }
    return QStringLiteral("NotRun");
}

ViewState viewStateFromKey(const QString &key)
{
    const QString value = normalized(key);
    for (const ViewState state :
         {ViewState::NotRun, ViewState::Loading, ViewState::Running, ViewState::Failed, ViewState::MissingResult,
          ViewState::InvalidResult, ViewState::Error, ViewState::Ready})
    {
        if (value == normalized(viewStateKey(state)))
            return state;
    }
    return ViewState::NotRun;
}

QString fieldName(const Field field)
{
    static const QMetaEnum meta_enum = QMetaEnum::fromType<Field>();
    const char            *name      = meta_enum.valueToKey(static_cast<int>(field));
    if (name == nullptr)
        return {};

    const QString camel_case = QString::fromLatin1(name);
    QString       snake_case;
    snake_case.reserve(camel_case.size() + 8);
    for (int index = 0; index < camel_case.size(); ++index)
    {
        const QChar current      = camel_case.at(index);
        const QChar previous     = index > 0 ? camel_case.at(index - 1) : QChar();
        const QChar next         = index + 1 < camel_case.size() ? camel_case.at(index + 1) : QChar();
        const bool word_boundary = index > 0 && current.isUpper()
                                && (previous.isLower() || previous.isDigit() || (previous.isUpper() && next.isLower()));
        if (word_boundary)
            snake_case.push_back(QLatin1Char('_'));
        snake_case.push_back(current.toLower());
    }
    return snake_case;
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

EvaluationProtocolKeys::EvaluationProtocolKeys(QObject *parent)
    : QObject(parent)
{
}

QString EvaluationProtocolKeys::chartKindLine() const
{
    return evaluation::chartKindKey(ChartKind::Line);
}

QString EvaluationProtocolKeys::chartKindBar() const
{
    return evaluation::chartKindKey(ChartKind::Bar);
}

QString EvaluationProtocolKeys::chartKindPie() const
{
    return evaluation::chartKindKey(ChartKind::Pie);
}

QString EvaluationProtocolKeys::chartAxisScore() const
{
    return evaluation::chartAxisIdKey(ChartAxisId::ScoreAxis);
}

QString EvaluationProtocolKeys::chartAxisCount() const
{
    return evaluation::chartAxisIdKey(ChartAxisId::CountAxis);
}

QString EvaluationProtocolKeys::chartIdAnomalyScoreDistribution() const
{
    return evaluation::chartIdKey(ChartId::AnomalyScoreDistribution);
}

QString EvaluationProtocolKeys::chartIdPrecisionRecall() const
{
    return evaluation::chartIdKey(ChartId::PrecisionRecall);
}

QString EvaluationProtocolKeys::chartIdConfidenceDistribution() const
{
    return evaluation::chartIdKey(ChartId::ConfidenceDistribution);
}

QString EvaluationProtocolKeys::chartIdPerClassMetrics() const
{
    return evaluation::chartIdKey(ChartId::PerClassMetrics);
}

QString EvaluationProtocolKeys::seriesKindGood() const
{
    return evaluation::seriesKindKey(SeriesKind::Good);
}

QString EvaluationProtocolKeys::seriesKindAnomaly() const
{
    return evaluation::seriesKindKey(SeriesKind::Anomaly);
}

QString EvaluationProtocolKeys::seriesKindMicro() const
{
    return evaluation::seriesKindKey(SeriesKind::Micro);
}

QString EvaluationProtocolKeys::seriesKindClass() const
{
    return evaluation::seriesKindKey(SeriesKind::Class);
}

QString EvaluationProtocolKeys::seriesKindOverall() const
{
    return evaluation::seriesKindKey(SeriesKind::Overall);
}

QString EvaluationProtocolKeys::seriesKindBestThreshold() const
{
    return evaluation::seriesKindKey(SeriesKind::BestThreshold);
}

QString EvaluationProtocolKeys::filterKindImageScore() const
{
    return evaluation::filterKindKey(FilterKind::ImageScore);
}

QString EvaluationProtocolKeys::filterKindPrecisionRecall() const
{
    return evaluation::filterKindKey(FilterKind::PrecisionRecall);
}

QString EvaluationProtocolKeys::filterKindPerClassMetrics() const
{
    return evaluation::filterKindKey(FilterKind::PerClassMetrics);
}

QString EvaluationProtocolKeys::matrixAxisFalseNegative() const
{
    return evaluation::matrixAxisKey(MatrixAxisKey::FalseNegative);
}

QString EvaluationProtocolKeys::matrixAxisFalsePositive() const
{
    return evaluation::matrixAxisKey(MatrixAxisKey::FalsePositive);
}

QString EvaluationProtocolKeys::matrixAxisUnmatchedGroundTruth() const
{
    return evaluation::matrixAxisKey(MatrixAxisKey::UnmatchedGroundTruth);
}

QString EvaluationProtocolKeys::matrixAxisUnmatchedPrediction() const
{
    return evaluation::matrixAxisKey(MatrixAxisKey::UnmatchedPrediction);
}

QString EvaluationProtocolKeys::matrixAxisTotal() const
{
    return evaluation::matrixAxisKey(MatrixAxisKey::Total);
}

QString EvaluationProtocolKeys::chartKindKey(const int kind) const
{
    return evaluation::chartKindKey(static_cast<ChartKind>(kind));
}

QString EvaluationProtocolKeys::chartAxisIdKey(const int id) const
{
    return evaluation::chartAxisIdKey(static_cast<ChartAxisId>(id));
}

QString EvaluationProtocolKeys::chartIdKey(const int id) const
{
    return evaluation::chartIdKey(static_cast<ChartId>(id));
}

QString EvaluationProtocolKeys::seriesKindKey(const int kind) const
{
    return evaluation::seriesKindKey(static_cast<SeriesKind>(kind));
}

QString EvaluationProtocolKeys::filterKindKey(const int kind) const
{
    return evaluation::filterKindKey(static_cast<FilterKind>(kind));
}

QString EvaluationProtocolKeys::matrixAxisKey(const int key) const
{
    return evaluation::matrixAxisKey(static_cast<MatrixAxisKey>(key));
}

QString EvaluationProtocolKeys::statusKey(const int status) const
{
    return evaluation::statusKey(static_cast<Status>(status));
}

QString EvaluationProtocolKeys::fieldName(const int field) const
{
    return evaluation::fieldName(static_cast<Field>(field));
}

EvaluationProtocolKeys *EvaluationProtocolKeys::create(QQmlEngine *, QJSEngine *)
{
    static EvaluationProtocolKeys instance;
    return &instance;
}

} // namespace dltool::model::evaluation
