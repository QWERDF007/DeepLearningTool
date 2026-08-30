#include "model/EvaluationThresholdSearch.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace dltool::model {

EvaluationThresholdPoint evaluationThresholdPoint(const double threshold, const EvaluationCounts &counts)
{
    EvaluationThresholdPoint point;
    point.threshold          = threshold;
    point.counts             = counts;
    point.precision_defined  = counts.tp + counts.fp > 0;
    point.recall_defined     = counts.tp + counts.fn > 0;
    point.precision          = point.precision_defined
                                  ? static_cast<double>(counts.tp) / static_cast<double>(counts.tp + counts.fp)
                                  : 0.0;
    point.recall             = point.recall_defined
                                ? static_cast<double>(counts.tp) / static_cast<double>(counts.tp + counts.fn)
                                : 0.0;
    point.f1_defined         = point.precision_defined && point.recall_defined
                        && point.precision + point.recall > 0.0;
    // Compute F1 directly from the counts. Apart from being the canonical
    // micro-F1 definition, this gives mathematically equal count ratios the
    // same rounded value even when their precision/recall paths round
    // differently.
    point.f1 = point.f1_defined
                 ? (2.0 * static_cast<double>(counts.tp))
                       / (2.0 * static_cast<double>(counts.tp) + static_cast<double>(counts.fp)
                          + static_cast<double>(counts.fn))
                 : 0.0;
    return point;
}

QVector<double> evaluationThresholdCandidates(const QVector<double> &scores)
{
    QVector<double> candidates;
    candidates.reserve(scores.size() + 1);
    for (const double score : scores)
    {
        if (std::isfinite(score))
            candidates.push_back(score);
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    if (!candidates.isEmpty())
    {
        const double no_prediction_threshold
            = std::nextafter(candidates.back(), std::numeric_limits<double>::infinity());
        if (std::isfinite(no_prediction_threshold) && no_prediction_threshold > candidates.back())
            candidates.push_back(no_prediction_threshold);
    }
    return candidates;
}

EvaluationThresholdSearchResult selectBestEvaluationThreshold(
    const QVector<EvaluationThresholdPoint> &points, const qint64 positive_ground_truth_count)
{
    EvaluationThresholdSearchResult result;
    result.positive_ground_truth_count = positive_ground_truth_count;
    result.points = points;
    if (positive_ground_truth_count <= 0)
        return result;

    for (const EvaluationThresholdPoint &point : points)
    {
        const bool                    new_best_f1
            = !result.available || point.f1 > result.best_point.f1;
        const bool                    equal_best_f1
            = result.available && point.f1 == result.best_point.f1;
        if (new_best_f1)
        {
            result.best_point = point;
            result.available  = true;
            result.equivalent_best_threshold_min = point.threshold;
            result.equivalent_best_threshold_max = point.threshold;
        }
        else if (equal_best_f1)
        {
            result.equivalent_best_threshold_min
                = std::min(result.equivalent_best_threshold_min, point.threshold);
            result.equivalent_best_threshold_max
                = std::max(result.equivalent_best_threshold_max, point.threshold);
            if (point.threshold > result.best_point.threshold)
                result.best_point = point;
        }
    }
    return result;
}

EvaluationThresholdSearchResult searchBestEvaluationThreshold(
    const QVector<double> &scores, const qint64 positive_ground_truth_count, const EvaluationThresholdCounter &counter,
    const std::shared_ptr<std::atomic_bool> &cancel, QString *err_msg)
{
    if (positive_ground_truth_count <= 0 || !counter)
        return selectBestEvaluationThreshold({}, positive_ground_truth_count);

    const QVector<double> candidates = evaluationThresholdCandidates(scores);
    if (candidates.isEmpty())
        return selectBestEvaluationThreshold({}, positive_ground_truth_count);

    QVector<EvaluationThresholdPoint> points;
    points.reserve(candidates.size());
    for (const double threshold : candidates)
    {
        if (cancel != nullptr && cancel->load(std::memory_order_relaxed))
        {
            if (err_msg != nullptr)
                *err_msg = QStringLiteral("评估已取消");
            return {};
        }

        EvaluationCounts counts;
        if (!counter(threshold, counts, err_msg))
        {
            if (err_msg != nullptr && err_msg->isEmpty())
                *err_msg = QStringLiteral("阈值计数失败");
            return {};
        }
        points.push_back(evaluationThresholdPoint(threshold, counts));
    }
    return selectBestEvaluationThreshold(points, positive_ground_truth_count);
}

} // namespace dltool::model
