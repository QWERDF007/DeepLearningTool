#include "model/ModelEvaluationProtocol.h"

#include <QMetaEnum>

namespace dltool::model::evaluation {

namespace {

QString normalized(const QString &value)
{
    return value.trimmed().toLower();
}

}

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
    /**
     * @brief 保留用户配置组的完整内容，并规范化评估器实际使用的字段。
     *
     * 其余仅用于评估的参数仍保存在快照中，修改指标选择器等参数时可触发
     * C++ 重新评估而无需重新推理；推理参数在 ModelTaskPreparation 中比较。
     */
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

QString displayText(const DisplayText text)
{
    switch (text)
    {
    case DisplayText::Good: return QStringLiteral("正常");
    case DisplayText::Anomaly: return QStringLiteral("异常");
    case DisplayText::Total: return QStringLiteral("合计");
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

ViewState viewStateFromKey(const QString &key)
{
    const QString value = normalized(key);
    for (const ViewState state : {ViewState::NotRun, ViewState::Loading, ViewState::Running, ViewState::Failed,
                                  ViewState::MissingResult, ViewState::InvalidResult, ViewState::Error,
                                  ViewState::Ready})
    {
        if (value == normalized(viewStateKey(state)))
            return state;
    }
    return ViewState::NotRun;
}

QString fieldName(const Field field)
{
    static const QMetaEnum meta_enum = QMetaEnum::fromType<Field>();
    const char *name = meta_enum.valueToKey(static_cast<int>(field));
    if (name == nullptr)
        return {};

    const QString camel_case = QString::fromLatin1(name);
    QString       snake_case;
    snake_case.reserve(camel_case.size() + 8);
    for (int index = 0; index < camel_case.size(); ++index)
    {
        const QChar current = camel_case.at(index);
        const QChar previous = index > 0 ? camel_case.at(index - 1) : QChar();
        const QChar next = index + 1 < camel_case.size() ? camel_case.at(index + 1) : QChar();
        const bool word_boundary = index > 0 && current.isUpper()
                                    && (previous.isLower() || previous.isDigit()
                                        || (previous.isUpper() && next.isLower()));
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

}
