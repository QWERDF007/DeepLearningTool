#pragma once

#include "dltool/model/Export.h"
#include "model/EvaluationData.h"

#include <QVector>
#include <QMap>
#include <QString>
#include <atomic>
#include <functional>
#include <memory>

namespace dltool::model {

/**
 * @brief 单个真实阈值切分点的计数与指标。
 */
struct MODEL_API EvaluationThresholdPoint
{
    double            threshold{0.0};
    EvaluationCounts  counts;
    double            precision{0.0};
    double            recall{0.0};
    double            f1{0.0};
    bool              precision_defined{false};
    bool              recall_defined{false};
    bool              f1_defined{false};
};

/**
 * @brief 从正式 TP/FP/FN 计数构造一个阈值工作点。
 *
 * 搜索、PR 曲线和协议展示共用该定义，分母为零时各指标按 0 处理。
 */
MODEL_API EvaluationThresholdPoint evaluationThresholdPoint(double threshold, const EvaluationCounts &counts);

/**
 * @brief 一次全量阈值搜索的内存结果。
 *
 * points 只在评估线程内部使用，结果协议只输出展示所需的最佳点，避免
 * 引入完整扫描结果的持久化格式。
 */
struct MODEL_API EvaluationThresholdSearchResult
{
    bool                       available{false};
    qint64                     positive_ground_truth_count{0};
    QVector<EvaluationThresholdPoint> points;
    QMap<int, QVector<EvaluationThresholdPoint>> class_points;
    EvaluationThresholdPoint   best_point;
    /**
     * @brief 与最大 F1 等价的候选阈值范围。
     *
     * best_point 仍按最高阈值规则选出；该范围保留所有具有相同最大 F1
     * 的真实候选点，供诊断和后续接口使用。
     */
    double                     equivalent_best_threshold_min{0.0};
    double                     equivalent_best_threshold_max{0.0};
};

using EvaluationThresholdCounter
    = std::function<bool(double threshold, EvaluationCounts &counts, QString *err_msg)>;

/**
 * @brief 从原始预测分数生成真实阈值切分点。
 *
 * 只保留有限、去重后的分数，并在浮点表示允许时追加一个严格高于最大
 * 有限分数的无预测端点。最大值已达到可表示上限时，最高分本身就是可用
 * 的最后一个有限工作点，不能再构造有限的更高阈值。
 */
MODEL_API QVector<double> evaluationThresholdCandidates(const QVector<double> &scores);

/**
 * @brief 遍历全部阈值切分点并选择最大 F1。
 *
 * 调用方的 counter 必须按正式评估规则，以 score >= threshold 计算计数。
 * F1 相同时选择数值最高的阈值；正类 GT 存在但最大 F1 为 0 仍然是有效结果。
 */
MODEL_API EvaluationThresholdSearchResult searchBestEvaluationThreshold(
    const QVector<double> &scores, qint64 positive_ground_truth_count, const EvaluationThresholdCounter &counter,
    const std::shared_ptr<std::atomic_bool> &cancel = {}, QString *err_msg = nullptr);

/**
 * @brief 从已计算的阈值工作点中选择最大 F1。
 *
 * 专用搜索器已经在一次扫描中生成所有工作点时使用此函数，避免再次
 * 生成候选阈值并调用计数器。
 */
MODEL_API EvaluationThresholdSearchResult selectBestEvaluationThreshold(
    const QVector<EvaluationThresholdPoint> &points, qint64 positive_ground_truth_count);

} // namespace dltool::model
