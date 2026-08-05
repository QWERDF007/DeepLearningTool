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

    // Hungarian 最小费用算法：费用为取负的 IoU，返回的分配使总 IoU 最大。
    // 方阵补齐后 dummy 行/列权值为零，低于阈值的分配最终仍被剔除。
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

QList<MatchPair> matchPredictions(const QList<EvaluationPredictionData>  &predictions,
                                  const QList<EvaluationGroundTruthData> &ground_truth, const double threshold,
                                  const evaluation::MatchingStrategy       strategy,
                                  const std::shared_ptr<std::atomic_bool> &cancel)
{
    // IoU 计算注入：两框均无效（无框的 GT/预测）视为完全匹配（IoU=1）。
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
