#pragma once

#include "dltool/model/Export.h"
#include "model/EvaluationData.h"
#include "model/ModelEvaluationProtocol.h"

#include <QList>
#include <QVector>
#include <atomic>
#include <functional>
#include <memory>

namespace dltool::model {

/**
 * @brief 一对已匹配的预测框与真值框。
 *
 * prediction 与 ground_truth 分别是预测、真值列表中的下标（0-based），
 * iou 为本次匹配的 IoU 值（无效框对记为 1.0）。
 */
struct MODEL_API MatchPair
{
    int    prediction{-1};   ///< 预测框下标。
    int    ground_truth{-1}; ///< 真值框下标。
    double iou{0.0};         ///< 匹配 IoU 值。
};

/**
 * @brief 贪心 IoU 一对一匹配。
 *
 * 按 IoU 从大到小（同值按下标升序）依次配对，每个预测/真值至多匹配一次；
 * 低于阈值的候选对直接丢弃，未匹配的预测/真值保留为 FP/FN。
 * @param pred_count 预测框数量。
 * @param gt_count 真值框数量。
 * @param iou_fn 调用方注入的 IoU 计算函数（i, j），Service 传 Box 几何，ViewModel 传 QVariantMap 几何。
 * @param threshold IoU 匹配阈值。
 * @param cancel 协作取消令牌，置位后提前返回空列表；可为空。
 * @return 按预测下标升序的匹配对列表。
 */
MODEL_API QList<MatchPair> greedyIoUMatches(int pred_count, int gt_count, const std::function<double(int, int)> &iou_fn,
                                            double threshold, const std::shared_ptr<std::atomic_bool> &cancel = {});

/**
 * @brief 匈牙利算法最大权一对一匹配。
 *
 * 以零权 dummy 边补齐方阵后运行 Hungarian 最小费用算法；无效边权为零，
 * 最终只接受达到 IoU 阈值的分配，因此未匹配的预测/GT 会保留为 FP/FN。
 * 该实现不依赖第三方矩阵库，适用于评估阶段的纯值记录。
 * @param pred_count 预测框数量。
 * @param gt_count 真值框数量。
 * @param iou_fn 调用方注入的 IoU 计算函数（i, j）。
 * @param threshold IoU 匹配阈值。
 * @param cancel 协作取消令牌，置位后提前返回空列表；可为空。
 * @return 按预测下标升序的匹配对列表。
 */
MODEL_API QList<MatchPair> hungarianIoUMatches(int pred_count, int gt_count,
                                               const std::function<double(int, int)> &iou_fn, double threshold,
                                               const std::shared_ptr<std::atomic_bool> &cancel = {});

/**
 * @brief 按预测激活顺序增量维护 Hungarian 最大权匹配。
 *
 * 阈值曲线中的预测只会按置信度从高到低逐步加入。该匹配器固定最终
 * 方阵的列数，每加入一行只执行一次增广路径，而不是对每个阈值从头
 * 求解完整方阵。IoU 矩阵和阈值保持不变，因此每个前缀仍得到该前缀
 * 的最大 IoU 总和匹配；未达到阈值的边权为零，输出时被过滤。
 */
class MODEL_API IncrementalHungarianMatcher
{
public:
    IncrementalHungarianMatcher(int prediction_count, int ground_truth_count, const QVector<QVector<double>> &ious,
                                double threshold);

    void             reset();
    bool             addPrediction(int prediction_index, const std::shared_ptr<std::atomic_bool> &cancel = {});
    QList<MatchPair> matches(const std::shared_ptr<std::atomic_bool> &cancel = {}) const;

private:
    double weight(int prediction_index, int column) const;

    int                             prediction_count_{0};
    int                             ground_truth_count_{0};
    int                             column_count_{0};
    const QVector<QVector<double>> *ious_{nullptr};
    double                          threshold_{0.0};
    QVector<int>                    row_predictions_;
    QVector<double>                 u_;
    QVector<double>                 v_;
    QVector<int>                    p_;
    QVector<int>                    way_;
};

/**
 * @brief 按策略匹配预测与真值记录。
 *
 * IoU 计算注入规则：两框均无效（无框的 GT/预测）视为完全匹配（IoU=1）。
 * @param predictions 预测列表。
 * @param ground_truth 真值列表。
 * @param threshold IoU 匹配阈值。
 * @param strategy 匹配策略（贪心/Hungarian）。
 * @param cancel 协作取消令牌，可为空。
 * @return 匹配对列表。
 */
MODEL_API QList<MatchPair> matchPredictions(const QList<EvaluationPredictionData>  &predictions,
                                            const QList<EvaluationGroundTruthData> &ground_truth, double threshold,
                                            evaluation::MatchingStrategy             strategy,
                                            const std::shared_ptr<std::atomic_bool> &cancel = {});

} // namespace dltool::model
