#include "model/EvaluationMatching.h"

#include "model/EvaluationGeometry.h"

#include <QVector>
#include <algorithm>
#include <limits>
#include <numeric>

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

} // namespace

QList<MatchPair> greedyIoUMatches(const int pred_count, const int gt_count,
                                  const std::function<double(int, int)> &iou_fn, const double threshold,
                                  const std::shared_ptr<std::atomic_bool> &cancel)
{
    struct Candidate
    {
        int    prediction{-1};
        int    ground_truth{-1};
        double iou{0.0};
    };

    QList<Candidate> candidates;
    for (int prediction = 0; prediction < pred_count; ++prediction)
    {
        if (isCancelled(cancel))
            return {};
        for (int gt = 0; gt < gt_count; ++gt)
        {
            const double iou = iou_fn(prediction, gt);
            if (iou >= threshold)
                candidates.push_back({prediction, gt, iou});
        }
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate &lhs, const Candidate &rhs)
              {
                  if (lhs.iou != rhs.iou)
                      return lhs.iou > rhs.iou;
                  if (lhs.prediction != rhs.prediction)
                      return lhs.prediction < rhs.prediction;
                  return lhs.ground_truth < rhs.ground_truth;
              });

    QVector<bool>    used_prediction(pred_count, false);
    QVector<bool>    used_ground_truth(gt_count, false);
    QList<MatchPair> result;
    for (const Candidate &candidate : candidates)
    {
        if (used_prediction.at(candidate.prediction) || used_ground_truth.at(candidate.ground_truth))
            continue;
        used_prediction[candidate.prediction]     = true;
        used_ground_truth[candidate.ground_truth] = true;
        result.push_back({candidate.prediction, candidate.ground_truth, candidate.iou});
    }
    return result;
}

QList<MatchPair> hungarianIoUMatches(const int pred_count, const int gt_count,
                                     const std::function<double(int, int)> &iou_fn, const double threshold,
                                     const std::shared_ptr<std::atomic_bool> &cancel)
{
    const int size = std::max(pred_count, gt_count);
    if (size <= 0)
        return {};

    QVector<QVector<double>> weight(size, QVector<double>(size, 0.0));
    for (int prediction = 0; prediction < pred_count; ++prediction)
    {
        if (isCancelled(cancel))
            return {};
        for (int gt = 0; gt < gt_count; ++gt)
        {
            const double iou = iou_fn(prediction, gt);
            if (iou >= threshold)
                weight[prediction][gt] = iou;
        }
    }

    // Hungarian 最小费用算法。
    // 费用取负 IoU，使总 IoU 最大；方阵补齐后的 dummy 行/列权值为零，低于阈值的分配最终会被剔除。
    const int       n = size;
    QVector<double> u(n + 1), v(n + 1);
    QVector<int>    p(n + 1), way(n + 1);
    for (int row = 1; row <= n; ++row)
    {
        if (isCancelled(cancel))
            return {};
        p[0]                    = row;
        int             column0 = 0;
        QVector<double> minv(n + 1, std::numeric_limits<double>::max());
        QVector<bool>   used(n + 1, false);
        do
        {
            used[column0]     = true;
            const int row0    = p[column0];
            double    delta   = std::numeric_limits<double>::max();
            int       column1 = 0;
            for (int column = 1; column <= n; ++column)
            {
                if (isCancelled(cancel))
                    return {};
                if (used[column])
                    continue;
                const double current = -weight[row0 - 1][column - 1] - u[row0] - v[column];
                if (current < minv[column])
                {
                    minv[column] = current;
                    way[column]  = column0;
                }
                if (minv[column] < delta)
                {
                    delta   = minv[column];
                    column1 = column;
                }
            }
            for (int column = 0; column <= n; ++column)
            {
                if (used[column])
                {
                    u[p[column]] += delta;
                    v[column] -= delta;
                }
                else
                {
                    minv[column] -= delta;
                }
            }
            column0 = column1;
        }
        while (p[column0] != 0);
        do
        {
            const int column1 = way[column0];
            p[column0]        = p[column1];
            column0           = column1;
        }
        while (column0 != 0);
    }

    QList<MatchPair> result;
    for (int column = 1; column <= n; ++column)
    {
        if (isCancelled(cancel))
            return {};
        const int prediction = p[column] - 1;
        const int gt         = column - 1;
        if (prediction < 0 || prediction >= pred_count || gt < 0 || gt >= gt_count)
            continue;
        const double iou = weight[prediction][gt];
        if (iou >= threshold)
            result.push_back({prediction, gt, iou});
    }
    std::sort(result.begin(), result.end(),
              [](const MatchPair &lhs, const MatchPair &rhs) { return lhs.prediction < rhs.prediction; });
    return result;
}

IncrementalHungarianMatcher::IncrementalHungarianMatcher(const int prediction_count, const int ground_truth_count,
                                                         const QVector<QVector<double>> &ious, const double threshold)
    : prediction_count_(std::max(0, prediction_count))
    , ground_truth_count_(std::max(0, ground_truth_count))
    , column_count_(std::max(prediction_count_, ground_truth_count_))
    , ious_(&ious)
    , threshold_(threshold)
    , u_(prediction_count_ + 1, 0.0)
    , v_(column_count_ + 1, 0.0)
    , p_(column_count_ + 1, 0)
    , way_(column_count_ + 1, 0)
{
}

void IncrementalHungarianMatcher::reset()
{
    row_predictions_.clear();
    std::fill(u_.begin(), u_.end(), 0.0);
    std::fill(v_.begin(), v_.end(), 0.0);
    std::fill(p_.begin(), p_.end(), 0);
    std::fill(way_.begin(), way_.end(), 0);
}

double IncrementalHungarianMatcher::weight(const int prediction_index, const int column) const
{
    if (ious_ == nullptr || prediction_index < 0 || prediction_index >= prediction_count_ || column <= 0
        || column > ground_truth_count_)
        return 0.0;

    const QVector<double> &prediction_ious = ious_->at(prediction_index);
    if (column - 1 < 0 || column - 1 >= prediction_ious.size())
        return 0.0;
    const double iou = prediction_ious.at(column - 1);
    return iou >= threshold_ ? iou : 0.0;
}

bool IncrementalHungarianMatcher::addPrediction(const int                                prediction_index,
                                                const std::shared_ptr<std::atomic_bool> &cancel)
{
    if (prediction_index < 0 || prediction_index >= prediction_count_)
        return false;
    if (row_predictions_.contains(prediction_index))
        return true;
    if (row_predictions_.size() >= prediction_count_ || column_count_ <= 0)
        return false;

    row_predictions_.push_back(prediction_index);
    const int row           = row_predictions_.size();
    p_[0]                   = row;
    int             column0 = 0;
    QVector<double> minv(column_count_ + 1, std::numeric_limits<double>::max());
    QVector<bool>   used(column_count_ + 1, false);

    do
    {
        if (isCancelled(cancel))
            return false;
        used[column0]     = true;
        const int row0    = p_.at(column0);
        double    delta   = std::numeric_limits<double>::max();
        int       column1 = 0;
        for (int column = 1; column <= column_count_; ++column)
        {
            if (isCancelled(cancel))
                return false;
            if (used.at(column))
                continue;
            const double current = -weight(row_predictions_.at(row0 - 1), column) - u_.at(row0) - v_.at(column);
            if (current < minv.at(column))
            {
                minv[column] = current;
                way_[column] = column0;
            }
            if (minv.at(column) < delta)
            {
                delta   = minv.at(column);
                column1 = column;
            }
        }
        for (int column = 0; column <= column_count_; ++column)
        {
            if (used.at(column))
            {
                u_[p_.at(column)] += delta;
                v_[column] -= delta;
            }
            else
                minv[column] -= delta;
        }
        column0 = column1;
    }
    while (p_.at(column0) != 0);

    do
    {
        const int column1 = way_.at(column0);
        p_[column0]       = p_.at(column1);
        column0           = column1;
    }
    while (column0 != 0);
    return true;
}

QList<MatchPair> IncrementalHungarianMatcher::matches(const std::shared_ptr<std::atomic_bool> &cancel) const
{
    QList<MatchPair> result;
    for (int column = 1; column <= ground_truth_count_; ++column)
    {
        if (isCancelled(cancel))
            return {};
        const int row = p_.at(column) - 1;
        if (row < 0 || row >= row_predictions_.size())
            continue;
        const int    prediction = row_predictions_.at(row);
        const double iou        = ious_ != nullptr && prediction >= 0 && prediction < ious_->size()
                                       && column - 1 < ious_->at(prediction).size()
                                    ? ious_->at(prediction).at(column - 1)
                                    : 0.0;
        if (iou >= threshold_)
            result.push_back({prediction, column - 1, iou});
    }
    std::sort(result.begin(), result.end(),
              [](const MatchPair &lhs, const MatchPair &rhs)
              {
                  if (lhs.prediction != rhs.prediction)
                      return lhs.prediction < rhs.prediction;
                  return lhs.ground_truth < rhs.ground_truth;
              });
    return result;
}

QList<MatchPair> matchPredictions(const QList<EvaluationPredictionData>  &predictions,
                                  const QList<EvaluationGroundTruthData> &ground_truth, const double threshold,
                                  const evaluation::MatchingStrategy       strategy,
                                  const std::shared_ptr<std::atomic_bool> &cancel)
{
    // 两框均无效时视为完全匹配，IoU 取 1。
    const auto iou_fn = [&predictions, &ground_truth](const int prediction, const int gt)
    {
        return (!predictions.at(prediction).box.valid() && !ground_truth.at(gt).box.valid())
                 ? 1.0
                 : intersectionOverUnion(predictions.at(prediction).box, ground_truth.at(gt).box);
    };
    if (strategy == evaluation::MatchingStrategy::HungarianIoU)
        return hungarianIoUMatches(predictions.size(), ground_truth.size(), iou_fn, threshold, cancel);
    return greedyIoUMatches(predictions.size(), ground_truth.size(), iou_fn, threshold, cancel);
}

} // namespace dltool::model
