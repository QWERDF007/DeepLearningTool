#include "model/EvaluationCharts.h"

#include "model/EvaluationAnomalyConfusion.h"
#include "model/EvaluationCommon.h"
#include "model/EvaluationGeometry.h"
#include "model/EvaluationMatching.h"
#include "model/ModelEvaluationProtocol.h"

#include <QSet>
#include <QVariantList>
#include <QVector>
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
 * @return 直方图数据。
 */
ScoreHistogramData scoreHistogram(const QVariantList &good_scores, const QVariantList &anomaly_scores,
                                  const double classification_threshold)
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
                              const double classification_threshold)
{
    constexpr const char *good_color         = "#43A047";
    constexpr const char *good_fill          = "rgba(67, 160, 71, 0.24)";
    constexpr const char *anomaly_color      = "#E53935";
    constexpr const char *anomaly_fill       = "rgba(229, 57, 53, 0.24)";
    ScoreHistogramData    histogram          = scoreHistogram(good_scores, anomaly_scores, classification_threshold);
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

    QVariantList  datasets;
    const QString good_label    = evaluation::displayText(evaluation::DisplayText::Good);
    const QString anomaly_label = evaluation::displayText(evaluation::DisplayText::Anomaly);
    if (has_good)
        datasets.push_back(distributionDataset(good_label, evaluation::seriesKindKey(evaluation::SeriesKind::Good),
                                               QString::fromLatin1(good_color), QString::fromLatin1(good_fill),
                                               histogram.good_points));
    if (has_anomaly)
        datasets.push_back(distributionDataset(
            anomaly_label, evaluation::seriesKindKey(evaluation::SeriesKind::Anomaly),
            QString::fromLatin1(anomaly_color), QString::fromLatin1(anomaly_fill), histogram.anomaly_points));
    if (has_good)
        datasets.push_back(
            referenceDataset(QString("%1 最大分数：%2").arg(good_label).arg(QString::number(good_max, 'f', 4)),
                             QString::fromLatin1(good_color), good_max, histogram.max_count));
    if (has_anomaly)
        datasets.push_back(
            referenceDataset(QString("%1 最小分数：%2").arg(anomaly_label).arg(QString::number(anomaly_min, 'f', 4)),
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

QVariantMap anomalyScoreChartForSamples(const QList<AnomalyScoreSample> &samples, const double classification_threshold)
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
                             classification_threshold);
}

QVariantMap anomalyScoreChartForEvaluationImages(const QMap<qint64, EvaluationImageData> &images,
                                                 const double                             classification_threshold)
{
    QList<AnomalyScoreSample> samples;
    samples.reserve(images.size());
    for (const EvaluationImageData &image : images)
    {
        bool ground_truth_anomaly = false;
        for (const EvaluationGroundTruthData &ground_truth : image.gt)
            ground_truth_anomaly = ground_truth_anomaly || ground_truth.anomaly;

        double score     = 0.0;
        bool   has_score = false;
        for (const EvaluationPredictionData &prediction : image.predictions)
        {
            if (!has_score || prediction.score > score)
            {
                score     = prediction.score;
                has_score = true;
            }
        }
        samples.push_back({score, ground_truth_anomaly});
    }
    return anomalyScoreChartForSamples(samples, classification_threshold);
}

struct PrecisionRecallPrediction
{
    double score{0.0};
    bool   true_positive{false};
};

struct PrecisionRecallClassCurve
{
    int                              class_id{-1};
    QString                          class_name;
    int                              ground_truth_count{0};
    double                           average_precision{0.0};
    QVector<double>                  precision;
    QList<PrecisionRecallPrediction> predictions;
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
                                                        const std::shared_ptr<std::atomic_bool> &cancel)
{
    PrecisionRecallClassCurve curve;
    curve.class_id   = class_id;
    curve.class_name = class_name;

    for (const EvaluationImageData &image : images)
    {
        if (isCancelled(cancel))
            return {};

        QList<EvaluationPredictionData>  predictions;
        QList<EvaluationGroundTruthData> ground_truth;
        for (const EvaluationPredictionData &prediction : image.predictions)
        {
            if (prediction.class_id == class_id && std::isfinite(prediction.score))
                predictions.push_back(prediction);
        }
        for (const EvaluationGroundTruthData &value : image.gt)
        {
            if (value.class_id == class_id)
                ground_truth.push_back(value);
        }
        curve.ground_truth_count += ground_truth.size();

        std::stable_sort(predictions.begin(), predictions.end(),
                         [](const EvaluationPredictionData &lhs, const EvaluationPredictionData &rhs)
                         { return lhs.score > rhs.score; });
        const QList<MatchPair> matches = matchPredictions(predictions, ground_truth, iou_threshold, strategy, cancel);
        if (isCancelled(cancel))
            return {};

        QVector<bool> true_positives(predictions.size(), false);
        for (const MatchPair &match : matches)
        {
            if (match.prediction >= 0 && match.prediction < true_positives.size())
                true_positives[match.prediction] = true;
        }
        for (int index = 0; index < predictions.size(); ++index)
            curve.predictions.push_back({predictions.at(index).score, true_positives.at(index)});
    }

    std::stable_sort(curve.predictions.begin(), curve.predictions.end(),
                     [](const PrecisionRecallPrediction &lhs, const PrecisionRecallPrediction &rhs)
                     { return lhs.score > rhs.score; });

    const int point_count = kPrecisionRecallInterpolationPoints;
    curve.precision.fill(0.0, point_count);
    if (curve.ground_truth_count <= 0 || curve.predictions.isEmpty())
        return curve;

    QVector<double> recall;
    QVector<double> precision;
    recall.reserve(curve.predictions.size());
    precision.reserve(curve.predictions.size());
    int true_positive_count  = 0;
    int false_positive_count = 0;
    for (const PrecisionRecallPrediction &prediction : curve.predictions)
    {
        if (prediction.true_positive)
            ++true_positive_count;
        else
            ++false_positive_count;
        recall.push_back(static_cast<double>(true_positive_count) / curve.ground_truth_count);
        precision.push_back(static_cast<double>(true_positive_count) / (true_positive_count + false_positive_count));
    }

    QVector<double> modified_recall;
    QVector<double> envelope;
    modified_recall.reserve(recall.size() + 3);
    envelope.reserve(precision.size() + 3);
    modified_recall.push_back(0.0);
    envelope.push_back(1.0);
    for (int index = 0; index < recall.size(); ++index)
    {
        modified_recall.push_back(recall.at(index));
        envelope.push_back(precision.at(index));
    }
    modified_recall.push_back(recall.isEmpty() ? 1.0 : recall.back());
    modified_recall.push_back(1.0);
    envelope.push_back(0.0);
    envelope.push_back(0.0);
    for (int index = envelope.size() - 2; index >= 0; --index)
        envelope[index] = std::max(envelope.at(index), envelope.at(index + 1));

    const int denominator = std::max(1, point_count - 1);
    double    previous_x  = 0.0;
    double    previous_y  = interpolatePrecision(modified_recall, envelope, previous_x);
    for (int index = 0; index < point_count; ++index)
    {
        const double x         = static_cast<double>(index) / denominator;
        const double y         = std::clamp(interpolatePrecision(modified_recall, envelope, x), 0.0, 1.0);
        curve.precision[index] = y;
        if (index > 0)
            curve.average_precision += (x - previous_x) * (y + previous_y) * 0.5;
        previous_x = x;
        previous_y = y;
    }
    return curve;
}

QVariantMap precisionRecallChartFromCurves(const QList<PrecisionRecallClassCurve> &curves)
{
    QVariantList    datasets;
    QVector<double> average_precision(kPrecisionRecallInterpolationPoints, 0.0);
    double          mean_average_precision = 0.0;
    for (const PrecisionRecallClassCurve &curve : curves)
    {
        if (curve.precision.size() != average_precision.size())
            continue;
        for (int index = 0; index < average_precision.size(); ++index)
            average_precision[index] += curve.precision.at(index);
        mean_average_precision += curve.average_precision;
    }
    if (!curves.isEmpty())
    {
        for (double &value : average_precision) value /= curves.size();
        mean_average_precision /= curves.size();
    }

    QVariantList average_points;
    average_points.reserve(average_precision.size());
    const int denominator = std::max(1, kPrecisionRecallInterpolationPoints - 1);
    for (int index = 0; index < average_precision.size(); ++index)
        average_points.push_back(QVariantMap{
            {QStringLiteral("x"), static_cast<double>(index) / denominator},
            {QStringLiteral("y"),              average_precision.at(index)}
        });

    constexpr const char *average_color = "#2563EB";
    datasets.push_back(QVariantMap{
        {evaluation::fieldName(evaluation::Field::Label),
         QStringLiteral("平均 (mAP: %1)").arg(QString::number(mean_average_precision, 'f', 3))},
        {evaluation::fieldName(evaluation::Field::SeriesKind),
         evaluation::seriesKindKey(evaluation::SeriesKind::Average)},
        {evaluation::fieldName(evaluation::Field::ClassId), -1},
        {evaluation::fieldName(evaluation::Field::ClassName), QStringLiteral("平均")},
        {QStringLiteral("average_precision"), mean_average_precision},
        {evaluation::fieldName(evaluation::Field::Data), average_points},
        {QStringLiteral("borderColor"), QString::fromLatin1(average_color)},
        {QStringLiteral("backgroundColor"), QString::fromLatin1(average_color)},
        {QStringLiteral("pointBackgroundColor"), QString::fromLatin1(average_color)},
        {QStringLiteral("pointBorderColor"), QString::fromLatin1(average_color)},
        {QStringLiteral("borderWidth"), 3},
        {QStringLiteral("pointRadius"), 0},
        {QStringLiteral("pointHoverRadius"), 4},
        {QStringLiteral("lineTension"), 0},
        {QStringLiteral("fill"), false},
        {QStringLiteral("showLine"), true}
    });

    for (const PrecisionRecallClassCurve &curve : curves)
    {
        QVariantList points;
        points.reserve(curve.precision.size());
        for (int index = 0; index < curve.precision.size(); ++index)
            points.push_back(QVariantMap{
                {QStringLiteral("x"), static_cast<double>(index) / denominator},
                {QStringLiteral("y"),                curve.precision.at(index)}
            });
        const QString color = classColor(curve.class_id);
        datasets.push_back(QVariantMap{
            {evaluation::fieldName(evaluation::Field::Label),
             QStringLiteral("%1 (AP: %2)").arg(curve.class_name).arg(QString::number(curve.average_precision, 'f', 3))},
            {evaluation::fieldName(evaluation::Field::SeriesKind),
             evaluation::seriesKindKey(evaluation::SeriesKind::Class)},
            {evaluation::fieldName(evaluation::Field::ClassId), curve.class_id},
            {evaluation::fieldName(evaluation::Field::ClassName), curve.class_name},
            {QStringLiteral("average_precision"), curve.average_precision},
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

QVariantMap buildPrecisionRecallChart(const QList<EvaluationImageData> &images, const QMap<int, QString> &class_catalog,
                                      const double iou_threshold, const evaluation::MatchingStrategy strategy,
                                      const QVariantList &class_ids, const std::shared_ptr<std::atomic_bool> &cancel)
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
    }

    QList<PrecisionRecallClassCurve> curves;
    curves.reserve(target_classes.size());
    for (auto it = class_names.cbegin(); it != class_names.cend(); ++it)
    {
        if (!target_classes.contains(it.key()))
            continue;
        const QString name = it.value().isEmpty() ? QString::number(it.key()) : it.value();
        curves.push_back(makePrecisionRecallClassCurve(it.key(), name, images, iou_threshold, strategy, cancel));
        if (isCancelled(cancel))
            return {};
    }
    return precisionRecallChartFromCurves(curves);
}

} // namespace

QVariantMap anomalyScoreChartForImages(const QList<EvaluationImageData> &images, const double classification_threshold)
{
    QList<AnomalyScoreSample> samples;
    samples.reserve(images.size());
    for (const EvaluationImageData &image : images)
    {
        const double score = image.max_prediction_score;
        const bool   ground_truth_anomaly
            = std::any_of(image.gt.cbegin(), image.gt.cend(),
                          [](const EvaluationGroundTruthRecord &ground_truth) { return ground_truth.anomaly; });
        samples.push_back({score, ground_truth_anomaly});
    }
    return anomalyScoreChartForSamples(samples, classification_threshold);
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
                                          const std::shared_ptr<std::atomic_bool> &cancel)
{
    return buildPrecisionRecallChart(images, class_catalog, iou_threshold, strategy, class_ids, cancel);
}

EvaluationChartOutput buildAnomalyEvaluationCharts(const QMap<qint64, EvaluationImageData> &images,
                                                   const QVariantMap &diagnostic, const double confidence)
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
    output.charts.push_back(anomalyScoreChartForEvaluationImages(images, confidence));
    output.chart_kinds.push_back(evaluation::chartKindKey(evaluation::ChartKind::Line));
    return output;
}

EvaluationChartOutput buildInstanceMatchingEvaluationCharts(const QMap<qint64, EvaluationImageData> &images,
                                                            const double confidence, const double iou_threshold,
                                                            const evaluation::MatchingStrategy       strategy,
                                                            const QVariantMap                       &diagnostic,
                                                            const std::shared_ptr<std::atomic_bool> &cancel)
{
    EvaluationChartOutput output;
    if (isCancelled(cancel))
        return output;

    QList<EvaluationImageData> chart_images;
    chart_images.reserve(images.size());
    for (const EvaluationImageData &image : images) chart_images.push_back(image);

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
        = precisionRecallChartForImages(chart_images, class_catalog, iou_threshold, strategy, {}, cancel);
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
    output.chart_kinds.push_back(evaluation::chartKindKey(evaluation::ChartKind::Line));
    return output;
}

EvaluationChartOutput buildEvaluationCharts(const evaluation::Method                 method,
                                            const QMap<qint64, EvaluationImageData> &images, const double confidence,
                                            const double iou_threshold, const evaluation::MatchingStrategy strategy,
                                            const QVariantMap                       &diagnostic,
                                            const std::shared_ptr<std::atomic_bool> &cancel)
{
    // 兼容分发：协议组装仍按 method 选择图表构建器。
    // 引擎子类已直接调用 buildAnomalyEvaluationCharts /
    // buildInstanceMatchingEvaluationCharts；该函数只服务旧协议组装路径。
    if (evaluation::isAnomaly(method))
        return buildAnomalyEvaluationCharts(images, diagnostic, confidence);
    if (!evaluation::hasInstanceMetrics(method))
        return {};
    return buildInstanceMatchingEvaluationCharts(images, confidence, iou_threshold, strategy, diagnostic, cancel);
}

QVariantMap buildInstanceEvent(const EvaluationImageData &image, const evaluation::Status status,
                               const EvaluationGroundTruthData *gt, const EvaluationPredictionData *pred,
                               const double iou, const QString &dataset_root, const QString &prediction_root,
                               const qint64 event_index)
{
    // 视口裁剪由 thumbnail provider 在渲染时根据 URL 中的绝对 bounds 推导。
    // QML 按 LabelInstanceThumbnail 模式使用原始几何换算 overlay，评估线程因此不再依赖图像宽高。
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
        {evaluation::fieldName(evaluation::Field::GtMaskUrl), maskUrl(gt_geometry, dataset_root)},
        {evaluation::fieldName(evaluation::Field::PredMaskUrl), maskUrl(pred_geometry, prediction_root)}
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

namespace {

const EvaluationGroundTruthData *primaryGroundTruth(const EvaluationImageData &image)
{
    const EvaluationGroundTruthData *result = image.gt.isEmpty() ? nullptr : &image.gt.front();
    for (const EvaluationGroundTruthData &ground_truth : image.gt)
        if (ground_truth.label_id < 0)
            return &ground_truth;
    return result;
}

QVariantMap anomalyConfusionCellMap(const EvaluationConfusionCell &cell)
{
    return QVariantMap{
        {       evaluation::fieldName(evaluation::Field::RowKey),                            cell.row_key},
        {    evaluation::fieldName(evaluation::Field::ColumnKey),                         cell.column_key},
        {     evaluation::fieldName(evaluation::Field::RowLabel),                          cell.row_label},
        {  evaluation::fieldName(evaluation::Field::ColumnLabel),                       cell.column_label},
        {   evaluation::fieldName(evaluation::Field::RowClassId),                       cell.row_class_id},
        {evaluation::fieldName(evaluation::Field::ColumnClassId),                    cell.column_class_id},
        {        evaluation::fieldName(evaluation::Field::Count),                              cell.count},
        {     evaluation::fieldName(evaluation::Field::CellKind), evaluation::cellKindKey(cell.cell_kind)},
        {   evaluation::fieldName(evaluation::Field::Selectable),                         cell.selectable},
        {   evaluation::fieldName(evaluation::Field::IsDiagonal),                           cell.diagonal},
        {      evaluation::fieldName(evaluation::Field::IsError),                              cell.error}
    };
}

QVariantList anomalyConfusionVariantCells(const QMap<qint64, EvaluationImageData> &images, const double threshold,
                                          const QMap<int, QString> &class_catalog)
{
    QList<AnomalyConfusionSample> samples;
    samples.reserve(images.size());
    for (const EvaluationImageData &image : images)
    {
        const EvaluationGroundTruthData *ground_truth = primaryGroundTruth(image);
        const int category_id = ground_truth != nullptr && ground_truth->class_id >= 0 ? ground_truth->class_id : -1;
        const QString category_name    = ground_truth != nullptr ? ground_truth->class_name : QString{};
        const bool    category_anomaly = ground_truth != nullptr && ground_truth->anomaly;
        const bool    predicted_anomaly
            = std::any_of(image.predictions.cbegin(), image.predictions.cend(),
                          [threshold](const EvaluationPredictionData &prediction)
                          { return prediction.class_id == 1 && prediction.score >= threshold; });
        samples.push_back(AnomalyConfusionSample{category_id, category_name, category_anomaly, predicted_anomaly});
    }

    QVariantList                               cells;
    const std::vector<EvaluationConfusionCell> shared_cells = buildAnomalyConfusionCells(samples, class_catalog);
    for (const EvaluationConfusionCell &cell : shared_cells) cells.push_back(anomalyConfusionCellMap(cell));
    return cells;
}

} // namespace

QVariantMap assembleEvaluationResult(const EvaluationResultContext &context)
{
    const auto  &images               = context.images;
    const auto  &classes              = context.classes;
    const auto  &per_class            = context.per_class;
    const auto  &overall              = context.overall;
    const auto  &image_counts         = context.image_counts;
    const auto  &matrix               = context.matrix;
    const auto  &event_records        = context.event_records;
    const int    prediction_count     = context.prediction_count;
    const auto   method               = context.method;
    const double confidence_threshold = context.confidence_threshold;
    const double iou_threshold        = context.iou_threshold;
    const auto   matching_strategy    = context.matching_strategy;
    const auto  &evaluation_config    = context.evaluation_config;
    const auto  &cancel               = context.cancel;
    QString     *err_msg              = context.err_msg;
    const auto   cancelled            = [cancel, err_msg]()
    {
        if (err_msg)
            *err_msg = QString("评估已取消");
        return isCancelled(cancel);
    };
    const bool   anomaly_method = evaluation::isAnomaly(method);
    // 序列化图像记录及其 GT/预测实例列表。
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
                {evaluation::fieldName(evaluation::Field::IsAnomaly),    gt.anomaly},
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

    // 按类别指标数据。
    QVariantList per_class_metrics;
    for (auto it = classes.cbegin(); it != classes.cend(); ++it)
    {
        if (cancelled())
            return {};
        const EvaluationCounts counts = per_class.value(it.key());
        QVariantMap            metric = evaluationMetricMap(counts.tp, counts.fp, counts.fn);
        metric.insert(evaluation::fieldName(evaluation::Field::ClassId), it.key());
        metric.insert(evaluation::fieldName(evaluation::Field::ClassName), it.value());
        per_class_metrics.push_back(metric);
    }

    // 类别目录。
    QVariantList class_catalog;
    for (auto it = classes.cbegin(); it != classes.cend(); ++it)
    {
        if (cancelled())
            return {};
        const QString color = context.class_colors.value(it.key(), classColor(it.key()));
        class_catalog.push_back(QVariantMap{
            {   evaluation::fieldName(evaluation::Field::Id),   it.key()},
            { evaluation::fieldName(evaluation::Field::Name), it.value()},
            {evaluation::fieldName(evaluation::Field::Color),      color}
        });
    }

    const QVariantList matrix_cells = anomaly_method
                                        ? anomalyConfusionVariantCells(images, confidence_threshold, classes)
                                        : evaluationConfusionCells(classes, matrix, event_records.size());
    const QVariantMap  confusion_definition{
        { evaluation::fieldName(evaluation::Field::SampleUnit),
         anomaly_method ? QStringLiteral("image") : QStringLiteral("instance_event")   },
        {evaluation::fieldName(evaluation::Field::Aggregation), QStringLiteral("micro")}
    };

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
    for (const QVariant &chart : official.charts) charts.push_back(chart);

    return QVariantMap{
        {     evaluation::fieldName(evaluation::Field::PrimaryMetricSet),
         official.available ? evaluation::metricSetKey(evaluation::MetricSet::Official)
         : evaluation::metricSetKey(evaluation::MetricSet::Diagnostic)                                   },
        {     evaluation::fieldName(evaluation::Field::EvaluationConfig),
         evaluation::normalizedEvaluationConfig(evaluation_config)                                       },
        {         evaluation::fieldName(evaluation::Field::ClassCatalog),                   class_catalog},
        {    evaluation::fieldName(evaluation::Field::DiagnosticMetrics),                      diagnostic},
        {      evaluation::fieldName(evaluation::Field::OfficialMetrics),
         official.available ? official.metrics
         : QVariantMap{{evaluation::fieldName(evaluation::Field::Available), false}}                     },
        {evaluation::fieldName(evaluation::Field::ImageMetricDefinition),
         official.available && !official.image_definition.isEmpty()
         ? official.image_definition
         : QVariantMap{{evaluation::fieldName(evaluation::Field::SampleUnit), QStringLiteral("image_presence")},
         {evaluation::fieldName(evaluation::Field::Aggregation), QStringLiteral("micro")},
         {evaluation::fieldName(evaluation::Field::PositiveDefinition),
         QStringLiteral("gt_or_pred_class_present")},
         {evaluation::fieldName(evaluation::Field::HasImageMetrics), true}}                              },
        {         evaluation::fieldName(evaluation::Field::Capabilities),
         QVariantMap{{evaluation::fieldName(evaluation::Field::HasInstanceMetrics), capabilities.has_instance_metrics},
         {evaluation::fieldName(evaluation::Field::HasImageMetrics), capabilities.has_image_metrics},
         {evaluation::fieldName(evaluation::Field::HasConfusionMatrix), capabilities.has_confusion_matrix},
         {evaluation::fieldName(evaluation::Field::HasInstanceEvents), capabilities.has_instance_events},
         {evaluation::fieldName(evaluation::Field::ChartKinds), capabilities.chart_kinds}}               },
        {      evaluation::fieldName(evaluation::Field::ConfusionMatrix),
         QVariantMap{{evaluation::fieldName(evaluation::Field::Cells), matrix_cells},
         {evaluation::fieldName(evaluation::Field::SampleUnit),
         confusion_definition.value(evaluation::fieldName(evaluation::Field::SampleUnit))},
         {evaluation::fieldName(evaluation::Field::Aggregation),
         confusion_definition.value(evaluation::fieldName(evaluation::Field::Aggregation))}}             },
        {               evaluation::fieldName(evaluation::Field::Charts),                          charts},
        {         evaluation::fieldName(evaluation::Field::ImageRecords),                   image_records},
        {      evaluation::fieldName(evaluation::Field::InstanceRecords),                   event_records},
        {           evaluation::fieldName(evaluation::Field::ImageCount),                   images.size()},
        {      evaluation::fieldName(evaluation::Field::PredictionCount),                prediction_count},
        {           evaluation::fieldName(evaluation::Field::EventCount),            event_records.size()},
    };
}

} // namespace dltool::model
