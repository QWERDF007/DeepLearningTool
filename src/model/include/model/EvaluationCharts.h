#pragma once

#include "dltool/model/Export.h"
#include "model/EvaluationData.h"
#include "model/ModelEvaluationModels.h"
#include "model/ModelEvaluationProtocol.h"

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <atomic>
#include <memory>

namespace dltool::model {

/**
 * @brief 官方评估输出：指标、图表与图像级定义。
 */
struct MODEL_API EvaluationChartOutput
{
    bool         available{false}; ///< 是否生成了官方指标。
    QVariantMap  metrics;          ///< 官方指标映射。
    QVariantList charts;           ///< 图表描述符列表。
    QStringList  chart_kinds;      ///< 图表类型列表。
    QVariantMap  image_definition; ///< 图像级指标定义。
};

/**
 * @brief 评估计数（真正例/假正例/假负例），供主链路与结果组装共用。
 */
struct MODEL_API EvaluationCounts
{
    qint64 tp{0};
    qint64 fp{0};
    qint64 fn{0};
};

/**
 * @brief 由图像记录构造异常分数分布图描述符。
 *
 * GOOD 图像取 max_prediction_score 为 GOOD 样本，Anomaly 图像取
 * max_prediction_score 为 Anomaly 样本；图表由公共直方图构造实现生成。
 * @param images 图像记录列表。
 * @return 图表描述符。
 */
MODEL_API QVariantMap anomalyScoreChartForImages(const QList<EvaluationImageRecord> &images);

/**
 * @brief 序列化单条实例事件记录。
 *
 * 计算裁剪视口、归一化叠加边界/点列与 mask URL，输出评估协议事件映射。
 * @param image 所属图像记录。
 * @param status 事件状态。
 * @param gt GT 记录，可为 nullptr（FP 事件）。
 * @param pred 预测记录，可为 nullptr（FN 事件）。
 * @param iou 匹配 IoU。
 * @param dataset_root GT mask 的解析根目录。
 * @param prediction_root 预测 mask 的解析根目录。
 * @param event_index 事件序号（1-based），用于生成事件 UUID 后缀。
 * @return 事件映射。
 */
MODEL_API QVariantMap buildInstanceEvent(const EvaluationImageData &image, evaluation::Status status,
                                         const EvaluationGroundTruthData *gt, const EvaluationPredictionData *pred,
                                         double iou, const QString &dataset_root, const QString &prediction_root,
                                         qint64 event_index);

/**
 * @brief 构造混淆矩阵单元格列表。
 *
 * 布局为 类别 x 类别 + FN/FP/合计 行列，与主链路矩阵键（行\x1f列）对应。
 * @param classes 类别目录。
 * @param matrix 矩阵计数（行\x1f列 -> 计数）。
 * @param total_count 全部单元格计数。
 * @param anomaly_method 是否为异常检测（无类别错误语义）。
 * @return 单元格映射列表。
 */
MODEL_API QVariantList evaluationConfusionCells(const QMap<int, QString> &classes, const QMap<QString, qint64> &matrix,
                                                qint64 total_count, bool anomaly_method);

/**
 * @brief 组装完整评估结果映射。
 *
 * 主链路 evaluate 完成计数与事件收集后，由本函数完成结果序列化：图像记录、
 * 按类别指标、类别目录、混淆矩阵、官方指标/图表与能力声明。
 * @param images 全部图像记录。
 * @param classes 类别目录。
 * @param per_class 按类别计数。
 * @param overall 实例级总体计数。
 * @param image_counts 图像级计数。
 * @param matrix 混淆矩阵计数。
 * @param event_records 实例事件列表。
 * @param prediction_count 预测总数。
 * @param method 评估方法。
 * @param confidence_threshold 置信度阈值。
 * @param iou_threshold IoU 阈值。
 * @param matching_strategy 匹配策略。
 * @param evaluation_config 规范化评估配置。
 * @param cancel 协作取消令牌，可为空。
 * @param err_msg 取消/失败时输出错误信息，可为 nullptr。
 * @return 评估结果映射；取消或失败时返回空映射。
 */
MODEL_API QVariantMap assembleEvaluationResult(
    const QMap<qint64, EvaluationImageData> &images, const QMap<int, QString> &classes,
    const QMap<int, EvaluationCounts> &per_class, const EvaluationCounts &overall, const EvaluationCounts &image_counts,
    const QMap<QString, qint64> &matrix, const QVariantList &event_records, int prediction_count,
    evaluation::Method method, double confidence_threshold, double iou_threshold,
    evaluation::MatchingStrategy matching_strategy, const QVariantMap &evaluation_config,
    const std::shared_ptr<std::atomic_bool> &cancel = {}, QString *err_msg = nullptr);

/**
 * @brief 由 TP/FP/FN 构造评估协议指标映射。
 * @param tp 真正例数。
 * @param fp 假正例数。
 * @param fn 假负例数。
 * @return 指标映射（precision/recall/f1 及定义标记）。
 */
MODEL_API QVariantMap evaluationMetricMap(qint64 tp, qint64 fp, qint64 fn);

/**
 * @brief 构建官方评估输出（指标与图表）。
 *
 * 检测方法生成 confidence-IoU 工作点的实例指标与 PR 曲线；异常检测方法
 * 生成 score-above-threshold 的图像级二元指标。
 * @param method 评估方法。
 * @param images 全部图像记录。
 * @param confidence 置信度阈值。
 * @param iou_threshold IoU 阈值。
 * @param strategy 匹配策略。
 * @param diagnostic 诊断指标（实例/图像分项）。
 * @param cancel 协作取消令牌，可为空。
 * @return 官方评估输出。
 */
MODEL_API EvaluationChartOutput buildEvaluationCharts(evaluation::Method                       method,
                                                      const QMap<qint64, EvaluationImageData> &images,
                                                      double confidence, double iou_threshold,
                                                      evaluation::MatchingStrategy             strategy,
                                                      const QVariantMap                       &diagnostic,
                                                      const std::shared_ptr<std::atomic_bool> &cancel = {});

/**
 * @brief 构造按类别指标柱状图描述符。
 * @param labels 类别名称标签。
 * @param precision 各类别 Precision 值。
 * @param recall 各类别 Recall 值。
 * @param f1 各类别 F1 值。
 * @return 图表描述符。
 */
MODEL_API QVariantMap perClassMetricsChart(const QVariantList &labels, const QVariantList &precision,
                                           const QVariantList &recall, const QVariantList &f1);

} // namespace dltool::model
