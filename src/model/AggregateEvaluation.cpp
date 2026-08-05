#include "model/AggregateEvaluation.h"

#include "model/EvaluationGeometry.h"
#include "model/EvaluationMatching.h"
#include "model/ModelEvaluationProtocol.h"

#include <QSet>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <limits>

namespace dltool::model {

namespace {

/**
 * @brief 聚合统计计数（真正例/假正例/假负例）。
 */
struct AggregateCounts
{
    qint64 tp{0};
    qint64 fp{0};
    qint64 fn{0};
};

/**
 * @brief 计算两条 QVariantMap 几何记录的 IoU。
 *
 * 与 Service 的匹配规则一致：两框均无效视为完全匹配（IoU=1）；单边无效
 * 返回 0。解析与交并比计算复用 EvaluationGeometry 公共实现。
 * @param lhs 左侧几何记录。
 * @param rhs 右侧几何记录。
 * @return IoU 值。
 */
double aggregateIou(const QVariantMap &lhs, const QVariantMap &rhs)
{
    EvaluationBox a;
    EvaluationBox b;
    const bool    has_a = readBox(lhs, a);
    const bool    has_b = readBox(rhs, b);
    if (!has_a && !has_b)
        return 1.0;
    if (!has_a || !has_b)
        return 0.0;
    return intersectionOverUnion(a, b);
}

/**
 * @brief 聚合匹配对（预测/真值下标与 IoU）。
 */
struct AggregateMatch
{
    int    prediction{-1};   ///< 预测下标。
    int    ground_truth{-1}; ///< 真值下标。
    double iou{0.0};         ///< 匹配 IoU。
};

/**
 * @brief 聚合匹配：委托公共匹配模块，注入 QVariantMap 几何 IoU。
 * @param predictions 预测记录列表。
 * @param ground_truth 真值记录列表。
 * @param threshold IoU 阈值。
 * @param strategy 匹配策略。
 * @return 匹配对列表（按预测下标升序）。
 */
QList<AggregateMatch> aggregateMatches(const QList<EvaluationPredictionRecord>  &predictions,
                                       const QList<EvaluationGroundTruthRecord> &ground_truth, const double threshold,
                                       const evaluation::MatchingStrategy strategy)
{
    // 几何 IoU 注入：ViewModel 使用 QVariantMap 几何记录，Service 使用 Box；
    // 匹配算法（贪心/Hungarian）由 EvaluationMatching 公共模块提供。
    const auto iou_fn = [&predictions, &ground_truth](const int prediction, const int gt)
    {
        return aggregateIou(predictions.at(prediction).geometry, ground_truth.at(gt).geometry);
    };
    const QList<MatchPair> matches = strategy == evaluation::MatchingStrategy::HungarianIoU
                                       ? hungarianIoUMatches(predictions.size(), ground_truth.size(), iou_fn, threshold)
                                       : greedyIoUMatches(predictions.size(), ground_truth.size(), iou_fn, threshold);

    QList<AggregateMatch> result;
    result.reserve(matches.size());
    for (const MatchPair &pair : matches) result.push_back({pair.prediction, pair.ground_truth, pair.iou});
    return result;
}

/**
 * @brief 判断图像记录是否为异常（按预测或 GT 类别包含类别 1）。
 * @param record 图像记录。
 * @param threshold 置信度阈值（仅预测侧使用）。
 * @param predicted 按预测判断时为 true，否则按 GT 判断。
 * @return 是异常返回 true。
 */
bool isAnomalyImage(const EvaluationImageRecord &record, const double threshold, const bool predicted)
{
    const QList<int> classes = predicted ? predClassIds(record, threshold) : gtClassIds(record);
    return classes.contains(1);
}

/**
 * @brief 由聚合计数构造指标记录。
 * @param key 指标键。
 * @param label 显示名称。
 * @param class_id 类别 ID。
 * @param counts 聚合计数。
 * @return 指标记录。
 */
EvaluationMetricRecord aggregateMetric(const QString &key, const QString &label, const int class_id,
                                       const AggregateCounts &counts)
{
    EvaluationMetricRecord value;
    value.key               = key;
    value.label             = label;
    value.class_name        = label;
    value.class_id          = class_id;
    value.tp                = counts.tp;
    value.fp                = counts.fp;
    value.fn                = counts.fn;
    value.precision_defined = counts.tp + counts.fp > 0;
    value.recall_defined    = counts.tp + counts.fn > 0;
    value.precision         = value.precision_defined ? static_cast<double>(counts.tp) / (counts.tp + counts.fp) : 0.0;
    value.recall            = value.recall_defined ? static_cast<double>(counts.tp) / (counts.tp + counts.fn) : 0.0;
    value.f1_defined        = value.precision_defined && value.recall_defined && value.precision + value.recall > 0.0;
    value.f1 = value.f1_defined ? 2.0 * value.precision * value.recall / (value.precision + value.recall) : 0.0;
    return value;
}

/**
 * @brief 判断类别 ID 是否允许参与聚合。
 * @param class_ids 类别过滤列表（空表示全部允许）。
 * @param class_id 类别 ID。
 * @return 允许返回 true。
 */
bool aggregateClassAllowed(const QVariantList &class_ids, const int class_id)
{
    if (class_ids.isEmpty() || class_id < 0)
        return true;
    for (const QVariant &value : class_ids)
        if (value.toInt() == class_id)
            return true;
    return false;
}

/**
 * @brief 统计给定置信度阈值下的聚合 TP/FP/FN。
 * @param images 图像记录列表。
 * @param class_ids 类别过滤列表。
 * @param threshold 置信度阈值。
 * @param iou_threshold IoU 阈值。
 * @param matching_strategy 匹配策略。
 * @return 聚合计数。
 */
AggregateCounts aggregateThresholdCounts(const QList<EvaluationImageRecord> &images, const QVariantList &class_ids,
                                         const double threshold, const double iou_threshold,
                                         const evaluation::MatchingStrategy matching_strategy)
{
    AggregateCounts counts;
    for (const EvaluationImageRecord &image : images)
    {
        QList<EvaluationPredictionRecord>  predictions;
        QList<EvaluationGroundTruthRecord> ground_truth;
        for (const EvaluationPredictionRecord &prediction : image.predictions)
        {
            if (prediction.score >= threshold && aggregateClassAllowed(class_ids, prediction.class_id))
                predictions.push_back(prediction);
        }
        for (const EvaluationGroundTruthRecord &gt : image.gt_instances)
        {
            if (aggregateClassAllowed(class_ids, gt.class_id))
                ground_truth.push_back(gt);
        }
        std::sort(predictions.begin(), predictions.end(),
                  [](const auto &lhs, const auto &rhs) { return lhs.score > rhs.score; });
        const QList<AggregateMatch> matches
            = aggregateMatches(predictions, ground_truth, iou_threshold, matching_strategy);
        QVector<bool> used_prediction(predictions.size(), false);
        QVector<bool> used_gt(ground_truth.size(), false);
        for (const AggregateMatch &candidate : matches)
        {
            if (used_prediction.at(candidate.prediction) || used_gt.at(candidate.ground_truth))
                continue;
            used_prediction[candidate.prediction] = true;
            used_gt[candidate.ground_truth]       = true;
            if (predictions.at(candidate.prediction).class_id == ground_truth.at(candidate.ground_truth).class_id)
                ++counts.tp;
            else
            {
                ++counts.fp;
                ++counts.fn;
            }
        }
        for (int index = 0; index < used_prediction.size(); ++index)
            if (!used_prediction.at(index))
                ++counts.fp;
        for (int index = 0; index < used_gt.size(); ++index)
            if (!used_gt.at(index))
                ++counts.fn;
    }
    return counts;
}

/**
 * @brief 分数直方图数据（GOOD/Anomaly 分箱点列）。
 */
struct ScoreHistogramData
{
    QVariantList        labels;         ///< 箱中心标签。
    QVariantList        good_points;    ///< GOOD 分箱点列。
    QVariantList        anomaly_points; ///< Anomaly 分箱点列。
    std::vector<double> centers;        ///< 箱中心值。
    int                 max_count{0};   ///< 最大箱计数。
    double              min_score{0.0}; ///< 分数最小值。
    double              max_score{1.0}; ///< 分数最大值。
};

/**
 * @brief 按 24 箱构造分数直方图。
 * @param good_scores GOOD 样本分数列表（空值忽略）。
 * @param anomaly_scores Anomaly 样本分数列表。
 * @return 直方图数据。
 */
ScoreHistogramData scoreHistogram(const QVariantList &good_scores, const QVariantList &anomaly_scores)
{
    ScoreHistogramData  histogram;
    std::vector<double> good_values;
    std::vector<double> anomaly_values;
    const auto          collect = [](const QVariantList &source, std::vector<double> &target)
    {
        for (const QVariant &value : source)
        {
            bool         ok    = false;
            const double score = value.toDouble(&ok);
            if (ok && std::isfinite(score))
                target.push_back(score);
        }
    };
    collect(good_scores, good_values);
    collect(anomaly_scores, anomaly_values);

    std::vector<double> all_values;
    all_values.reserve(good_values.size() + anomaly_values.size());
    all_values.insert(all_values.end(), good_values.cbegin(), good_values.cend());
    all_values.insert(all_values.end(), anomaly_values.cbegin(), anomaly_values.cend());
    if (all_values.empty())
        return histogram;

    const auto minmax              = std::minmax_element(all_values.cbegin(), all_values.cend());
    histogram.min_score            = *minmax.first;
    histogram.max_score            = *minmax.second;
    constexpr int bin_count        = 24;
    int           actual_bin_count = bin_count;
    if (std::abs(histogram.max_score - histogram.min_score) <= 1e-12)
    {
        const double padding = std::max(0.01, std::abs(histogram.min_score) * 0.05);
        histogram.min_score -= padding;
        histogram.max_score += padding;
        actual_bin_count = 1;
    }
    const double     bin_width = (histogram.max_score - histogram.min_score) / actual_bin_count;
    std::vector<int> good_counts(static_cast<std::size_t>(actual_bin_count), 0);
    std::vector<int> anomaly_counts(static_cast<std::size_t>(actual_bin_count), 0);
    histogram.centers.reserve(actual_bin_count);
    histogram.labels.reserve(actual_bin_count);
    for (int index = 0; index < actual_bin_count; ++index)
    {
        const double center = histogram.min_score + (static_cast<double>(index) + 0.5) * bin_width;
        histogram.centers.push_back(center);
        histogram.labels.push_back(QString::number(center, 'f', 4));
    }

    const auto addToBins = [&](const std::vector<double> &values, std::vector<int> &counts)
    {
        for (const double score : values)
        {
            const int index = std::clamp(static_cast<int>(std::floor((score - histogram.min_score) / bin_width)), 0,
                                         actual_bin_count - 1);
            ++counts.at(static_cast<std::size_t>(index));
        }
    };
    addToBins(good_values, good_counts);
    addToBins(anomaly_values, anomaly_counts);

    const auto makePoints = [&histogram](const std::vector<int> &counts)
    {
        QVariantList points;
        points.reserve(static_cast<int>(counts.size()));
        for (std::size_t index = 0; index < counts.size(); ++index)
        {
            // 零计数箱是断点而不是 x 轴上的点：用无效 QVariant 在 QML/JSON
            // 边界变成 null，使 Chart.js 在空区间断开曲线而不是画零基线。
            const QVariant y = counts.at(index) > 0 ? QVariant(counts.at(index)) : QVariant();
            points.push_back(QVariantMap{
                {QStringLiteral("x"), histogram.centers.at(index)},
                {QStringLiteral("y"),                           y}
            });
        }
        return points;
    };
    histogram.good_points    = makePoints(good_counts);
    histogram.anomaly_points = makePoints(anomaly_counts);
    histogram.max_count      = std::max(*std::max_element(good_counts.cbegin(), good_counts.cend()),
                                        *std::max_element(anomaly_counts.cbegin(), anomaly_counts.cend()));
    return histogram;
}

/**
 * @brief 构造异常分数分布图（直方图 + 参考线）。
 * @param good_scores GOOD 样本分数列表。
 * @param anomaly_scores Anomaly 样本分数列表。
 * @param has_good 是否存在 GOOD 样本。
 * @param good_max GOOD 最大分数。
 * @param has_anomaly 是否存在 Anomaly 样本。
 * @param anomaly_min Anomaly 最小分数。
 * @return 图表描述符。
 */
QVariantMap anomalyScoreChart(const QVariantList &good_scores, const QVariantList &anomaly_scores, const bool has_good,
                              const double good_max, const bool has_anomaly, const double anomaly_min)
{
    constexpr const char *good_color         = "#43A047";
    constexpr const char *good_fill          = "rgba(67, 160, 71, 0.24)";
    constexpr const char *anomaly_color      = "#E53935";
    constexpr const char *anomaly_fill       = "rgba(229, 57, 53, 0.24)";
    ScoreHistogramData    histogram          = scoreHistogram(good_scores, anomaly_scores);
    const auto            alignBoundaryPoint = [](QVariantList &points, const double score, const bool last)
    {
        if (!std::isfinite(score))
            return;

        const int start = last ? points.size() - 1 : 0;
        const int end   = last ? -1 : points.size();
        const int step  = last ? -1 : 1;
        for (int index = start; index != end; index += step)
        {
            QVariantMap  point       = points.at(index).toMap();
            bool         valid_count = false;
            const double count       = point.value(QStringLiteral("y")).toDouble(&valid_count);
            if (!valid_count || count <= 0.0)
                continue;

            // 直方图点通常使用箱中心。与参考标记关联的边界点移动到原始样本
            // 极值，使曲线与标记共享同一个可见 x 坐标。
            point.insert(QStringLiteral("x"), score);
            points[index] = point;
            return;
        }
    };
    if (has_good)
        alignBoundaryPoint(histogram.good_points, good_max, true);
    if (has_anomaly)
        alignBoundaryPoint(histogram.anomaly_points, anomaly_min, false);

    const auto isolatedPointRadii = [](const QVariantList &points)
    {
        const auto hasValue = [](const QVariant &value)
        {
            bool         ok    = false;
            const double count = value.toDouble(&ok);
            return ok && std::isfinite(count) && count > 0.0;
        };

        QVariantList radii;
        radii.reserve(points.size());
        for (int index = 0; index < points.size(); ++index)
        {
            const QVariantMap point    = points.at(index).toMap();
            const bool        current  = hasValue(point.value(QStringLiteral("y")));
            const bool        previous = index > 0 && hasValue(points.at(index - 1).toMap().value(QStringLiteral("y")));
            const bool        next
                = index + 1 < points.size() && hasValue(points.at(index + 1).toMap().value(QStringLiteral("y")));
            radii.push_back(current && !previous && !next ? 3 : 0);
        }
        return radii;
    };

    const auto distributionDataset = [&isolatedPointRadii](const QString &label, const QString &line_color,
                                                           const QString &fill_color, const QVariantList &points)
    {
        return QVariantMap{
            {               QStringLiteral("label"),                        label},
            {                QStringLiteral("data"),                       points},
            {     QStringLiteral("backgroundColor"),                   fill_color},
            {         QStringLiteral("borderColor"),                   line_color},
            {QStringLiteral("pointBackgroundColor"),                   line_color},
            {    QStringLiteral("pointBorderColor"),                   line_color},
            {         QStringLiteral("pointRadius"),   isolatedPointRadii(points)},
            {    QStringLiteral("pointHoverRadius"),                            4},
            {         QStringLiteral("borderWidth"),                            2},
            {         QStringLiteral("lineTension"),                            0},
            {            QStringLiteral("spanGaps"),                        false},
            {             QStringLiteral("xAxisID"), QStringLiteral("score-axis")},
            {             QStringLiteral("yAxisID"), QStringLiteral("count-axis")},
            {            QStringLiteral("showLine"),                         true},
            {                QStringLiteral("fill"),                         true}
        };
    };
    const auto referenceDataset
        = [](const QString &label, const QString &color, const double value, const int max_count)
    {
        return QVariantMap{
            {           QStringLiteral("label"), label                                                },
            {            QStringLiteral("data"),
             QVariantList{QVariantMap{{QStringLiteral("x"), value}, {QStringLiteral("y"), 0}},
             QVariantMap{{QStringLiteral("x"), value}, {QStringLiteral("y"), max_count}}}             },
            { QStringLiteral("backgroundColor"),                                                 color},
            {     QStringLiteral("borderColor"),                                                 color},
            {     QStringLiteral("borderWidth"),                                                     2},
            {      QStringLiteral("borderDash"),                                    QVariantList{6, 4}},
            {     QStringLiteral("pointRadius"),                                                     0},
            {QStringLiteral("pointHoverRadius"),                                                     0},
            {     QStringLiteral("lineTension"),                                                     0},
            {        QStringLiteral("spanGaps"),                                                 false},
            {         QStringLiteral("xAxisID"),                          QStringLiteral("score-axis")},
            {         QStringLiteral("yAxisID"),                          QStringLiteral("count-axis")},
            {        QStringLiteral("showLine"),                                                  true},
            {            QStringLiteral("fill"),                                                 false}
        };
    };

    QVariantList datasets;
    if (has_good)
        datasets.push_back(distributionDataset(QStringLiteral("GOOD"), QString::fromLatin1(good_color),
                                               QString::fromLatin1(good_fill), histogram.good_points));
    if (has_anomaly)
        datasets.push_back(distributionDataset(QStringLiteral("Anomaly"), QString::fromLatin1(anomaly_color),
                                               QString::fromLatin1(anomaly_fill), histogram.anomaly_points));
    if (has_good)
        datasets.push_back(referenceDataset(QString("GOOD 最大分数：%1").arg(QString::number(good_max, 'f', 4)),
                                            QString::fromLatin1(good_color), good_max, histogram.max_count));
    if (has_anomaly)
        datasets.push_back(referenceDataset(QString("Anomaly 最小分数：%1").arg(QString::number(anomaly_min, 'f', 4)),
                                            QString::fromLatin1(anomaly_color), anomaly_min, histogram.max_count));

    const double      suggested_count = histogram.max_count > 0 ? histogram.max_count * 1.1 : 1.0;
    const QVariantMap options{
        {QStringLiteral("maintainAspectRatio"),                false                                               },
        {         QStringLiteral("responsive"),                                                                true},
        {             QStringLiteral("legend"),
         QVariantMap{{QStringLiteral("display"), true}, {QStringLiteral("position"), QStringLiteral("top")}}       },
        {           QStringLiteral("tooltips"),
         QVariantMap{{QStringLiteral("mode"), QStringLiteral("nearest")}, {QStringLiteral("intersect"), false}}    },
        {             QStringLiteral("scales"),
         QVariantMap{
         {QStringLiteral("xAxes"),
         QVariantList{QVariantMap{
         {QStringLiteral("id"), QStringLiteral("score-axis")},
         {QStringLiteral("type"), QStringLiteral("linear")},
         {QStringLiteral("display"), true},
         {QStringLiteral("ticks"), QVariantMap{{QStringLiteral("min"), histogram.min_score},
         {QStringLiteral("max"), histogram.max_score},
         {QStringLiteral("maxTicksLimit"), 12},
         {QStringLiteral("maxRotation"), 0},
         {QStringLiteral("minRotation"), 0}}},
         {QStringLiteral("scaleLabel"),
         QVariantMap{{QStringLiteral("display"), true}, {QStringLiteral("labelString"), QString("分数")}}}}}},
         {QStringLiteral("yAxes"),
         QVariantList{QVariantMap{
         {QStringLiteral("id"), QStringLiteral("count-axis")},
         {QStringLiteral("type"), QStringLiteral("linear")},
         {QStringLiteral("display"), true},
         {QStringLiteral("ticks"), QVariantMap{{QStringLiteral("beginAtZero"), true},
         {QStringLiteral("suggestedMax"), suggested_count}}},
         {QStringLiteral("scaleLabel"), QVariantMap{{QStringLiteral("display"), true},
         {QStringLiteral("labelString"), QString("数量")}}}}}}}                                                    }
    };

    return QVariantMap{
        {      evaluation::fieldName(evaluation::Field::Kind),QStringLiteral("line")                                                              },
        {   evaluation::fieldName(evaluation::Field::ChartId), QStringLiteral("anomaly_score_distribution")},
        {evaluation::fieldName(evaluation::Field::FilterKind),                QStringLiteral("image_score")},
        {     evaluation::fieldName(evaluation::Field::Title), QString("异常分数分布（图像级 pred_score）")},
        {      evaluation::fieldName(evaluation::Field::Data),
         QVariantMap{{evaluation::fieldName(evaluation::Field::Labels), histogram.labels},
         {evaluation::fieldName(evaluation::Field::Datasets), datasets}}                                   },
        {   evaluation::fieldName(evaluation::Field::Options),                                      options}
    };
}

} // namespace

const QList<int> &gtClassIds(const EvaluationImageRecord &record)
{
    return record.gt_class_ids;
}

QList<int> predClassIds(const EvaluationImageRecord &record, const double threshold)
{
    QList<int> ids;
    for (const EvaluationPredictionRecord &prediction : record.predictions)
        if (prediction.class_id >= 0 && prediction.score >= threshold && !ids.contains(prediction.class_id))
            ids.push_back(prediction.class_id);
    return ids;
}

bool hasPredictions(const EvaluationImageRecord &record, const double threshold)
{
    return std::any_of(record.predictions.cbegin(), record.predictions.cend(),
                       [threshold](const EvaluationPredictionRecord &prediction)
                       { return prediction.score >= threshold; });
}

bool hasGroundTruth(const EvaluationImageRecord &record)
{
    return record.has_gt;
}

double imageScore(const EvaluationImageRecord &record)
{
    return record.max_prediction_score;
}

QVariantMap anomalyScoreChartForImages(const QList<EvaluationImageRecord> &images)
{
    QVariantList good_scores;
    QVariantList anomaly_scores;
    bool         has_good    = false;
    bool         has_anomaly = false;
    double       good_max    = 0.0;
    double       anomaly_min = std::numeric_limits<double>::max();
    for (const EvaluationImageRecord &image : images)
    {
        const double score = imageScore(image);
        if (image.gt_class_ids.contains(1))
        {
            good_scores.push_back(QVariant());
            anomaly_scores.push_back(score);
            has_anomaly = true;
            anomaly_min = std::min(anomaly_min, score);
        }
        else
        {
            good_scores.push_back(score);
            anomaly_scores.push_back(QVariant());
            has_good = true;
            good_max = std::max(good_max, score);
        }
    }
    return anomalyScoreChart(good_scores, anomaly_scores, has_good, good_max, has_anomaly, anomaly_min);
}

EvaluationAggregateOutput aggregateEvaluation(const EvaluationAggregateInput &input)
{
    EvaluationAggregateOutput  output;
    const QString              matrix_fn    = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::FalseNegative);
    const QString              matrix_fp    = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::FalsePositive);
    const QString              matrix_total = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::Total);
    QMap<int, AggregateCounts> classes;
    QMap<int, QString>         class_names = input.class_catalog;
    QMap<QString, qint64>      matrix;
    AggregateCounts            overall;
    for (const EvaluationAggregateInput::InstanceEvent &record : input.instances)
    {
        if (record.gt_class_id >= 0)
            class_names.insert(record.gt_class_id,
                               record.gt_class.isEmpty() ? QString::number(record.gt_class_id) : record.gt_class);
        if (record.pred_class_id >= 0)
            class_names.insert(record.pred_class_id,
                               record.pred_class.isEmpty() ? QString::number(record.pred_class_id) : record.pred_class);
        if (record.status == evaluation::Status::TruePositive)
        {
            ++overall.tp;
            ++classes[record.pred_class_id].tp;
            ++matrix[QString::number(record.pred_class_id) + QLatin1Char('\x1f') + QString::number(record.gt_class_id)];
        }
        else if (record.status == evaluation::Status::ClassMismatch)
        {
            ++overall.fp;
            ++overall.fn;
            ++classes[record.pred_class_id].fp;
            ++classes[record.gt_class_id].fn;
            ++matrix[QString::number(record.pred_class_id) + QLatin1Char('\x1f') + QString::number(record.gt_class_id)];
        }
        else if (record.status == evaluation::Status::FalsePositive)
        {
            ++overall.fp;
            ++classes[record.pred_class_id].fp;
            ++matrix[QString::number(record.pred_class_id) + QLatin1Char('\x1f') + matrix_fp];
        }
        else if (record.status == evaluation::Status::FalseNegative)
        {
            ++overall.fn;
            ++classes[record.gt_class_id].fn;
            ++matrix[matrix_fn + QLatin1Char('\x1f') + QString::number(record.gt_class_id)];
        }
    }

    // 异常项目按图像级评估。GOOD 图像在项目数据库/任务选择中没有 GT 标签，
    // 上面的实例事件矩阵无法表达真负；这里显式构造二元图像矩阵，行列布局
    // 与检测方法保持一致。
    if (input.anomaly_detection)
    {
        class_names.clear();
        class_names.insert(0, QStringLiteral("GOOD"));
        class_names.insert(1, QStringLiteral("Anomaly"));
        matrix.clear();
        for (const EvaluationImageRecord &image : input.images)
        {
            const bool    ground_truth_anomaly = isAnomalyImage(image, input.confidence_threshold, false);
            const bool    predicted_anomaly    = isAnomalyImage(image, input.confidence_threshold, true);
            const QString row                  = predicted_anomaly ? QStringLiteral("1") : QStringLiteral("0");
            const QString column               = ground_truth_anomaly ? QStringLiteral("1") : QStringLiteral("0");
            ++matrix[row + QLatin1Char('\x1f') + column];
        }
    }

    AggregateCounts image_counts;
    for (const EvaluationImageRecord &image : input.images)
    {
        QSet<int> gt_classes;
        QSet<int> pred_classes;
        for (const int class_id : gtClassIds(image))
            if (aggregateClassAllowed(input.class_ids, class_id))
                gt_classes.insert(class_id);
        for (const int class_id : predClassIds(image, input.confidence_threshold))
            if (aggregateClassAllowed(input.class_ids, class_id))
                pred_classes.insert(class_id);
        for (const int class_id : pred_classes)
        {
            if (gt_classes.contains(class_id))
                ++image_counts.tp;
            else
                ++image_counts.fp;
        }
        for (const int class_id : gt_classes)
            if (!pred_classes.contains(class_id))
                ++image_counts.fn;
        if (gt_classes.isEmpty() && pred_classes.isEmpty())
        {
            const bool has_gt   = hasGroundTruth(image) && input.class_ids.isEmpty();
            const bool has_pred = hasPredictions(image, input.confidence_threshold) && input.class_ids.isEmpty();
            if (has_gt && has_pred)
                ++image_counts.tp;
            else if (has_pred)
                ++image_counts.fp;
            else if (has_gt)
                ++image_counts.fn;
        }
    }

    output.instance_metrics.push_back(aggregateMetric(QStringLiteral("overall"), QString("整体"), -1, overall));
    for (auto it = class_names.cbegin(); it != class_names.cend(); ++it)
    {
        output.per_class_metrics.push_back(
            aggregateMetric(QString::number(it.key()), it.value(), it.key(), classes.value(it.key())));
    }
    output.image_metrics.push_back(aggregateMetric(QStringLiteral("image"), QString("图像"), -1, image_counts));

    // 混淆矩阵单元格：类别 x 类别 + FN/FP/合计行列。
    std::vector<EvaluationConfusionCell> cells;
    const auto appendCell = [&](const QString &row, const QString &column, qint64 count,
                                const evaluation::CellKind kind, bool selectable, bool diagonal, bool error)
    {
        const bool    row_fn       = row == matrix_fn;
        const bool    row_total    = row == matrix_total;
        const bool    column_fp    = column == matrix_fp;
        const bool    column_total = column == matrix_total;
        const int     row_id       = row_fn || row_total ? -1 : row.toInt();
        const int     column_id    = column_fp || column_total ? -1 : column.toInt();
        const QString total_label  = QString("合计");
        const QString row_label    = row_fn ? matrix_fn : (row_total ? total_label : class_names.value(row_id));
        const QString column_label
            = column_fp ? matrix_fp : (column_total ? total_label : class_names.value(column_id));
        EvaluationConfusionCell cell;
        cell.row_key         = row;
        cell.column_key      = column;
        cell.row_label       = row_label;
        cell.column_label    = column_label;
        cell.row_class_id    = row_id;
        cell.column_class_id = column_id;
        cell.count           = count;
        cell.cell_kind       = kind;
        cell.selectable      = selectable;
        cell.diagonal        = diagonal;
        cell.error           = error;
        cells.push_back(cell);
    };
    QMap<int, qint64> pred_totals;
    QMap<int, qint64> gt_totals;
    qint64            unmatched_fp = 0;
    qint64            unmatched_fn = 0;
    for (auto it = matrix.cbegin(); it != matrix.cend(); ++it)
    {
        const QList<QString> keys = it.key().split(QLatin1Char('\x1f'));
        if (keys.size() != 2)
            continue;
        if (keys.at(0) == matrix_fn)
            unmatched_fn += it.value();
        else
            pred_totals[keys.at(0).toInt()] += it.value();
        if (keys.at(1) == matrix_fp)
            unmatched_fp += it.value();
        else
            gt_totals[keys.at(1).toInt()] += it.value();
    }
    for (auto row_it = class_names.cbegin(); row_it != class_names.cend(); ++row_it)
    {
        const QString row = QString::number(row_it.key());
        for (auto column_it = class_names.cbegin(); column_it != class_names.cend(); ++column_it)
        {
            const QString column   = QString::number(column_it.key());
            const bool    diagonal = row_it.key() == column_it.key();
            appendCell(row, column, matrix.value(row + QLatin1Char('\x1f') + column),
                       diagonal ? evaluation::CellKind::Match : evaluation::CellKind::ClassMismatch, true, diagonal,
                       !diagonal);
        }
        appendCell(row, matrix_fp, matrix.value(row + QLatin1Char('\x1f') + matrix_fp),
                   evaluation::CellKind::FalsePositive, true, false, true);
        appendCell(row, matrix_total, pred_totals.value(row_it.key()), evaluation::CellKind::PredTotal, true, false,
                   false);
    }
    for (auto column_it = class_names.cbegin(); column_it != class_names.cend(); ++column_it)
    {
        const QString column = QString::number(column_it.key());
        appendCell(matrix_fn, column, matrix.value(matrix_fn + QLatin1Char('\x1f') + column),
                   evaluation::CellKind::FalseNegative, true, false, true);
    }
    appendCell(matrix_fn, matrix_fp, 0, evaluation::CellKind::NotApplicable, false, false, false);
    appendCell(matrix_fn, matrix_total, unmatched_fn, evaluation::CellKind::FalseNegativeTotal, true, false, true);
    for (auto column_it = class_names.cbegin(); column_it != class_names.cend(); ++column_it)
    {
        const QString column = QString::number(column_it.key());
        appendCell(matrix_total, column, gt_totals.value(column_it.key()), evaluation::CellKind::GtTotal, true, false,
                   false);
    }
    appendCell(matrix_total, matrix_fp, unmatched_fp, evaluation::CellKind::FalsePositiveTotal, true, false, true);
    appendCell(matrix_total, matrix_total, input.anomaly_detection ? input.images.size() : input.instances.size(),
               evaluation::CellKind::All, true, false, false);
    output.confusion = std::move(cells);

    // 派生图表：按类别指标柱状图（聚合后重算）与异常分数分布图。
    if (input.has_instance_metrics)
    {
        QVariantList labels;
        QVariantList precision;
        QVariantList recall;
        QVariantList f1;
        for (const EvaluationMetricRecord &record : output.per_class_metrics)
        {
            labels.push_back(record.class_name);
            precision.push_back(record.precision);
            recall.push_back(record.recall);
            f1.push_back(record.f1);
        }
        output.charts.push_back(QVariantMap{
            {      evaluation::fieldName(evaluation::Field::Kind),QStringLiteral("bar")                                                                  },
            {   evaluation::fieldName(evaluation::Field::ChartId), QStringLiteral("per_class_metrics")},
            {evaluation::fieldName(evaluation::Field::FilterKind), QStringLiteral("per_class_metrics")},
            {     evaluation::fieldName(evaluation::Field::Title),               QString("按类别指标")},
            {      evaluation::fieldName(evaluation::Field::Data),
             QVariantMap{
             {evaluation::fieldName(evaluation::Field::Labels), labels},
             {evaluation::fieldName(evaluation::Field::Datasets),
             QVariantList{
             QVariantMap{{evaluation::fieldName(evaluation::Field::Label), QStringLiteral("Precision")},
             {evaluation::fieldName(evaluation::Field::Data), precision}},
             QVariantMap{{evaluation::fieldName(evaluation::Field::Label), QStringLiteral("Recall")},
             {evaluation::fieldName(evaluation::Field::Data), recall}},
             QVariantMap{{evaluation::fieldName(evaluation::Field::Label), QStringLiteral("F1")},
             {evaluation::fieldName(evaluation::Field::Data), f1}}}}}                                 },
            {   evaluation::fieldName(evaluation::Field::Options),
             QVariantMap{{QStringLiteral("maintainAspectRatio"), false}}                              }
        });
    }

    if (input.anomaly_detection)
    {
        QList<EvaluationImageRecord> images;
        images.reserve(input.images.size());
        for (const EvaluationImageRecord &image : input.images) images.push_back(image);
        output.charts.push_back(anomalyScoreChartForImages(images));
    }

    // 沿用 Service 图表描述符：PR 曲线按聚合输入重算，过滤掉已被本地派生
    // 图表替换的异常分布与按类别指标图。
    for (const QVariantMap &descriptor : input.chart_descriptors)
    {
        const QString chart_id    = descriptor.value(evaluation::fieldName(evaluation::Field::ChartId)).toString();
        const QString filter_kind = descriptor.value(evaluation::fieldName(evaluation::Field::FilterKind)).toString();
        if (chart_id == QStringLiteral("anomaly_score_distribution"))
            continue;
        if (descriptor.value(evaluation::fieldName(evaluation::Field::Kind)).toString() == QStringLiteral("bar")
            && (filter_kind == QStringLiteral("per_class_metrics") || chart_id == QStringLiteral("per_class_metrics")))
            continue;
        QVariantMap filtered = descriptor;
        if (filter_kind == QStringLiteral("precision_recall") || chart_id == QStringLiteral("precision_recall"))
        {
            QList<double> thresholds{1.0, input.confidence_threshold};
            for (const EvaluationImageRecord &image : input.images)
                for (const EvaluationPredictionRecord &prediction : image.predictions)
                    if (std::isfinite(prediction.score) && aggregateClassAllowed(input.class_ids, prediction.class_id))
                        thresholds.push_back(std::clamp(prediction.score, 0.0, 1.0));
            std::sort(thresholds.begin(), thresholds.end(), std::greater<double>());
            QList<double> unique;
            for (const double value : thresholds)
                if (unique.isEmpty() || !qFuzzyCompare(unique.back() + 1.0, value + 1.0))
                    unique.push_back(value);
            QVariantList labels;
            QVariantList precision;
            QVariantList recall;
            for (const double threshold : unique)
            {
                const AggregateCounts counts = aggregateThresholdCounts(input.images, input.class_ids, threshold,
                                                                        input.iou_threshold, input.matching_strategy);
                labels.push_back(threshold);
                precision.push_back(counts.tp + counts.fp > 0 ? static_cast<double>(counts.tp) / (counts.tp + counts.fp)
                                                              : 0.0);
                recall.push_back(counts.tp + counts.fn > 0 ? static_cast<double>(counts.tp) / (counts.tp + counts.fn)
                                                           : 0.0);
            }
            filtered.insert(
                evaluation::fieldName(evaluation::Field::Data),
                QVariantMap{
                    {  evaluation::fieldName(evaluation::Field::Labels),labels                                                        },
                    {evaluation::fieldName(evaluation::Field::Datasets),
                     QVariantList{
                     QVariantMap{{evaluation::fieldName(evaluation::Field::Label), QStringLiteral("Precision")},
                     {evaluation::fieldName(evaluation::Field::Data), precision}},
                     QVariantMap{{evaluation::fieldName(evaluation::Field::Label), QStringLiteral("Recall")},
                     {evaluation::fieldName(evaluation::Field::Data), recall}}}}
            });
        }
        else if (filter_kind == QStringLiteral("image_score"))
        {
            QVariantList labels;
            QVariantList scores;
            for (const EvaluationImageRecord &image : input.images)
            {
                labels.push_back(image.image_name);
                scores.push_back(imageScore(image));
            }
            filtered.insert(evaluation::fieldName(evaluation::Field::Data),
                            QVariantMap{
                                {  evaluation::fieldName(evaluation::Field::Labels),labels                                                                                },
                                {evaluation::fieldName(evaluation::Field::Datasets),
                                 QVariantList{QVariantMap{
                                 {evaluation::fieldName(evaluation::Field::Label), QStringLiteral("score")},
                                 {evaluation::fieldName(evaluation::Field::Data), scores}}}}
            });
        }
        output.charts.push_back(std::move(filtered));
    }
    return output;
}

} // namespace dltool::model
