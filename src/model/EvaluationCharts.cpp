#include "model/EvaluationCharts.h"

#include "model/EvaluationCommon.h"
#include "model/EvaluationGeometry.h"
#include "model/EvaluationMatching.h"
#include "model/ModelEvaluationProtocol.h"

#include <QVariantList>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace dltool::model {

namespace {

/**
 * @brief 判断协作取消令牌是否已被置位。
 * @param cancel 协作取消令牌，可为空。
 * @return 已置位返回 true。
 */
bool isCancelled(const std::shared_ptr<std::atomic_bool> &cancel)
{
    return cancel != nullptr && cancel->load(std::memory_order_relaxed);
}

/**
 * @brief 安全比率：分母为 0 时返回 0。
 * @param numerator 分子。
 * @param denominator 分母。
 * @return 比率值。
 */
double safeRatio(const qint64 numerator, const qint64 denominator)
{
    return denominator > 0 ? static_cast<double>(numerator) / static_cast<double>(denominator) : 0.0;
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
        const double score = image.max_prediction_score;
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

QVariantMap evaluationMetricMap(const qint64 tp, const qint64 fp, const qint64 fn)
{
    const double precision = safeRatio(tp, tp + fp);
    const double recall    = safeRatio(tp, tp + fn);
    const double f1        = precision + recall > 0.0 ? 2.0 * precision * recall / (precision + recall) : 0.0;
    return {
        {       evaluation::fieldName(evaluation::Field::Precision),                                              precision},
        {          evaluation::fieldName(evaluation::Field::Recall),                                                 recall},
        {              evaluation::fieldName(evaluation::Field::F1),                                                     f1},
        {evaluation::fieldName(evaluation::Field::PrecisionDefined),                                            tp + fp > 0},
        {   evaluation::fieldName(evaluation::Field::RecallDefined),                                            tp + fn > 0},
        {       evaluation::fieldName(evaluation::Field::F1Defined), tp + fp > 0 && tp + fn > 0 && precision + recall > 0.0},
        {              evaluation::fieldName(evaluation::Field::Tp),                                                     tp},
        {              evaluation::fieldName(evaluation::Field::Fp),                                                     fp},
        {              evaluation::fieldName(evaluation::Field::Fn),                                                     fn}
    };
}

QVariantMap perClassMetricsChart(const QVariantList &labels, const QVariantList &precision, const QVariantList &recall,
                                 const QVariantList &f1)
{
    return QVariantMap{
        {      evaluation::fieldName(evaluation::Field::Kind),QStringLiteral("bar")                                                              },
        {   evaluation::fieldName(evaluation::Field::ChartId), QStringLiteral("per_class_metrics")},
        {evaluation::fieldName(evaluation::Field::FilterKind), QStringLiteral("per_class_metrics")},
        {     evaluation::fieldName(evaluation::Field::Title),               QString("按类别指标")},
        {      evaluation::fieldName(evaluation::Field::Data),
         QVariantMap{
         {evaluation::fieldName(evaluation::Field::Labels), labels},
         {evaluation::fieldName(evaluation::Field::Datasets),
         QVariantList{QVariantMap{{evaluation::fieldName(evaluation::Field::Label), QStringLiteral("Precision")},
         {evaluation::fieldName(evaluation::Field::Data), precision}},
         QVariantMap{{evaluation::fieldName(evaluation::Field::Label), QStringLiteral("Recall")},
         {evaluation::fieldName(evaluation::Field::Data), recall}},
         QVariantMap{{evaluation::fieldName(evaluation::Field::Label), QStringLiteral("F1")},
         {evaluation::fieldName(evaluation::Field::Data), f1}}}}}                                 },
        {   evaluation::fieldName(evaluation::Field::Options),
         QVariantMap{{QStringLiteral("maintainAspectRatio"), false}}                              }
    };
}

EvaluationChartOutput buildEvaluationCharts(const evaluation::Method                 method,
                                            const QMap<qint64, EvaluationImageData> &images, const double confidence,
                                            const double iou_threshold, const evaluation::MatchingStrategy strategy,
                                            const QVariantMap                       &diagnostic,
                                            const std::shared_ptr<std::atomic_bool> &cancel)
{
    EvaluationChartOutput output;
    const bool            detection = evaluation::hasInstanceMetrics(method);
    const bool            anomaly   = evaluation::isAnomaly(method);
    if (!detection && !anomaly)
        return output;

    if (anomaly)
    {
        // 异常检测为图像级二元分类：GOOD 是隐式负类（正常样本没有 GT 标签），
        // 指标定义为 score-above-threshold。
        output.available = true;
        output.metrics   = QVariantMap{
            { evaluation::fieldName(evaluation::Field::Available),true                                                                  },
            {     evaluation::fieldName(evaluation::Field::Image),
             diagnostic.value(evaluation::fieldName(evaluation::Field::Image))                              },
            {evaluation::fieldName(evaluation::Field::Definition), QStringLiteral("anomaly_score_threshold")}
        };
        output.image_definition = QVariantMap{
            {        evaluation::fieldName(evaluation::Field::SampleUnit),                 QStringLiteral("image")},
            {       evaluation::fieldName(evaluation::Field::Aggregation),                 QStringLiteral("micro")},
            {evaluation::fieldName(evaluation::Field::PositiveDefinition), QStringLiteral("score_above_threshold")},
            {   evaluation::fieldName(evaluation::Field::HasImageMetrics),                                    true}
        };
        return output;
    }

    struct Counts
    {
        qint64 tp{0};
        qint64 fp{0};
        qint64 fn{0};
    };

    const auto countsAt = [&](const double threshold)
    {
        Counts counts;
        for (const EvaluationImageData &image : images)
        {
            if (isCancelled(cancel))
                return counts;
            QList<EvaluationPredictionData> predictions;
            for (const EvaluationPredictionData &prediction : image.predictions)
                if (prediction.score >= threshold)
                    predictions.push_back(prediction);
            const QList<MatchPair> pairs = matchPredictions(predictions, image.gt, iou_threshold, strategy, cancel);
            QVector<bool>          used_prediction(predictions.size(), false);
            QVector<bool>          used_gt(image.gt.size(), false);
            for (const MatchPair &pair : pairs)
            {
                if (pair.prediction < 0 || pair.ground_truth < 0 || used_prediction.at(pair.prediction)
                    || used_gt.at(pair.ground_truth))
                    continue;
                used_prediction[pair.prediction] = true;
                used_gt[pair.ground_truth]       = true;
                if (predictions.at(pair.prediction).class_id == image.gt.at(pair.ground_truth).class_id)
                    ++counts.tp;
                else
                {
                    ++counts.fp;
                    ++counts.fn;
                }
            }
            for (int index = 0; index < predictions.size(); ++index)
                if (!used_prediction.at(index))
                    ++counts.fp;
            for (int index = 0; index < image.gt.size(); ++index)
                if (!used_gt.at(index))
                    ++counts.fn;
        }
        return counts;
    };

    // PR 曲线阈值集：1.0、所有预测分数、工作点置信度。
    QList<double> thresholds;
    thresholds.push_back(1.0);
    for (const EvaluationImageData &image : images)
    {
        if (isCancelled(cancel))
            return {};
        for (const EvaluationPredictionData &prediction : image.predictions)
            if (std::isfinite(prediction.score))
                thresholds.push_back(std::clamp(prediction.score, 0.0, 1.0));
    }
    thresholds.push_back(confidence);
    std::sort(thresholds.begin(), thresholds.end(), std::greater<double>());
    QList<double> unique_thresholds;
    for (const double value : thresholds)
        if (unique_thresholds.isEmpty() || !qFuzzyCompare(unique_thresholds.back() + 1.0, value + 1.0))
            unique_thresholds.push_back(value);

    QVariantList recall_values;
    QVariantList precision_values;
    QVariantList threshold_labels;
    for (const double threshold : unique_thresholds)
    {
        if (isCancelled(cancel))
            return {};
        const Counts      counts = countsAt(threshold);
        const QVariantMap metric = evaluationMetricMap(counts.tp, counts.fp, counts.fn);
        threshold_labels.push_back(threshold);
        precision_values.push_back(metric.value(evaluation::fieldName(evaluation::Field::Precision)));
        recall_values.push_back(metric.value(evaluation::fieldName(evaluation::Field::Recall)));
    }
    const Counts work_point = countsAt(confidence);
    output.available        = true;
    output.metrics          = QVariantMap{
        {evaluation::fieldName(evaluation::Field::Available), true},
        {evaluation::fieldName(evaluation::Field::Instance),
         evaluationMetricMap(work_point.tp, work_point.fp, work_point.fn)},
        {evaluation::fieldName(evaluation::Field::PerClass),
         diagnostic.value(evaluation::fieldName(evaluation::Field::Instance))
             .toMap()
             .value(evaluation::fieldName(evaluation::Field::PerClass))},
        {evaluation::fieldName(evaluation::Field::Definition), QStringLiteral("confidence_iou_work_point")}
    };
    output.image_definition = QVariantMap{
        {        evaluation::fieldName(evaluation::Field::SampleUnit),     QStringLiteral("image_class_presence")},
        {       evaluation::fieldName(evaluation::Field::Aggregation),                    QStringLiteral("micro")},
        {evaluation::fieldName(evaluation::Field::PositiveDefinition), QStringLiteral("gt_or_pred_class_present")},
        {   evaluation::fieldName(evaluation::Field::HasImageMetrics),                                       true}
    };
    output.charts.push_back(QVariantMap{
        {      evaluation::fieldName(evaluation::Field::Kind),QStringLiteral("line")                                                              },
        {   evaluation::fieldName(evaluation::Field::ChartId),          QStringLiteral("precision_recall")},
        {evaluation::fieldName(evaluation::Field::FilterKind),          QStringLiteral("precision_recall")},
        {     evaluation::fieldName(evaluation::Field::Title),                          QString("PR 曲线")},
        {      evaluation::fieldName(evaluation::Field::Data),
         QVariantMap{
         {evaluation::fieldName(evaluation::Field::Labels), threshold_labels},
         {evaluation::fieldName(evaluation::Field::Datasets),
         QVariantList{QVariantMap{{evaluation::fieldName(evaluation::Field::Label), QStringLiteral("Precision")},
         {evaluation::fieldName(evaluation::Field::Data), precision_values}},
         QVariantMap{{evaluation::fieldName(evaluation::Field::Label), QStringLiteral("Recall")},
         {evaluation::fieldName(evaluation::Field::Data), recall_values}}}}}                              },
        {   evaluation::fieldName(evaluation::Field::Options),
         QVariantMap{{QStringLiteral("maintainAspectRatio"), false}}                                      }
    });
    output.chart_kinds.push_back(QStringLiteral("line"));
    return output;
}

QVariantMap buildInstanceEvent(const EvaluationImageData &image, const evaluation::Status status,
                               const EvaluationGroundTruthData *gt, const EvaluationPredictionData *pred,
                               const double iou, const QString &dataset_root, const QString &prediction_root,
                               const qint64 event_index)
{
    // 裁剪视口：GT/预测并集扩展 5% 边距；整幅图像无有效边界时退回全图。
    const QVariantMap crop
        = cropBounds(gt ? gt->bounds : QVariantMap{}, pred ? pred->bounds : QVariantMap{}, image.width, image.height);
    const QVariantMap viewport      = crop.isEmpty() && image.width > 0 && image.height > 0
                                        ? evaluationBoxMap(EvaluationBox{0.0, 0.0, static_cast<double>(image.width),
                                                                         static_cast<double>(image.height)})
                                        : crop;
    const QVariantMap gt_geometry   = gt ? gt->geometry : QVariantMap{};
    const QVariantMap pred_geometry = pred ? pred->geometry : QVariantMap{};
    return QVariantMap{
        {evaluation::fieldName(evaluation::Field::EventUuid), QStringLiteral("%1-%2").arg(image.id).arg(event_index)},
        {evaluation::fieldName(evaluation::Field::ImageId), image.id},
        {evaluation::fieldName(evaluation::Field::Status), evaluation::statusKey(status)},
        {evaluation::fieldName(evaluation::Field::Score), pred ? pred->score : 0.0},
        {evaluation::fieldName(evaluation::Field::Iou), iou},
        {evaluation::fieldName(evaluation::Field::GtLabelId), gt ? gt->label_id : -1},
        {evaluation::fieldName(evaluation::Field::GtClassId), gt ? gt->class_id : -1},
        {evaluation::fieldName(evaluation::Field::GtClassName), gt ? gt->class_name : QString()},
        {evaluation::fieldName(evaluation::Field::GtGeometry), gt_geometry},
        {evaluation::fieldName(evaluation::Field::PredInstanceId), pred ? pred->prediction_id : QString()},
        {evaluation::fieldName(evaluation::Field::PredClassId), pred ? pred->class_id : -1},
        {evaluation::fieldName(evaluation::Field::PredClassName), pred ? pred->class_name : QString()},
        {evaluation::fieldName(evaluation::Field::PredGeometry), pred_geometry},
        {evaluation::fieldName(evaluation::Field::CropBounds), viewport},
        {evaluation::fieldName(evaluation::Field::GtOverlayBounds),
         normalizedOverlayBounds(gt ? gt->bounds : QVariantMap{}, viewport)},
        {evaluation::fieldName(evaluation::Field::PredOverlayBounds),
         normalizedOverlayBounds(pred ? pred->bounds : QVariantMap{}, viewport)},
        {evaluation::fieldName(evaluation::Field::GtOverlayPoints), normalizedOverlayPoints(gt_geometry, viewport)},
        {evaluation::fieldName(evaluation::Field::PredOverlayPoints), normalizedOverlayPoints(pred_geometry, viewport)},
        {evaluation::fieldName(evaluation::Field::GtMaskUrl), maskUrl(gt_geometry, dataset_root)},
        {evaluation::fieldName(evaluation::Field::PredMaskUrl), maskUrl(pred_geometry, prediction_root)}
    };
}

QVariantList evaluationConfusionCells(const QMap<int, QString> &classes, const QMap<QString, qint64> &matrix,
                                      const qint64 total_count, const bool anomaly_method)
{
    const QString     matrix_fn    = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::FalseNegative);
    const QString     matrix_fp    = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::FalsePositive);
    const QString     matrix_total = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::Total);
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

    QVariantList cells;
    const auto   appendCell = [&](const QString &row, const QString &column, qint64 count,
                                  const evaluation::CellKind kind, bool selectable, bool diagonal, bool error)
    {
        const bool    row_fn       = row == matrix_fn;
        const bool    row_total    = row == matrix_total;
        const bool    column_fp    = column == matrix_fp;
        const bool    column_total = column == matrix_total;
        const int     row_id       = row_fn || row_total ? -1 : row.toInt();
        const int     column_id    = column_fp || column_total ? -1 : column.toInt();
        const QString total_label  = QString("合计");
        const QString row_label    = row_fn ? matrix_fn : (row_total ? total_label : classes.value(row_id));
        const QString column_label = column_fp ? matrix_fp : (column_total ? total_label : classes.value(column_id));
        cells.push_back(QVariantMap{
            {       evaluation::fieldName(evaluation::Field::RowKey),                           row},
            {    evaluation::fieldName(evaluation::Field::ColumnKey),                        column},
            {     evaluation::fieldName(evaluation::Field::RowLabel),                     row_label},
            {  evaluation::fieldName(evaluation::Field::ColumnLabel),                  column_label},
            {   evaluation::fieldName(evaluation::Field::RowClassId),                        row_id},
            {evaluation::fieldName(evaluation::Field::ColumnClassId),                     column_id},
            {        evaluation::fieldName(evaluation::Field::Count),                         count},
            {     evaluation::fieldName(evaluation::Field::CellKind), evaluation::cellKindKey(kind)},
            {   evaluation::fieldName(evaluation::Field::Selectable),                    selectable},
            {   evaluation::fieldName(evaluation::Field::IsDiagonal),                      diagonal},
            {      evaluation::fieldName(evaluation::Field::IsError),                         error}
        });
    };
    for (auto row_it = classes.cbegin(); row_it != classes.cend(); ++row_it)
    {
        const QString row = QString::number(row_it.key());
        for (auto column_it = classes.cbegin(); column_it != classes.cend(); ++column_it)
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
    for (auto column_it = classes.cbegin(); column_it != classes.cend(); ++column_it)
    {
        const QString column = QString::number(column_it.key());
        appendCell(matrix_fn, column, matrix.value(matrix_fn + QLatin1Char('\x1f') + column),
                   evaluation::CellKind::FalseNegative, true, false, true);
    }
    appendCell(matrix_fn, matrix_fp, 0, evaluation::CellKind::NotApplicable, false, false, false);
    appendCell(matrix_fn, matrix_total, unmatched_fn, evaluation::CellKind::FalseNegativeTotal, true, false, true);
    for (auto column_it = classes.cbegin(); column_it != classes.cend(); ++column_it)
    {
        const QString column = QString::number(column_it.key());
        appendCell(matrix_total, column, gt_totals.value(column_it.key()), evaluation::CellKind::GtTotal, true, false,
                   false);
    }
    appendCell(matrix_total, matrix_fp, unmatched_fp, evaluation::CellKind::FalsePositiveTotal, true, false, true);
    appendCell(matrix_total, matrix_total, total_count, evaluation::CellKind::All, true, false, false);
    (void)anomaly_method;
    return cells;
}

QVariantMap assembleEvaluationResult(const QMap<qint64, EvaluationImageData> &images, const QMap<int, QString> &classes,
                                     const QMap<int, EvaluationCounts> &per_class, const EvaluationCounts &overall,
                                     const EvaluationCounts &image_counts, const QMap<QString, qint64> &matrix,
                                     const QVariantList &event_records, const int prediction_count,
                                     const evaluation::Method method, const double confidence_threshold,
                                     const double iou_threshold, const evaluation::MatchingStrategy matching_strategy,
                                     const QVariantMap                       &evaluation_config,
                                     const std::shared_ptr<std::atomic_bool> &cancel, QString *err_msg)
{
    const auto cancelled = [cancel, err_msg]()
    {
        if (err_msg)
            *err_msg = QString("评估已取消");
        return isCancelled(cancel);
    };
    const bool anomaly_method = evaluation::isAnomaly(method);

    // 图像记录序列化：GT/预测实例列表。
    QVariantList image_records;
    for (const EvaluationImageData &image : images)
    {
        if (cancelled())
            return {};
        QVariantList gt_instances;
        for (const EvaluationGroundTruthData &gt : image.gt)
        {
            gt_instances.push_back(QVariantMap{
                {  evaluation::fieldName(evaluation::Field::LabelId),   gt.label_id},
                {  evaluation::fieldName(evaluation::Field::ClassId),   gt.class_id},
                {evaluation::fieldName(evaluation::Field::ClassName), gt.class_name},
                { evaluation::fieldName(evaluation::Field::Geometry),   gt.geometry}
            });
        }
        QVariantList prediction_instances;
        for (const EvaluationPredictionData &prediction : image.predictions)
        {
            prediction_instances.push_back(QVariantMap{
                {evaluation::fieldName(evaluation::Field::PredictionId), prediction.prediction_id},
                {     evaluation::fieldName(evaluation::Field::ClassId),      prediction.class_id},
                {   evaluation::fieldName(evaluation::Field::ClassName),    prediction.class_name},
                {       evaluation::fieldName(evaluation::Field::Score),         prediction.score},
                {    evaluation::fieldName(evaluation::Field::Geometry),      prediction.geometry}
            });
        }
        image_records.push_back(QVariantMap{
            {    evaluation::fieldName(evaluation::Field::ImageId),             image.id},
            {  evaluation::fieldName(evaluation::Field::DatasetId),     image.dataset_id},
            {  evaluation::fieldName(evaluation::Field::ImageName),           image.name},
            {  evaluation::fieldName(evaluation::Field::ImagePath),           image.path},
            { evaluation::fieldName(evaluation::Field::ImageWidth),          image.width},
            {evaluation::fieldName(evaluation::Field::ImageHeight),         image.height},
            {evaluation::fieldName(evaluation::Field::GtInstances),         gt_instances},
            {evaluation::fieldName(evaluation::Field::Predictions), prediction_instances}
        });
    }

    // 按类别指标与图表数据。
    QVariantList per_class_metrics;
    QVariantList chart_labels;
    QVariantList precision_values;
    QVariantList recall_values;
    QVariantList f1_values;
    for (auto it = classes.cbegin(); it != classes.cend(); ++it)
    {
        if (cancelled())
            return {};
        const EvaluationCounts counts = per_class.value(it.key());
        QVariantMap            metric = evaluationMetricMap(counts.tp, counts.fp, counts.fn);
        metric.insert(evaluation::fieldName(evaluation::Field::ClassId), it.key());
        metric.insert(evaluation::fieldName(evaluation::Field::ClassName), it.value());
        per_class_metrics.push_back(metric);
        chart_labels.push_back(it.value());
        precision_values.push_back(metric.value(evaluation::fieldName(evaluation::Field::Precision)));
        recall_values.push_back(metric.value(evaluation::fieldName(evaluation::Field::Recall)));
        f1_values.push_back(metric.value(evaluation::fieldName(evaluation::Field::F1)));
    }

    // 类别目录。
    QVariantList class_catalog;
    for (auto it = classes.cbegin(); it != classes.cend(); ++it)
    {
        if (cancelled())
            return {};
        class_catalog.push_back(QVariantMap{
            {   evaluation::fieldName(evaluation::Field::Id),             it.key()},
            { evaluation::fieldName(evaluation::Field::Name),           it.value()},
            {evaluation::fieldName(evaluation::Field::Color), classColor(it.key())}
        });
    }

    // 异常检测的矩阵重建：每幅图像一个二元结果，使正常图像的 GOOD/GOOD
    // 真负计入矩阵（它们没有 GT 实例事件）。
    QMap<QString, qint64> resolved_matrix = matrix;
    if (anomaly_method)
    {
        resolved_matrix.clear();
        for (const EvaluationImageData &image : images)
        {
            const bool ground_truth_anomaly
                = std::any_of(image.gt.cbegin(), image.gt.cend(),
                              [](const EvaluationGroundTruthData &ground_truth) { return ground_truth.class_id == 1; });
            const bool predicted_anomaly
                = std::any_of(image.predictions.cbegin(), image.predictions.cend(),
                              [confidence_threshold](const EvaluationPredictionData &prediction)
                              { return prediction.class_id == 1 && prediction.score >= confidence_threshold; });
            const QString row    = predicted_anomaly ? QStringLiteral("1") : QStringLiteral("0");
            const QString column = ground_truth_anomaly ? QStringLiteral("1") : QStringLiteral("0");
            ++resolved_matrix[row + QLatin1Char('\x1f') + column];
        }
    }
    const QVariantList matrix_cells = evaluationConfusionCells(
        classes, resolved_matrix, anomaly_method ? images.size() : event_records.size(), anomaly_method);

    const QVariantMap diagnostic = {
        {evaluation::fieldName(evaluation::Field::Instance),
         QVariantMap{{evaluation::fieldName(evaluation::Field::Overall),
                      evaluationMetricMap(overall.tp, overall.fp, overall.fn)},
                     {evaluation::fieldName(evaluation::Field::PerClass), per_class_metrics}}},
        {evaluation::fieldName(evaluation::Field::Image),
         evaluationMetricMap(image_counts.tp, image_counts.fp, image_counts.fn)}
    };
    const EvaluationChartOutput official = buildEvaluationCharts(method, images, confidence_threshold, iou_threshold,
                                                                 matching_strategy, diagnostic, cancel);
    if (cancelled())
        return {};
    const EvaluationCapabilities capabilities = evaluationCapabilitiesForMethod(method);
    QVariantList                 charts;
    if (capabilities.has_instance_metrics)
        charts.push_back(perClassMetricsChart(chart_labels, precision_values, recall_values, f1_values));
    for (const QVariant &chart : official.charts) charts.push_back(chart);

    return QVariantMap{
        {     evaluation::fieldName(evaluation::Field::PrimaryMetricSet),
         official.available ? evaluation::metricSetKey(evaluation::MetricSet::Official)
         : evaluation::metricSetKey(evaluation::MetricSet::Diagnostic)                                 },
        {     evaluation::fieldName(evaluation::Field::EvaluationConfig),
         evaluation::normalizedEvaluationConfig(evaluation_config)                                     },
        {         evaluation::fieldName(evaluation::Field::ClassCatalog),                 class_catalog},
        {    evaluation::fieldName(evaluation::Field::DiagnosticMetrics),                    diagnostic},
        {      evaluation::fieldName(evaluation::Field::OfficialMetrics),
         official.available ? official.metrics
         : QVariantMap{{evaluation::fieldName(evaluation::Field::Available), false}}                   },
        {evaluation::fieldName(evaluation::Field::ImageMetricDefinition),
         official.available && !official.image_definition.isEmpty()
         ? official.image_definition
         : QVariantMap{{evaluation::fieldName(evaluation::Field::SampleUnit), QStringLiteral("image_presence")},
         {evaluation::fieldName(evaluation::Field::Aggregation), QStringLiteral("micro")},
         {evaluation::fieldName(evaluation::Field::PositiveDefinition),
         QStringLiteral("gt_or_pred_class_present")},
         {evaluation::fieldName(evaluation::Field::HasImageMetrics), true}}                            },
        {         evaluation::fieldName(evaluation::Field::Capabilities),
         QVariantMap{{evaluation::fieldName(evaluation::Field::HasInstanceMetrics), capabilities.has_instance_metrics},
         {evaluation::fieldName(evaluation::Field::HasImageMetrics), capabilities.has_image_metrics},
         {evaluation::fieldName(evaluation::Field::HasConfusionMatrix), capabilities.has_confusion_matrix},
         {evaluation::fieldName(evaluation::Field::HasInstanceEvents), capabilities.has_instance_events},
         {evaluation::fieldName(evaluation::Field::ChartKinds), capabilities.chart_kinds}}             },
        {      evaluation::fieldName(evaluation::Field::ConfusionMatrix),
         QVariantMap{{evaluation::fieldName(evaluation::Field::Cells), matrix_cells}}                  },
        {               evaluation::fieldName(evaluation::Field::Charts),                        charts},
        {         evaluation::fieldName(evaluation::Field::ImageRecords),                 image_records},
        {      evaluation::fieldName(evaluation::Field::InstanceRecords),                 event_records},
        {           evaluation::fieldName(evaluation::Field::ImageCount),                 images.size()},
        {      evaluation::fieldName(evaluation::Field::PredictionCount),              prediction_count},
        {           evaluation::fieldName(evaluation::Field::EventCount),          event_records.size()},
    };
}

} // namespace dltool::model
