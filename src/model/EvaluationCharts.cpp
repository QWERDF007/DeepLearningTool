#include "model/EvaluationCharts.h"

#include "model/EvaluationCommon.h"
#include "model/EvaluationDataset.h"
#include "model/EvaluationGeometry.h"
#include "model/EvaluationMatching.h"
#include "model/ModelEvaluationProtocol.h"

#include <spdlog/spdlog.h>

#include <QElapsedTimer>
#include <QSet>
#include <QVariantList>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

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
 * @brief 分数直方图数据（正常/异常分箱点列）。
 */
struct ScoreHistogramData
{
    QVariantList        labels;         ///< 箱中心标签。
    QVariantList        good_points;    ///< 正常分箱点列。
    QVariantList        anomaly_points; ///< 异常分箱点列。
    std::vector<double> centers;        ///< 箱中心值。
    int                 max_count{0};   ///< 最大箱计数。
    double              min_score{0.0}; ///< 分数最小值。
    double              max_score{1.0}; ///< 分数最大值。
};

/**
 * @brief 按 24 箱构造分数直方图。
 * @param good_scores 正常样本分数列表（空值忽略）。
 * @param anomaly_scores 异常样本分数列表。
 * @param classification_threshold 分类阈值；纳入横轴范围但不参与分箱计数。
 * @param best_threshold 最佳阈值；纳入横轴范围但不参与分箱计数。
 * @return 直方图数据。
 */
ScoreHistogramData scoreHistogram(const QVariantList &good_scores, const QVariantList &anomaly_scores,
                                  const double classification_threshold, const double best_threshold)
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
    if (std::isfinite(classification_threshold))
        all_values.push_back(classification_threshold);
    if (std::isfinite(best_threshold))
        all_values.push_back(best_threshold);
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
 * @param good_scores 正常样本分数列表。
 * @param anomaly_scores 异常样本分数列表。
 * @param has_good 是否存在正常样本。
 * @param good_max 正常样本最大分数。
 * @param has_anomaly 是否存在异常样本。
 * @param anomaly_min 异常样本最小分数。
 * @param classification_threshold 图像分数分类阈值。
 * @return 图表描述符。
 */
QVariantMap anomalyScoreChart(const QVariantList &good_scores, const QVariantList &anomaly_scores, const bool has_good,
                              const double good_max, const bool has_anomaly, const double anomaly_min,
                              const double classification_threshold,
                              const EvaluationThresholdSearchResult *threshold_search)
{
    const double best_threshold = threshold_search != nullptr && threshold_search->available
                                    ? threshold_search->best_point.threshold
                                    : std::numeric_limits<double>::quiet_NaN();
    ScoreHistogramData    histogram
        = scoreHistogram(good_scores, anomaly_scores, classification_threshold, best_threshold);
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

    const auto distributionDataset
        = [&isolatedPointRadii](const QString &label, const QString &series_kind, const QString &line_color,
                                const QString &fill_color, const QVariantList &points)
    {
        return QVariantMap{
            {                             QStringLiteral("label"),                                                          label},
            {evaluation::fieldName(evaluation::Field::SeriesKind),                                                    series_kind},
            {                              QStringLiteral("data"),                                                         points},
            {                   QStringLiteral("backgroundColor"),                                                     fill_color},
            {                       QStringLiteral("borderColor"),                                                     line_color},
            {              QStringLiteral("pointBackgroundColor"),                                                     line_color},
            {                  QStringLiteral("pointBorderColor"),                                                     line_color},
            {                       QStringLiteral("pointRadius"),                                     isolatedPointRadii(points)},
            {                  QStringLiteral("pointHoverRadius"),                                                              4},
            {                       QStringLiteral("borderWidth"),                                                              2},
            {                       QStringLiteral("lineTension"),                                                              0},
            {                          QStringLiteral("spanGaps"),                                                          false},
            {                           QStringLiteral("xAxisID"), evaluation::chartAxisIdKey(evaluation::ChartAxisId::ScoreAxis)},
            {                           QStringLiteral("yAxisID"), evaluation::chartAxisIdKey(evaluation::ChartAxisId::CountAxis)},
            {                          QStringLiteral("showLine"),                                                           true},
            {                              QStringLiteral("fill"),                                                           true}
        };
    };
    const auto referenceDataset
        = [](const QString &label, const QString &color, const double value, const int max_count)
    {
        return QVariantMap{
            {           QStringLiteral("label"),          label                                                },
            {evaluation::fieldName(evaluation::Field::SeriesKind),
             evaluation::seriesKindKey(evaluation::SeriesKind::BestThreshold)},
            {evaluation::fieldName(evaluation::Field::Threshold), value},
            {evaluation::fieldName(evaluation::Field::ReadOnly), true},
            {evaluation::fieldName(evaluation::Field::Reference), true},
            {            QStringLiteral("data"),
             QVariantList{QVariantMap{{QStringLiteral("x"), value}, {QStringLiteral("y"), 0}},
             QVariantMap{{QStringLiteral("x"), value}, {QStringLiteral("y"), max_count}}}                      },
            { QStringLiteral("backgroundColor"),                                                          color},
            {     QStringLiteral("borderColor"),                                                          color},
            {    QStringLiteral("tooltipXOnly"),                                                           true},
            {     QStringLiteral("borderWidth"),                                                              2},
            {      QStringLiteral("borderDash"),                                             QVariantList{6, 4}},
            {     QStringLiteral("pointRadius"),                                                              0},
            {QStringLiteral("pointHoverRadius"),                                                              0},
            {     QStringLiteral("lineTension"),                                                              0},
            {        QStringLiteral("spanGaps"),                                                          false},
            {         QStringLiteral("xAxisID"), evaluation::chartAxisIdKey(evaluation::ChartAxisId::ScoreAxis)},
            {         QStringLiteral("yAxisID"), evaluation::chartAxisIdKey(evaluation::ChartAxisId::CountAxis)},
            {        QStringLiteral("showLine"),                                                           true},
            {            QStringLiteral("fill"),                                                          false}
        };
    };

    QVariantList datasets;
    // 异常检测的图例和颜色语义固定为正常/异常两组。即使当前筛选结果
    // 暂时没有其中一组样本，也保留空系列，避免图表在筛选前后改变结构。
    const QString good_color    = QStringLiteral("#43A047");
    const QString good_fill     = QStringLiteral("rgba(67, 160, 71, 0.24)");
    const QString anomaly_color = QStringLiteral("#E53935");
    const QString anomaly_fill  = QStringLiteral("rgba(229, 57, 53, 0.24)");
    datasets.push_back(distributionDataset(evaluation::displayText(evaluation::DisplayText::Good),
                                            evaluation::seriesKindKey(evaluation::SeriesKind::Good), good_color,
                                            good_fill, histogram.good_points));
    datasets.push_back(distributionDataset(evaluation::displayText(evaluation::DisplayText::Anomaly),
                                            evaluation::seriesKindKey(evaluation::SeriesKind::Anomaly), anomaly_color,
                                            anomaly_fill, histogram.anomaly_points));
    if (has_good)
        datasets.push_back(referenceDataset(
            QStringLiteral("正常 最大分数：%1").arg(QString::number(good_max, 'f', 4)), good_color, good_max,
            histogram.max_count));
    if (has_anomaly)
        datasets.push_back(referenceDataset(
            QStringLiteral("异常 最小分数：%1").arg(QString::number(anomaly_min, 'f', 4)), anomaly_color, anomaly_min,
            histogram.max_count));
    if (threshold_search != nullptr && threshold_search->available && std::isfinite(best_threshold))
    {
        const EvaluationThresholdPoint &best = threshold_search->best_point;
        datasets.push_back(referenceDataset(
            QStringLiteral("最佳阈值：%1").arg(QString::number(best.threshold, 'f', 4)), QStringLiteral("#D97706"),
            best.threshold, histogram.max_count));
    }
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
         {QStringLiteral("id"), evaluation::chartAxisIdKey(evaluation::ChartAxisId::ScoreAxis)},
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
         {QStringLiteral("id"), evaluation::chartAxisIdKey(evaluation::ChartAxisId::CountAxis)},
         {QStringLiteral("type"), QStringLiteral("linear")},
         {QStringLiteral("display"), true},
         {QStringLiteral("ticks"), QVariantMap{{QStringLiteral("beginAtZero"), true},
         {QStringLiteral("suggestedMax"), suggested_count}}},
         {QStringLiteral("scaleLabel"), QVariantMap{{QStringLiteral("display"), true},
         {QStringLiteral("labelString"), QString("数量")}}}}}}}                                                    }
    };

    return QVariantMap{
        {      evaluation::fieldName(evaluation::Field::Kind),evaluation::chartKindKey(evaluation::ChartKind::Line)                                                              },
        {   evaluation::fieldName(evaluation::Field::ChartId),
         evaluation::chartIdKey(evaluation::ChartId::AnomalyScoreDistribution)                                      },
        {evaluation::fieldName(evaluation::Field::FilterKind),
         evaluation::filterKindKey(evaluation::FilterKind::ImageScore)                                              },
        {     evaluation::fieldName(evaluation::Field::Title),          QString("异常分数分布（图像级 pred_score）")},
        {      evaluation::fieldName(evaluation::Field::Data),
         QVariantMap{{evaluation::fieldName(evaluation::Field::Labels), histogram.labels},
         {evaluation::fieldName(evaluation::Field::Datasets), datasets}}                                            },
        {   evaluation::fieldName(evaluation::Field::Options),                                               options}
    };
}

struct AnomalyScoreSample
{
    double score{0.0};
    bool   ground_truth_anomaly{false};
};

QVariantMap anomalyScoreChartForSamples(const QList<AnomalyScoreSample> &samples, const double classification_threshold,
                                        const EvaluationThresholdSearchResult *threshold_search)
{
    QVariantList good_scores;
    QVariantList anomaly_scores;
    bool         has_good    = false;
    bool         has_anomaly = false;
    double       good_max    = -std::numeric_limits<double>::infinity();
    double       anomaly_min = std::numeric_limits<double>::max();
    for (const AnomalyScoreSample &sample : samples)
    {
        if (sample.ground_truth_anomaly)
        {
            good_scores.push_back(QVariant());
            anomaly_scores.push_back(sample.score);
            has_anomaly = true;
            anomaly_min = std::min(anomaly_min, sample.score);
        }
        else
        {
            good_scores.push_back(sample.score);
            anomaly_scores.push_back(QVariant());
            has_good = true;
            good_max = std::max(good_max, sample.score);
        }
    }
    return anomalyScoreChart(good_scores, anomaly_scores, has_good, good_max, has_anomaly, anomaly_min,
                             classification_threshold, threshold_search);
}

EvaluationThresholdSearchResult anomalyThresholdSearchForImages(const QList<EvaluationImageData> &images,
                                                                const std::shared_ptr<std::atomic_bool> &cancel,
                                                                QString *err_msg)
{
    QElapsedTimer total_timer;
    total_timer.start();
    struct Sample
    {
        double score{0.0};
        bool   ground_truth_anomaly{false};
    };

    QVector<double> scores;
    QVector<Sample> ranked_samples;
    qint64           positive_ground_truth_count = 0;
    ranked_samples.reserve(images.size());
    for (const EvaluationImageData &image : images)
    {
        if (isCancelled(cancel))
        {
            if (err_msg != nullptr)
                *err_msg = QStringLiteral("评估已取消");
            return {};
        }
        bool ground_truth_anomaly = false;
        for (const EvaluationGroundTruthData &ground_truth : image.gt)
            ground_truth_anomaly = ground_truth_anomaly || ground_truth.anomaly;
        if (ground_truth_anomaly)
            ++positive_ground_truth_count;

        double      score      = 0.0;
        const bool  has_score  = evaluationAnomalyImageScore(image, &score);
        if (has_score)
        {
            scores.push_back(score);
            ranked_samples.push_back({score, ground_truth_anomaly});
        }
    }
    spdlog::debug("[评估耗时] threshold-search anomaly collect 完成: {} ms, images={}, scored_images={}, scores={}, "
                  "positive_gt={}",
                  total_timer.elapsed(), images.size(), ranked_samples.size(), scores.size(), positive_ground_truth_count);

    QElapsedTimer phase_timer;
    phase_timer.start();
    std::stable_sort(ranked_samples.begin(), ranked_samples.end(),
                     [](const Sample &lhs, const Sample &rhs) { return lhs.score > rhs.score; });
    const qint64 sort_elapsed = phase_timer.elapsed();
    phase_timer.restart();
    const QVector<double> candidates = evaluationThresholdCandidates(scores);
    const qint64 candidate_elapsed = phase_timer.elapsed();
    spdlog::debug("[评估耗时] threshold-search anomaly candidates 完成: sort={} ms, unique={} ms, candidates={}",
                  sort_elapsed, candidate_elapsed, candidates.size());
    phase_timer.restart();
    QVector<EvaluationThresholdPoint> points(candidates.size());
    EvaluationCounts                  counts{0, 0, positive_ground_truth_count};
    int                               active_count = 0;
    for (int index = candidates.size() - 1; index >= 0; --index)
    {
        if (isCancelled(cancel))
        {
            if (err_msg != nullptr)
                *err_msg = QStringLiteral("评估已取消");
            return {};
        }
        const double threshold = candidates.at(index);
        while (active_count < ranked_samples.size() && ranked_samples.at(active_count).score >= threshold)
        {
            if (ranked_samples.at(active_count).ground_truth_anomaly)
            {
                ++counts.tp;
                --counts.fn;
            }
            else
                ++counts.fp;
            ++active_count;
        }
        points[index] = evaluationThresholdPoint(threshold, counts);
    }
    const qint64 scan_elapsed = phase_timer.elapsed();

    const qint64 select_elapsed = 0;
    EvaluationThresholdSearchResult result
        = selectBestEvaluationThreshold(points, positive_ground_truth_count);
    spdlog::debug("[评估耗时] threshold-search anomaly 完成: scan={} ms, select={} ms, total={} ms, points={}, "
                  "available={}, best_threshold={}, best_f1={}",
                  scan_elapsed, select_elapsed, total_timer.elapsed(), result.points.size(), result.available,
                  result.best_point.threshold, result.best_point.f1);
    return result;
}

QVariantMap anomalyScoreChartForEvaluationImages(const QMap<qint64, EvaluationImageData> &images,
                                                 const double                             classification_threshold,
                                                 const EvaluationThresholdSearchResult *threshold_search)
{
    QList<AnomalyScoreSample> samples;
    samples.reserve(images.size());
    for (const EvaluationImageData &image : images)
    {
        bool ground_truth_anomaly = false;
        for (const EvaluationGroundTruthData &ground_truth : image.gt)
            ground_truth_anomaly = ground_truth_anomaly || ground_truth.anomaly;

        double      score      = 0.0;
        const bool  has_score  = evaluationAnomalyImageScore(image, &score);
        if (!has_score)
            continue;
        samples.push_back({score, ground_truth_anomaly});
    }
    return anomalyScoreChartForSamples(samples, classification_threshold, threshold_search);
}

struct PrecisionRecallClassCurve
{
    int                         class_id{-1};
    QString                     class_name;
    QVector<EvaluationThresholdPoint> threshold_points;
};

bool precisionRecallClassAllowed(const QVariantList &class_ids, const int class_id)
{
    if (class_ids.isEmpty())
        return true;
    for (const QVariant &value : class_ids)
        if (value.toInt() == class_id)
            return true;
    return false;
}

double interpolatePrecision(const QVector<double> &recall, const QVector<double> &precision, const double value)
{
    if (recall.isEmpty() || precision.isEmpty())
        return 0.0;
    if (value < recall.front())
        return precision.front();
    if (value >= recall.back())
        return precision.back();

    /* upper_bound 让重复 Recall 点取最后一个 envelope 值，和 np.interp 的离散行为一致。 */
    const auto upper = std::upper_bound(recall.cbegin(), recall.cend(), value);
    const int  right = static_cast<int>(std::distance(recall.cbegin(), upper)) - 1;
    const int  next  = right + 1;
    if (right < 0)
        return precision.front();
    if (next >= recall.size())
        return precision.back();
    const double width = recall.at(next) - recall.at(right);
    if (width <= 0.0)
        return precision.at(next);
    const double factor = (value - recall.at(right)) / width;
    return precision.at(right) + factor * (precision.at(next) - precision.at(right));
}

PrecisionRecallClassCurve makePrecisionRecallClassCurve(const int class_id, const QString &class_name,
                                                        const QList<EvaluationImageData>        &images,
                                                        const double                             iou_threshold,
                                                        const evaluation::MatchingStrategy       strategy,
                                                        const QVector<double>                    &thresholds,
                                                        const std::shared_ptr<std::atomic_bool> &cancel)
{
    PrecisionRecallClassCurve curve;
    curve.class_id   = class_id;
    curve.class_name = class_name;

    curve.threshold_points.reserve(thresholds.size());
    for (const double threshold : thresholds)
    {
        if (isCancelled(cancel))
            return {};

        EvaluationCounts counts;
        for (const EvaluationImageData &image : images)
        {
            if (isCancelled(cancel))
                return {};

            QList<EvaluationPredictionData> predictions;
            QList<EvaluationGroundTruthData> ground_truth;
            for (const EvaluationPredictionData &prediction : image.predictions)
            {
                if (prediction.class_id == class_id && std::isfinite(prediction.score)
                    && prediction.score >= threshold)
                    predictions.push_back(prediction);
            }
            for (const EvaluationGroundTruthData &value : image.gt)
            {
                if (value.class_id == class_id)
                    ground_truth.push_back(value);
            }

            std::stable_sort(predictions.begin(), predictions.end(),
                             [](const EvaluationPredictionData &lhs, const EvaluationPredictionData &rhs)
                             { return lhs.score > rhs.score; });
            const QList<MatchPair> matches
                = matchPredictionsByClass(predictions, ground_truth, iou_threshold, strategy, cancel);
            if (isCancelled(cancel))
                return {};

            QVector<bool> used_predictions(predictions.size(), false);
            QVector<bool> used_ground_truth(ground_truth.size(), false);
            for (const MatchPair &match : matches)
            {
                if (match.prediction < 0 || match.prediction >= predictions.size() || match.ground_truth < 0
                    || match.ground_truth >= ground_truth.size() || used_predictions.at(match.prediction)
                    || used_ground_truth.at(match.ground_truth))
                    continue;
                used_predictions[match.prediction]   = true;
                used_ground_truth[match.ground_truth] = true;
                ++counts.tp;
            }
            for (const bool used : used_predictions)
                if (!used)
                    ++counts.fp;
            for (const bool used : used_ground_truth)
                if (!used)
                    ++counts.fn;
        }
        curve.threshold_points.push_back(evaluationThresholdPoint(threshold, counts));
    }
    return curve;
}

struct SampledPrecisionRecallCurve
{
    struct Point
    {
        double recall{0.0};
        double precision{0.0};
        double threshold{std::numeric_limits<double>::quiet_NaN()};
    };

    QVector<Point> points;
    double         average_precision{0.0};
};

SampledPrecisionRecallCurve sampleThresholdCurve(const QVector<EvaluationThresholdPoint> &threshold_points)
{
    SampledPrecisionRecallCurve result;
    result.points.fill({}, kPrecisionRecallInterpolationPoints);
    if (threshold_points.isEmpty())
        return result;

    QVector<SampledPrecisionRecallCurve::Point> raw_points;
    raw_points.reserve(threshold_points.size() + 1);
    raw_points.push_back({0.0, 0.0, threshold_points.back().threshold});
    for (const EvaluationThresholdPoint &point : threshold_points)
    {
        if (std::isfinite(point.recall) && std::isfinite(point.precision))
            raw_points.push_back({std::clamp(point.recall, 0.0, 1.0), std::clamp(point.precision, 0.0, 1.0),
                                  point.threshold});
    }
    if (raw_points.isEmpty())
        return result;

    std::stable_sort(raw_points.begin(), raw_points.end(),
                     [](const SampledPrecisionRecallCurve::Point &lhs,
                        const SampledPrecisionRecallCurve::Point &rhs)
                     {
                         if (lhs.recall != rhs.recall)
                             return lhs.recall < rhs.recall;
                         if (lhs.precision != rhs.precision)
                             return lhs.precision > rhs.precision;
                         return lhs.threshold > rhs.threshold;
                     });

    QVector<double> recall;
    QVector<double> precision;
    QVector<double> thresholds;
    recall.reserve(raw_points.size());
    precision.reserve(raw_points.size());
    thresholds.reserve(raw_points.size());
    for (const auto &point : raw_points)
    {
        if (!recall.isEmpty() && std::abs(recall.back() - point.recall) <= 1e-12)
        {
            if (point.precision > precision.back()
                || (point.precision == precision.back() && point.threshold > thresholds.back()))
            {
                precision.back() = point.precision;
                thresholds.back() = point.threshold;
            }
        }
        else
        {
            recall.push_back(point.recall);
            precision.push_back(point.precision);
            thresholds.push_back(point.threshold);
        }
    }

    // The precision envelope is the standard PR display convention and keeps
    // the sampled line from hiding a better operating point at the same recall.
    for (int index = precision.size() - 2; index >= 0; --index)
        precision[index] = std::max(precision.at(index), precision.at(index + 1));

    const auto thresholdAtRecall = [&recall, &thresholds](const double value)
    {
        if (recall.isEmpty())
            return std::numeric_limits<double>::quiet_NaN();
        if (value <= recall.front())
            return thresholds.front();
        if (value >= recall.back())
            return thresholds.back();

        const auto upper = std::upper_bound(recall.cbegin(), recall.cend(), value);
        const int  right = static_cast<int>(std::distance(recall.cbegin(), upper)) - 1;
        const int  next  = right + 1;
        return value - recall.at(right) < recall.at(next) - value ? thresholds.at(right) : thresholds.at(next);
    };

    const int denominator = std::max(1, kPrecisionRecallInterpolationPoints - 1);
    double    previous_x  = 0.0;
    double    previous_y  = interpolatePrecision(recall, precision, previous_x);
    for (int index = 0; index < kPrecisionRecallInterpolationPoints; ++index)
    {
        const double x = static_cast<double>(index) / denominator;
        const double y = std::clamp(interpolatePrecision(recall, precision, x), 0.0, 1.0);
        result.points[index] = {x, y, thresholdAtRecall(x)};
        if (index > 0)
            result.average_precision += (x - previous_x) * (y + previous_y) * 0.5;
        previous_x = x;
        previous_y = y;
    }
    return result;
}

QVariantMap precisionRecallChartFromCurves(const QList<PrecisionRecallClassCurve> &curves,
                                           const EvaluationThresholdSearchResult *threshold_search)
{
    QVariantList datasets;

    if (threshold_search != nullptr && threshold_search->available)
    {
        const SampledPrecisionRecallCurve micro = sampleThresholdCurve(threshold_search->points);
        QVariantList                                    points;
        points.reserve(micro.points.size());
        for (const SampledPrecisionRecallCurve::Point &point : micro.points)
            points.push_back(QVariantMap{
                {QStringLiteral("x"), point.recall},
                {QStringLiteral("y"), point.precision},
                {evaluation::fieldName(evaluation::Field::Threshold), point.threshold}
            });

        datasets.push_back(QVariantMap{
            {evaluation::fieldName(evaluation::Field::Label),
             QStringLiteral("总体 micro (AP: %1)").arg(QString::number(micro.average_precision, 'f', 3))},
            {evaluation::fieldName(evaluation::Field::SeriesKind),
             evaluation::seriesKindKey(evaluation::SeriesKind::Micro)},
            {evaluation::fieldName(evaluation::Field::ClassId), -1},
            {evaluation::fieldName(evaluation::Field::ClassName), QStringLiteral("总体 micro")},
            {QStringLiteral("average_precision"), micro.average_precision},
            {evaluation::fieldName(evaluation::Field::Data), points},
            {QStringLiteral("borderColor"), QStringLiteral("#2563EB")},
            {QStringLiteral("backgroundColor"), QStringLiteral("#2563EB")},
            {QStringLiteral("pointBackgroundColor"), QStringLiteral("#2563EB")},
            {QStringLiteral("pointBorderColor"), QStringLiteral("#2563EB")},
            {QStringLiteral("borderWidth"), 3},
            {QStringLiteral("pointRadius"), 0},
            {QStringLiteral("pointHoverRadius"), 4},
            {QStringLiteral("lineTension"), 0},
            {QStringLiteral("fill"), false},
            {QStringLiteral("showLine"), true}
        });

        const EvaluationThresholdPoint &best = threshold_search->best_point;
        datasets.push_back(QVariantMap{
            {evaluation::fieldName(evaluation::Field::Label),
             QStringLiteral("最佳阈值：%1").arg(QString::number(best.threshold, 'f', 4))},
            {evaluation::fieldName(evaluation::Field::SeriesKind),
             evaluation::seriesKindKey(evaluation::SeriesKind::BestThreshold)},
            {evaluation::fieldName(evaluation::Field::Threshold), best.threshold},
            {evaluation::fieldName(evaluation::Field::BestF1), best.f1},
            {evaluation::fieldName(evaluation::Field::ReadOnly), true},
            {evaluation::fieldName(evaluation::Field::Reference), true},
            {evaluation::fieldName(evaluation::Field::Data),
             QVariantList{QVariantMap{{QStringLiteral("x"), best.recall},
                                      {QStringLiteral("y"), best.precision},
                                      {evaluation::fieldName(evaluation::Field::Threshold), best.threshold},
                                      {evaluation::fieldName(evaluation::Field::BestF1), best.f1}}}},
            {QStringLiteral("borderColor"), QStringLiteral("#D97706")},
            {QStringLiteral("backgroundColor"), QStringLiteral("#D97706")},
            {QStringLiteral("pointBackgroundColor"), QStringLiteral("#D97706")},
            {QStringLiteral("pointBorderColor"), QStringLiteral("#D97706")},
            {QStringLiteral("borderWidth"), 0},
            {QStringLiteral("pointRadius"), 6},
            {QStringLiteral("pointHoverRadius"), 7},
            {QStringLiteral("showLine"), false},
            {QStringLiteral("fill"), false}
        });
    }

    for (const PrecisionRecallClassCurve &curve : curves)
    {
        const SampledPrecisionRecallCurve sampled = sampleThresholdCurve(curve.threshold_points);
        QVariantList points;
        points.reserve(sampled.points.size());
        for (const SampledPrecisionRecallCurve::Point &point : sampled.points)
            points.push_back(QVariantMap{
                {QStringLiteral("x"), point.recall},
                {QStringLiteral("y"), point.precision},
                {evaluation::fieldName(evaluation::Field::Threshold), point.threshold}
            });
        const QString color = classColor(curve.class_id);
        datasets.push_back(QVariantMap{
            {evaluation::fieldName(evaluation::Field::Label),
             QStringLiteral("%1 (AP: %2)").arg(curve.class_name).arg(QString::number(sampled.average_precision, 'f', 3))},
            {evaluation::fieldName(evaluation::Field::SeriesKind),
             evaluation::seriesKindKey(evaluation::SeriesKind::Class)},
            {evaluation::fieldName(evaluation::Field::ClassId), curve.class_id},
            {evaluation::fieldName(evaluation::Field::ClassName), curve.class_name},
            {QStringLiteral("average_precision"), sampled.average_precision},
            {evaluation::fieldName(evaluation::Field::Data), points},
            {QStringLiteral("borderColor"), color},
            {QStringLiteral("backgroundColor"), color},
            {QStringLiteral("pointBackgroundColor"), color},
            {QStringLiteral("pointBorderColor"), color},
            {QStringLiteral("borderWidth"), 2},
            {QStringLiteral("pointRadius"), 0},
            {QStringLiteral("pointHoverRadius"), 4},
            {QStringLiteral("lineTension"), 0},
            {QStringLiteral("fill"), false},
            {QStringLiteral("showLine"), true}
        });
    }

    const QVariantMap ticks{
        {          QStringLiteral("min"), 0.0},
        {          QStringLiteral("max"), 1.0},
        {     QStringLiteral("stepSize"), 0.2},
        {QStringLiteral("maxTicksLimit"),   6},
        {    QStringLiteral("precision"),   1},
        {  QStringLiteral("maxRotation"),   0},
        {  QStringLiteral("minRotation"),   0}
    };
    const QVariantMap scales{
        {QStringLiteral("xAxes"),
         QVariantList{QVariantMap{
         {QStringLiteral("type"), QStringLiteral("linear")},
         {QStringLiteral("display"), true},
         {QStringLiteral("ticks"), ticks},
         {QStringLiteral("scaleLabel"), QVariantMap{{QStringLiteral("display"), true},
         {QStringLiteral("labelString"), QStringLiteral("Recall")}}}}}   },
        {QStringLiteral("yAxes"),
         QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("linear")},
         {QStringLiteral("display"), true},
         {QStringLiteral("ticks"), ticks},
         {QStringLiteral("scaleLabel"),
         QVariantMap{{QStringLiteral("display"), true},
         {QStringLiteral("labelString"), QStringLiteral("Precision")}}}}}}
    };
    return QVariantMap{
        {      evaluation::fieldName(evaluation::Field::Kind),evaluation::chartKindKey(evaluation::ChartKind::Line)                                                              },
        {   evaluation::fieldName(evaluation::Field::ChartId),
         evaluation::chartIdKey(evaluation::ChartId::PrecisionRecall)                                               },
        {evaluation::fieldName(evaluation::Field::FilterKind),
         evaluation::filterKindKey(evaluation::FilterKind::PrecisionRecall)                                         },
        {     evaluation::fieldName(evaluation::Field::Title),               QStringLiteral("Precision-Recall 曲线")},
        {      evaluation::fieldName(evaluation::Field::Data),
         QVariantMap{{evaluation::fieldName(evaluation::Field::Labels), QVariantList{}},
         {evaluation::fieldName(evaluation::Field::Datasets), datasets}}                                            },
        {   evaluation::fieldName(evaluation::Field::Options),
         QVariantMap{{QStringLiteral("maintainAspectRatio"), false},
         {QStringLiteral("responsive"), true},
         {QStringLiteral("legend"), QVariantMap{{QStringLiteral("display"), true},
         {QStringLiteral("position"), QStringLiteral("top")}}},
         {QStringLiteral("tooltips"), QVariantMap{{QStringLiteral("mode"), QStringLiteral("nearest")},
         {QStringLiteral("intersect"), false}}},
         {QStringLiteral("scales"), scales}}                                                                        }
    };
}

struct ThresholdEdge
{
    int    prediction{0};
    int    ground_truth{0};
    double iou{0.0};
};

struct PreparedThresholdClass
{
    int                                   class_id{-1};
    QVector<EvaluationPredictionData>     predictions;
    QVector<EvaluationGroundTruthData>     ground_truth;
    QVector<QVector<double>>               ious;
    QVector<ThresholdEdge>                 greedy_edges;
    int                                   active_prediction_count{0};
};

struct PreparedThresholdImage
{
    QVector<PreparedThresholdClass> classes;
};

EvaluationThresholdSearchResult instanceThresholdSearchForImages(
    const QList<EvaluationImageData> &images, const double iou_threshold, const evaluation::MatchingStrategy strategy,
    const QVariantList &class_ids, const std::shared_ptr<std::atomic_bool> &cancel, QString *err_msg)
{
    QElapsedTimer total_timer;
    total_timer.start();
    const auto allowed = [&class_ids](const int class_id)
    {
        return precisionRecallClassAllowed(class_ids, class_id);
    };

    QVector<double> scores;
    qint64           positive_ground_truth_count = 0;
    QVector<PreparedThresholdImage> prepared_images;
    prepared_images.reserve(images.size());
    qint64       prediction_count = 0;
    qint64       ground_truth_count = 0;
    qint64       iou_pair_count = 0;
    qint64       edge_count = 0;
    qint64       iou_elapsed = 0;
    QElapsedTimer iou_timer;
    iou_timer.start();

    for (const EvaluationImageData &image : images)
    {
        if (isCancelled(cancel))
        {
            if (err_msg != nullptr)
                *err_msg = QStringLiteral("评估已取消");
            return {};
        }

        QMap<int, QList<EvaluationPredictionData>> predictions_by_class;
        QMap<int, QList<EvaluationGroundTruthData>> ground_truth_by_class;
        for (const EvaluationGroundTruthData &ground_truth : image.gt)
        {
            if (!allowed(ground_truth.class_id))
                continue;
            ground_truth_by_class[ground_truth.class_id].push_back(ground_truth);
            ++positive_ground_truth_count;
        }
        for (const EvaluationPredictionData &prediction : image.predictions)
        {
            if (!allowed(prediction.class_id) || !std::isfinite(prediction.score))
                continue;
            predictions_by_class[prediction.class_id].push_back(prediction);
            scores.push_back(prediction.score);
        }

        QSet<int> class_set;
        for (auto it = predictions_by_class.cbegin(); it != predictions_by_class.cend(); ++it)
            class_set.insert(it.key());
        for (auto it = ground_truth_by_class.cbegin(); it != ground_truth_by_class.cend(); ++it)
            class_set.insert(it.key());

        PreparedThresholdImage prepared_image;
        prepared_image.classes.reserve(class_set.size());
        for (const int class_id : class_set)
        {
            PreparedThresholdClass prepared_class;
            prepared_class.class_id = class_id;
            const QList<EvaluationPredictionData> predictions = predictions_by_class.value(class_id);
            const QList<EvaluationGroundTruthData> ground_truth = ground_truth_by_class.value(class_id);
            for (const EvaluationPredictionData &prediction : predictions)
                prepared_class.predictions.push_back(prediction);
            for (const EvaluationGroundTruthData &value : ground_truth)
                prepared_class.ground_truth.push_back(value);
            prediction_count += prepared_class.predictions.size();
            ground_truth_count += prepared_class.ground_truth.size();
            std::stable_sort(prepared_class.predictions.begin(), prepared_class.predictions.end(),
                             [](const EvaluationPredictionData &lhs, const EvaluationPredictionData &rhs)
                             { return lhs.score > rhs.score; });

            prepared_class.ious.resize(prepared_class.predictions.size());
            iou_timer.restart();
            for (int prediction_index = 0; prediction_index < prepared_class.predictions.size(); ++prediction_index)
            {
                if (isCancelled(cancel))
                {
                    if (err_msg != nullptr)
                        *err_msg = QStringLiteral("评估已取消");
                    return {};
                }
                QVector<double> &prediction_ious = prepared_class.ious[prediction_index];
                prediction_ious.resize(prepared_class.ground_truth.size());
                for (int ground_truth_index = 0; ground_truth_index < prepared_class.ground_truth.size();
                     ++ground_truth_index)
                {
                    if ((ground_truth_index & 0x3f) == 0 && isCancelled(cancel))
                    {
                        if (err_msg != nullptr)
                            *err_msg = QStringLiteral("评估已取消");
                        return {};
                    }
                    const EvaluationBox &prediction_box = prepared_class.predictions.at(prediction_index).box;
                    const EvaluationBox &ground_truth_box = prepared_class.ground_truth.at(ground_truth_index).box;
                    const double iou = (!prediction_box.valid() && !ground_truth_box.valid())
                                         ? 1.0
                                         : intersectionOverUnion(prediction_box, ground_truth_box);
                    ++iou_pair_count;
                    prediction_ious[ground_truth_index] = iou;
                    if (iou >= iou_threshold)
                        prepared_class.greedy_edges.push_back({prediction_index, ground_truth_index, iou});
                }
            }
            std::sort(prepared_class.greedy_edges.begin(), prepared_class.greedy_edges.end(),
                      [](const ThresholdEdge &lhs, const ThresholdEdge &rhs)
                      {
                          if (lhs.iou != rhs.iou)
                              return lhs.iou > rhs.iou;
                          if (lhs.prediction != rhs.prediction)
                              return lhs.prediction < rhs.prediction;
                          return lhs.ground_truth < rhs.ground_truth;
                      });
            iou_elapsed += iou_timer.elapsed();
            edge_count += prepared_class.greedy_edges.size();
            prepared_image.classes.push_back(std::move(prepared_class));
        }
        prepared_images.push_back(std::move(prepared_image));
    }

    const qint64 preparation_elapsed = total_timer.elapsed();
    spdlog::debug("[评估耗时] threshold-search instance prepare 完成: {} ms, images={}, predictions={}, gt={}, "
                  "iou_pairs={}, iou_compute={} ms, edges={}, scores={}, positive_gt={}",
                  preparation_elapsed, images.size(), prediction_count, ground_truth_count, iou_pair_count,
                  iou_elapsed, edge_count, scores.size(), positive_ground_truth_count);

    QElapsedTimer phase_timer;
    phase_timer.start();
    const QVector<double> candidates = evaluationThresholdCandidates(scores);
    const qint64 candidate_elapsed = phase_timer.elapsed();
    QVector<EvaluationThresholdPoint> global_points(candidates.size());
    QMap<int, QVector<EvaluationThresholdPoint>> class_points;
    for (const PreparedThresholdImage &image : prepared_images)
        for (const PreparedThresholdClass &prepared_class : image.classes)
            class_points[prepared_class.class_id].resize(candidates.size());

    std::vector<std::unique_ptr<IncrementalHungarianMatcher>> hungarian_matchers;
    if (strategy == evaluation::MatchingStrategy::HungarianIoU)
    {
        for (PreparedThresholdImage &image : prepared_images)
            for (PreparedThresholdClass &prepared_class : image.classes)
                hungarian_matchers.push_back(std::make_unique<IncrementalHungarianMatcher>(
                    prepared_class.predictions.size(), prepared_class.ground_truth.size(), prepared_class.ious,
                    iou_threshold));
    }
    const qint64 matcher_elapsed = phase_timer.elapsed() - candidate_elapsed;
    spdlog::debug("[评估耗时] threshold-search instance candidates/matchers 完成: candidates={} ({}) ms, matchers={} "
                  "({} ms), strategy={}",
                  candidates.size(), candidate_elapsed, hungarian_matchers.size(), matcher_elapsed,
                  static_cast<int>(strategy));

    int matcher_index = 0;
    for (int candidate_index = candidates.size() - 1; candidate_index >= 0; --candidate_index)
    {
        if (isCancelled(cancel))
        {
            if (err_msg != nullptr)
                *err_msg = QStringLiteral("评估已取消");
            return {};
        }
        const double threshold = candidates.at(candidate_index);
        EvaluationCounts global_counts;
        matcher_index = 0;
        for (PreparedThresholdImage &image : prepared_images)
        {
            for (PreparedThresholdClass &prepared_class : image.classes)
            {
                while (prepared_class.active_prediction_count < prepared_class.predictions.size()
                       && prepared_class.predictions.at(prepared_class.active_prediction_count).score >= threshold)
                {
                    if (strategy == evaluation::MatchingStrategy::HungarianIoU
                        && !hungarian_matchers.at(matcher_index)->addPrediction(
                            prepared_class.active_prediction_count, cancel))
                    {
                        if (isCancelled(cancel))
                        {
                            if (err_msg != nullptr)
                                *err_msg = QStringLiteral("评估已取消");
                            return {};
                        }
                    }
                    ++prepared_class.active_prediction_count;
                }

                EvaluationCounts class_count;
                if (strategy == evaluation::MatchingStrategy::HungarianIoU)
                {
                    const QList<MatchPair> matches = hungarian_matchers.at(matcher_index)->matches(cancel);
                    class_count.tp = matches.size();
                }
                else
                {
                    QVector<bool> used_ground_truth(prepared_class.ground_truth.size(), false);
                    QVector<bool> used_prediction(prepared_class.active_prediction_count, false);
                    for (int edge_index = 0; edge_index < prepared_class.greedy_edges.size(); ++edge_index)
                    {
                        if ((edge_index & 0x3f) == 0 && isCancelled(cancel))
                        {
                            if (err_msg != nullptr)
                                *err_msg = QStringLiteral("评估已取消");
                            return {};
                        }
                        const ThresholdEdge &edge = prepared_class.greedy_edges.at(edge_index);
                        if (edge.prediction >= prepared_class.active_prediction_count
                            || used_prediction.at(edge.prediction) || used_ground_truth.at(edge.ground_truth))
                            continue;
                        used_prediction[edge.prediction] = true;
                        used_ground_truth[edge.ground_truth] = true;
                        ++class_count.tp;
                    }
                }
                class_count.fp = prepared_class.active_prediction_count - class_count.tp;
                class_count.fn = prepared_class.ground_truth.size() - class_count.tp;
                global_counts.tp += class_count.tp;
                global_counts.fp += class_count.fp;
                global_counts.fn += class_count.fn;
                class_points[prepared_class.class_id][candidate_index]
                    = evaluationThresholdPoint(threshold, class_count);
                ++matcher_index;
            }
        }
        global_points[candidate_index] = evaluationThresholdPoint(threshold, global_counts);
    }
    const qint64 scan_elapsed = phase_timer.elapsed();

    const qint64 select_elapsed = 0;
    EvaluationThresholdSearchResult result
        = selectBestEvaluationThreshold(global_points, positive_ground_truth_count);
    if (result.available)
        result.class_points = std::move(class_points);
    spdlog::debug("[评估耗时] threshold-search instance 完成: prepare={} ms, candidates={} ms, matchers={} ms, "
                  "scan={} ms, select={} ms, total={} ms, points={}, available={}, best_threshold={}, best_f1={}",
                  preparation_elapsed, candidate_elapsed, matcher_elapsed, scan_elapsed, select_elapsed,
                  total_timer.elapsed(), result.points.size(), result.available, result.best_point.threshold,
                  result.best_point.f1);
    return result;
}

QVariantMap buildPrecisionRecallChart(const QList<EvaluationImageData> &images,
                                      const QMap<int, QString> &class_catalog, const double iou_threshold,
                                      const evaluation::MatchingStrategy strategy, const QVariantList &class_ids,
                                      const std::shared_ptr<std::atomic_bool> &cancel,
                                      const EvaluationThresholdSearchResult *threshold_search)
{
    QSet<int>          target_classes;
    QMap<int, QString> class_names = class_catalog;
    for (const EvaluationImageData &image : images)
    {
        if (isCancelled(cancel))
            return {};
        for (const EvaluationGroundTruthData &ground_truth : image.gt)
        {
            if (ground_truth.class_id < 0 || !precisionRecallClassAllowed(class_ids, ground_truth.class_id))
                continue;
            target_classes.insert(ground_truth.class_id);
            if (!ground_truth.class_name.isEmpty())
                class_names.insert(ground_truth.class_id, ground_truth.class_name);
        }
        for (const EvaluationPredictionData &prediction : image.predictions)
        {
            if (prediction.class_id < 0 || !precisionRecallClassAllowed(class_ids, prediction.class_id))
                continue;
            target_classes.insert(prediction.class_id);
            if (!prediction.class_name.isEmpty())
                class_names.insert(prediction.class_id, prediction.class_name);
        }
    }

    QList<PrecisionRecallClassCurve> curves;
    curves.reserve(target_classes.size());
    QVector<double> thresholds;
    if (threshold_search != nullptr)
    {
        thresholds.reserve(threshold_search->points.size());
        for (const EvaluationThresholdPoint &point : threshold_search->points)
            thresholds.push_back(point.threshold);
    }
    for (auto it = class_names.cbegin(); it != class_names.cend(); ++it)
    {
        if (!target_classes.contains(it.key()))
            continue;
        const QString name = it.value().isEmpty() ? QString::number(it.key()) : it.value();
        PrecisionRecallClassCurve curve;
        curve.class_id   = it.key();
        curve.class_name = name;
        if (threshold_search != nullptr)
        {
            const auto points = threshold_search->class_points.constFind(it.key());
            if (points != threshold_search->class_points.cend())
                curve.threshold_points = points.value();
        }
        if (curve.threshold_points.isEmpty())
            curve = makePrecisionRecallClassCurve(it.key(), name, images, iou_threshold, strategy, thresholds, cancel);
        curves.push_back(std::move(curve));
        if (isCancelled(cancel))
            return {};
    }
    return precisionRecallChartFromCurves(curves, threshold_search);
}

QVariantMap confidenceDistributionChart(const QList<EvaluationImageData> &images,
                                        const EvaluationThresholdSearchResult *threshold_search)
{
    constexpr int    bin_count = 24;
    constexpr double bin_width = 1.0 / static_cast<double>(bin_count);
    QVector<int>     counts(bin_count, 0);
    for (const EvaluationImageData &image : images)
    {
        for (const EvaluationPredictionData &prediction : image.predictions)
        {
            if (!std::isfinite(prediction.score))
                continue;
            const double score = std::clamp(prediction.score, 0.0, 1.0);
            const int    index = std::clamp(static_cast<int>(std::floor(score / bin_width)), 0, bin_count - 1);
            ++counts[index];
        }
    }

    QVariantList labels;
    QVariantList points;
    labels.reserve(bin_count);
    points.reserve(bin_count);
    int max_count = 0;
    for (int index = 0; index < bin_count; ++index)
    {
        const double center = (static_cast<double>(index) + 0.5) * bin_width;
        labels.push_back(QString::number(center, 'f', 3));
        points.push_back(QVariantMap{{QStringLiteral("x"), center}, {QStringLiteral("y"), counts.at(index)}});
        max_count = std::max(max_count, counts.at(index));
    }

    QVariantList datasets;
    datasets.push_back(QVariantMap{
        {evaluation::fieldName(evaluation::Field::Label), QStringLiteral("总体置信度")},
        {evaluation::fieldName(evaluation::Field::SeriesKind),
         evaluation::seriesKindKey(evaluation::SeriesKind::Overall)},
        {evaluation::fieldName(evaluation::Field::Data), points},
        {QStringLiteral("type"), evaluation::chartKindKey(evaluation::ChartKind::Bar)},
        {QStringLiteral("backgroundColor"), QStringLiteral("rgba(37, 99, 235, 0.55)")},
        {QStringLiteral("borderColor"), QStringLiteral("#2563EB")},
        {QStringLiteral("borderWidth"), 1},
        {QStringLiteral("barPercentage"), 1.0},
        {QStringLiteral("categoryPercentage"), 1.0}
    });

    if (threshold_search != nullptr && threshold_search->available)
    {
        const double threshold = std::clamp(threshold_search->best_point.threshold, 0.0, 1.0);
        datasets.push_back(QVariantMap{
            {evaluation::fieldName(evaluation::Field::Label),
             QStringLiteral("最佳阈值：%1").arg(QString::number(threshold_search->best_point.threshold, 'f', 4))},
            {evaluation::fieldName(evaluation::Field::SeriesKind),
             evaluation::seriesKindKey(evaluation::SeriesKind::BestThreshold)},
            {evaluation::fieldName(evaluation::Field::Threshold), threshold_search->best_point.threshold},
            {evaluation::fieldName(evaluation::Field::ReadOnly), true},
            {evaluation::fieldName(evaluation::Field::Reference), true},
            {QStringLiteral("type"), evaluation::chartKindKey(evaluation::ChartKind::Line)},
            {QStringLiteral("data"),
             QVariantList{QVariantMap{{QStringLiteral("x"), threshold}, {QStringLiteral("y"), 0}},
                          QVariantMap{{QStringLiteral("x"), threshold}, {QStringLiteral("y"), max_count}}}},
            {QStringLiteral("borderColor"), QStringLiteral("#D97706")},
            {QStringLiteral("backgroundColor"), QStringLiteral("#D97706")},
            {QStringLiteral("borderWidth"), 2},
            {QStringLiteral("borderDash"), QVariantList{6, 4}},
            {QStringLiteral("pointRadius"), 0},
            {QStringLiteral("showLine"), true},
            {QStringLiteral("fill"), false}
        });
    }

    const QVariantMap ticks{
        {QStringLiteral("min"), 0.0},
        {QStringLiteral("max"), 1.0},
        {QStringLiteral("stepSize"), 0.2},
        {QStringLiteral("maxTicksLimit"), 6},
        {QStringLiteral("precision"), 1},
        {QStringLiteral("maxRotation"), 0},
        {QStringLiteral("minRotation"), 0}
    };
    const QVariantMap options{
        {QStringLiteral("maintainAspectRatio"), false},
        {QStringLiteral("responsive"), true},
        {QStringLiteral("legend"), QVariantMap{{QStringLiteral("display"), true},
                                                 {QStringLiteral("position"), QStringLiteral("top")}}},
        {QStringLiteral("scales"), QVariantMap{
            {QStringLiteral("xAxes"), QVariantList{QVariantMap{
                {QStringLiteral("type"), QStringLiteral("linear")},
                {QStringLiteral("display"), true},
                {QStringLiteral("ticks"), ticks},
                {QStringLiteral("scaleLabel"), QVariantMap{{QStringLiteral("display"), true},
                                                             {QStringLiteral("labelString"), QStringLiteral("置信度")}}}
            }}},
            {QStringLiteral("yAxes"), QVariantList{QVariantMap{
                {QStringLiteral("type"), QStringLiteral("linear")},
                {QStringLiteral("display"), true},
                {QStringLiteral("ticks"), QVariantMap{{QStringLiteral("beginAtZero"), true},
                                                        {QStringLiteral("suggestedMax"), max_count > 0 ? max_count * 1.1 : 1}}},
                {QStringLiteral("scaleLabel"), QVariantMap{{QStringLiteral("display"), true},
                                                             {QStringLiteral("labelString"), QStringLiteral("数量")}}}
            }}}
        }}
    };
    return QVariantMap{
        {evaluation::fieldName(evaluation::Field::Kind), evaluation::chartKindKey(evaluation::ChartKind::Bar)},
        {evaluation::fieldName(evaluation::Field::ChartId),
         evaluation::chartIdKey(evaluation::ChartId::ConfidenceDistribution)},
        {evaluation::fieldName(evaluation::Field::FilterKind),
         evaluation::filterKindKey(evaluation::FilterKind::ImageScore)},
        {evaluation::fieldName(evaluation::Field::Title), QStringLiteral("置信度分布")},
        {evaluation::fieldName(evaluation::Field::Data),
         QVariantMap{{evaluation::fieldName(evaluation::Field::Labels), labels},
                     {evaluation::fieldName(evaluation::Field::Datasets), datasets}}},
        {evaluation::fieldName(evaluation::Field::Options), options}
    };
}

} // namespace

EvaluationThresholdSearchResult searchAnomalyThresholdForImages(
    const QList<EvaluationImageData> &images, const std::shared_ptr<std::atomic_bool> &cancel, QString *err_msg)
{
    return anomalyThresholdSearchForImages(images, cancel, err_msg);
}

EvaluationThresholdSearchResult searchInstanceThresholdForImages(
    const QList<EvaluationImageData> &images, const double iou_threshold,
    const evaluation::MatchingStrategy strategy, const QVariantList &class_ids,
    const std::shared_ptr<std::atomic_bool> &cancel, QString *err_msg)
{
    return instanceThresholdSearchForImages(images, iou_threshold, strategy, class_ids, cancel, err_msg);
}

QVariantMap anomalyScoreChartForImages(const QList<EvaluationImageData> &images, const double classification_threshold,
                                       const EvaluationThresholdSearchResult *threshold_search)
{
    QList<AnomalyScoreSample> samples;
    samples.reserve(images.size());
    for (const EvaluationImageData &image : images)
    {
        double score = 0.0;
        if (!evaluationAnomalyImageScore(image, &score))
            continue;
        const bool   ground_truth_anomaly
            = std::any_of(image.gt.cbegin(), image.gt.cend(),
                          [](const EvaluationGroundTruthData &ground_truth) { return ground_truth.anomaly; });
        samples.push_back({score, ground_truth_anomaly});
    }
    return anomalyScoreChartForSamples(samples, classification_threshold, threshold_search);
}

QVariantMap anomalyPrecisionRecallChartForImages(const QList<EvaluationImageData> &images,
                                                 const EvaluationThresholdSearchResult *threshold_search,
                                                 const std::shared_ptr<std::atomic_bool> &cancel)
{
    EvaluationThresholdSearchResult local_search;
    if (threshold_search == nullptr)
        local_search = anomalyThresholdSearchForImages(images, cancel, nullptr);
    const EvaluationThresholdSearchResult *search
        = threshold_search != nullptr ? threshold_search : &local_search;
    return precisionRecallChartFromCurves({}, search);
}

QVariantMap confidenceDistributionChartForImages(const QList<EvaluationImageData> &images,
                                                  const EvaluationThresholdSearchResult *threshold_search)
{
    return confidenceDistributionChart(images, threshold_search);
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

QVariantMap precisionRecallChartForImages(const QList<EvaluationImageData> &images,
                                           const QMap<int, QString> &class_catalog, const double iou_threshold,
                                           const evaluation::MatchingStrategy strategy, const QVariantList &class_ids,
                                           const std::shared_ptr<std::atomic_bool> &cancel,
                                           const EvaluationThresholdSearchResult *threshold_search)
{
    EvaluationThresholdSearchResult local_search;
    if (threshold_search == nullptr)
        local_search = instanceThresholdSearchForImages(images, iou_threshold, strategy, class_ids, cancel, nullptr);
    const EvaluationThresholdSearchResult *search
        = threshold_search != nullptr ? threshold_search : &local_search;
    return buildPrecisionRecallChart(images, class_catalog, iou_threshold, strategy, class_ids, cancel, search);
}

EvaluationChartOutput buildAnomalyEvaluationCharts(const QMap<qint64, EvaluationImageData> &images,
                                                   const QVariantMap &diagnostic, const double confidence,
                                                   const EvaluationThresholdSearchResult *threshold_search)
{
    // 异常检测采用图像级二元分类，正常样本是没有 GT 标签的隐式负类。指标定义为预测分数高于置信度阈值。
    EvaluationChartOutput output;
    output.available = true;
    output.metrics   = QVariantMap{
        { evaluation::fieldName(evaluation::Field::Available),true                                                              },
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
    EvaluationThresholdSearchResult local_search;
    if (threshold_search == nullptr)
    {
        QList<EvaluationImageData> chart_images;
        chart_images.reserve(images.size());
        for (const EvaluationImageData &image : images)
            chart_images.push_back(image);
        local_search = anomalyThresholdSearchForImages(chart_images, {}, nullptr);
    }
    const EvaluationThresholdSearchResult *search
        = threshold_search != nullptr ? threshold_search : &local_search;
    output.charts.push_back(precisionRecallChartFromCurves({}, search));
    output.charts.push_back(anomalyScoreChartForEvaluationImages(images, confidence, search));
    output.chart_kinds.push_back(evaluation::chartKindKey(evaluation::ChartKind::Line));
    output.chart_kinds.push_back(evaluation::chartKindKey(evaluation::ChartKind::Line));
    return output;
}

EvaluationChartOutput buildInstanceMatchingEvaluationCharts(const QMap<qint64, EvaluationImageData> &images,
                                                             const double, const double iou_threshold,
                                                             const evaluation::MatchingStrategy       strategy,
                                                             const QVariantMap                       &diagnostic,
                                                             const std::shared_ptr<std::atomic_bool> &cancel,
                                                             const EvaluationThresholdSearchResult *threshold_search)
{
    EvaluationChartOutput output;
    if (isCancelled(cancel))
        return output;

    QList<EvaluationImageData> chart_images;
    chart_images.reserve(images.size());
    for (const EvaluationImageData &image : images) chart_images.push_back(image);

    EvaluationThresholdSearchResult local_search;
    if (threshold_search == nullptr)
        local_search = instanceThresholdSearchForImages(chart_images, iou_threshold, strategy, {}, cancel, nullptr);
    const EvaluationThresholdSearchResult *search
        = threshold_search != nullptr ? threshold_search : &local_search;

    QMap<int, QString> class_catalog;
    const QVariantList per_class = diagnostic.value(evaluation::fieldName(evaluation::Field::Instance))
                                       .toMap()
                                       .value(evaluation::fieldName(evaluation::Field::PerClass))
                                       .toList();
    for (const QVariant &value : per_class)
    {
        const QVariantMap metric   = value.toMap();
        const int         class_id = metric.value(evaluation::fieldName(evaluation::Field::ClassId)).toInt();
        if (class_id >= 0)
            class_catalog.insert(class_id,
                                 metric.value(evaluation::fieldName(evaluation::Field::ClassName)).toString());
    }
    const QVariantMap precision_recall
        = precisionRecallChartForImages(chart_images, class_catalog, iou_threshold, strategy, {}, cancel,
                                        search);
    if (isCancelled(cancel))
        return output;
    output.available = true;
    output.metrics   = QVariantMap{
        { evaluation::fieldName(evaluation::Field::Available),true                                                              },
        {  evaluation::fieldName(evaluation::Field::Instance),
         diagnostic.value(evaluation::fieldName(evaluation::Field::Instance))
         .toMap()
         .value(evaluation::fieldName(evaluation::Field::Overall))                                        },
        {  evaluation::fieldName(evaluation::Field::PerClass),
         diagnostic.value(evaluation::fieldName(evaluation::Field::Instance))
         .toMap()
         .value(evaluation::fieldName(evaluation::Field::PerClass))                                       },
        {evaluation::fieldName(evaluation::Field::Definition), QStringLiteral("confidence_iou_work_point")}
    };
    output.image_definition = QVariantMap{
        {        evaluation::fieldName(evaluation::Field::SampleUnit),     QStringLiteral("image_presence")},
        {       evaluation::fieldName(evaluation::Field::Aggregation),             QStringLiteral("binary")},
        {evaluation::fieldName(evaluation::Field::PositiveDefinition), QStringLiteral("gt_or_pred_present")},
        {   evaluation::fieldName(evaluation::Field::HasImageMetrics),                                 true}
    };
    output.charts.push_back(precision_recall);
    output.charts.push_back(confidenceDistributionChartForImages(chart_images, search));
    output.chart_kinds.push_back(evaluation::chartKindKey(evaluation::ChartKind::Line));
    output.chart_kinds.push_back(evaluation::chartKindKey(evaluation::ChartKind::Bar));
    return output;
}

EvaluationInstanceRecord buildInstanceRecord(const EvaluationImageData &image, const evaluation::Status status,
                                             const EvaluationGroundTruthData *gt,
                                             const EvaluationPredictionData *pred, const double iou,
                                             const QString &dataset_root, const QString &prediction_root,
                                             const qint64 event_index)
{
    // 视口裁剪由 thumbnail provider 在渲染时根据 URL 中的绝对 bounds 推导。
    // QML 按 LabelInstanceThumbnail 模式使用原始几何换算 overlay，评估线程因此不再依赖图像宽高。
    EvaluationInstanceRecord record;
    record.event_uuid       = QStringLiteral("%1-%2").arg(image.id).arg(event_index);
    record.image_id         = image.id;
    record.dataset_id       = image.dataset_id;
    record.image_name       = image.name;
    record.image_path       = image.path;
    record.image_width      = image.width;
    record.image_height     = image.height;
    record.status            = status;
    record.score             = pred ? pred->score : 0.0;
    record.iou               = iou;
    record.gt_label_id       = gt ? gt->label_id : -1;
    record.gt_instance_id    = record.gt_label_id >= 0 ? QString::number(record.gt_label_id) : QString();
    record.gt_class_id       = gt ? gt->class_id : -1;
    record.gt_class          = gt ? gt->class_name : QString();
    record.gt_geometry       = gt ? gt->geometry : QVariantMap{};
    record.gt_bounds         = record.gt_geometry.value(evaluation::fieldName(evaluation::Field::Bounds)).toMap();
    record.pred_instance_id  = pred ? pred->prediction_id : QString();
    record.pred_class_id     = pred ? pred->class_id : -1;
    record.pred_class        = pred ? pred->class_name : QString();
    record.pred_geometry     = pred ? pred->geometry : QVariantMap{};
    record.pred_bounds       = record.pred_geometry.value(evaluation::fieldName(evaluation::Field::Bounds)).toMap();
    record.gt_mask_url       = maskUrl(record.gt_geometry, dataset_root);
    record.pred_mask_url     = maskUrl(record.pred_geometry, prediction_root);
    return record;
}

QVariantMap buildInstanceEvent(const EvaluationImageData &image, const evaluation::Status status,
                               const EvaluationGroundTruthData *gt, const EvaluationPredictionData *pred,
                               const double iou, const QString &dataset_root, const QString &prediction_root,
                               const qint64 event_index)
{
    const EvaluationInstanceRecord record
        = buildInstanceRecord(image, status, gt, pred, iou, dataset_root, prediction_root, event_index);
    return QVariantMap{
        {evaluation::fieldName(evaluation::Field::EventUuid), record.event_uuid},
        {evaluation::fieldName(evaluation::Field::ImageId), record.image_id},
        {evaluation::fieldName(evaluation::Field::Status), evaluation::statusKey(record.status)},
        {evaluation::fieldName(evaluation::Field::Score), record.score},
        {evaluation::fieldName(evaluation::Field::Iou), record.iou},
        {evaluation::fieldName(evaluation::Field::GtLabelId), record.gt_label_id},
        {evaluation::fieldName(evaluation::Field::GtClassId), record.gt_class_id},
        {evaluation::fieldName(evaluation::Field::GtClassName), record.gt_class},
        {evaluation::fieldName(evaluation::Field::GtGeometry), record.gt_geometry},
        {evaluation::fieldName(evaluation::Field::PredInstanceId), record.pred_instance_id},
        {evaluation::fieldName(evaluation::Field::PredClassId), record.pred_class_id},
        {evaluation::fieldName(evaluation::Field::PredClassName), record.pred_class},
        {evaluation::fieldName(evaluation::Field::PredGeometry), record.pred_geometry},
        {evaluation::fieldName(evaluation::Field::GtMaskUrl), record.gt_mask_url},
        {evaluation::fieldName(evaluation::Field::PredMaskUrl), record.pred_mask_url}
    };
}

QVariantList evaluationConfusionCells(const QMap<int, QString> &classes, const QMap<QString, qint64> &matrix,
                                      const qint64 total_count)
{
    const QString     matrix_fn           = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::FalseNegative);
    const QString     matrix_fp           = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::FalsePositive);
    const QString     matrix_unmatched_fn = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::UnmatchedGroundTruth);
    const QString     matrix_unmatched_fp = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::UnmatchedPrediction);
    const QString     matrix_total        = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::Total);
    QMap<int, qint64> pred_totals;
    QMap<int, qint64> gt_totals;
    QMap<int, qint64> row_fp_totals;
    QMap<int, qint64> column_fn_totals;
    QMap<int, qint64> unmatched_pred_totals;
    QMap<int, qint64> unmatched_gt_totals;
    qint64            unmatched_fp   = 0;
    qint64            unmatched_fn   = 0;
    qint64            mismatch_total = 0;
    for (auto it = matrix.cbegin(); it != matrix.cend(); ++it)
    {
        const QList<QString> keys = it.key().split(QLatin1Char('\x1f'));
        if (keys.size() != 2)
            continue;
        const QString &row_key    = keys.at(0);
        const QString &column_key = keys.at(1);
        const qint64   count      = it.value();
        const bool     row_fn     = row_key == matrix_fn;
        const bool     column_fp  = column_key == matrix_fp;
        if (row_fn)
        {
            unmatched_fn += it.value();
            if (!column_fp)
            {
                unmatched_gt_totals[column_key.toInt()] += count;
                column_fn_totals[column_key.toInt()] += count;
            }
        }
        else
            pred_totals[row_key.toInt()] += count;
        if (column_fp)
        {
            unmatched_fp += it.value();
            if (!row_fn)
            {
                unmatched_pred_totals[row_key.toInt()] += count;
                row_fp_totals[row_key.toInt()] += count;
            }
        }
        else
            gt_totals[column_key.toInt()] += count;
        if (!row_fn && !column_fp && row_key != column_key)
        {
            row_fp_totals[row_key.toInt()] += count;
            column_fn_totals[column_key.toInt()] += count;
            mismatch_total += count;
        }
    }

    QVariantList cells;
    const auto   appendCell = [&](const QString &row, const QString &column, qint64 count,
                                  const evaluation::CellKind kind, bool selectable, bool diagonal, bool error)
    {
        const bool    row_fn              = row == matrix_fn;
        const bool    row_unmatched_fn    = row == matrix_unmatched_fn;
        const bool    row_total           = row == matrix_total;
        const bool    column_fp           = column == matrix_fp;
        const bool    column_unmatched_fp = column == matrix_unmatched_fp;
        const bool    column_total        = column == matrix_total;
        const int     row_id              = row_fn || row_unmatched_fn || row_total ? -1 : row.toInt();
        const int     column_id           = column_fp || column_unmatched_fp || column_total ? -1 : column.toInt();
        const QString total_label         = evaluation::displayText(evaluation::DisplayText::Total);
        const QString row_label
            = row_fn ? evaluation::matrixAxisLabel(evaluation::MatrixAxisKey::FalseNegative)
                     : (row_unmatched_fn ? evaluation::matrixAxisLabel(evaluation::MatrixAxisKey::UnmatchedGroundTruth)
                                         : (row_total ? total_label : classes.value(row_id)));
        const QString column_label
            = column_fp
                ? evaluation::matrixAxisLabel(evaluation::MatrixAxisKey::FalsePositive)
                : (column_unmatched_fp ? evaluation::matrixAxisLabel(evaluation::MatrixAxisKey::UnmatchedPrediction)
                                       : (column_total ? total_label : classes.value(column_id)));
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
        appendCell(row, matrix_unmatched_fp, unmatched_pred_totals.value(row_it.key()),
                   evaluation::CellKind::FalsePositive, true, false, true);
        appendCell(row, matrix_fp, row_fp_totals.value(row_it.key()), evaluation::CellKind::FalsePositive, true, false,
                   true);
        appendCell(row, matrix_total, pred_totals.value(row_it.key()), evaluation::CellKind::PredTotal, true, false,
                   false);
    }
    for (auto column_it = classes.cbegin(); column_it != classes.cend(); ++column_it)
    {
        const QString column = QString::number(column_it.key());
        appendCell(matrix_unmatched_fn, column, unmatched_gt_totals.value(column_it.key()),
                   evaluation::CellKind::FalseNegative, true, false, true);
    }
    appendCell(matrix_unmatched_fn, matrix_unmatched_fp, 0, evaluation::CellKind::NotApplicable, true, false, false);
    appendCell(matrix_unmatched_fn, matrix_fp, unmatched_fn, evaluation::CellKind::FalseNegative, true, false, true);
    appendCell(matrix_unmatched_fn, matrix_total, unmatched_fn, evaluation::CellKind::FalseNegativeTotal, true, false,
               true);
    for (auto column_it = classes.cbegin(); column_it != classes.cend(); ++column_it)
    {
        const QString column = QString::number(column_it.key());
        appendCell(matrix_fn, column, column_fn_totals.value(column_it.key()), evaluation::CellKind::FalseNegative,
                   true, false, true);
    }
    appendCell(matrix_fn, matrix_unmatched_fp, unmatched_fp, evaluation::CellKind::FalsePositive, true, false, true);
    appendCell(matrix_fn, matrix_fp, mismatch_total + unmatched_fp + unmatched_fn, evaluation::CellKind::NotApplicable,
               true, false, false);
    appendCell(matrix_fn, matrix_total, mismatch_total + unmatched_fn, evaluation::CellKind::FalseNegativeTotal, true,
               false, true);
    for (auto column_it = classes.cbegin(); column_it != classes.cend(); ++column_it)
    {
        const QString column = QString::number(column_it.key());
        appendCell(matrix_total, column, gt_totals.value(column_it.key()), evaluation::CellKind::GtTotal, true, false,
                   false);
    }
    appendCell(matrix_total, matrix_unmatched_fp, unmatched_fp, evaluation::CellKind::FalsePositiveTotal, true, false,
               true);
    appendCell(matrix_total, matrix_fp, mismatch_total + unmatched_fp, evaluation::CellKind::FalsePositiveTotal, true,
               false, true);
    appendCell(matrix_total, matrix_total, total_count, evaluation::CellKind::All, true, false, false);
    return cells;
}

} // namespace dltool::model
