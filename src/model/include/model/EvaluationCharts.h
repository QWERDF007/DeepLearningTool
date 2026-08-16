#pragma once

#include "dltool/model/Export.h"
#include "model/EvaluationData.h"
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
 * @brief Precision-Recall 曲线的固定插值点数。
 *
 * 该值对应 Ultralytics PR 曲线的均匀 Recall 网格；调整后会同时影响
 * 各类别曲线、平均曲线以及 AP 的数值积分。
 */
inline constexpr int kPrecisionRecallInterpolationPoints = 100;

/**
 * @brief 组装评估结果所需的上下文。
 *
 * 大型快照字段以引用传递，避免为整理参数额外复制评估数据；上下文只在
 * assembleEvaluationResult 调用期间有效。
 */
struct MODEL_API EvaluationResultContext
{
    const QMap<qint64, EvaluationImageData> &images;
    const QMap<int, QString>                &classes;
    const QMap<int, EvaluationCounts>      &per_class;
    const EvaluationCounts                 &overall;
    const EvaluationCounts                 &image_counts;
    const QMap<QString, qint64>             &matrix;
    const QVariantList                      &event_records;
    int                                      prediction_count{0};
    evaluation::Method                       method{evaluation::Method::Unknown};
    double                                   confidence_threshold{evaluation::kDefaultConfidenceThreshold};
    double                                   iou_threshold{evaluation::kDefaultIouThreshold};
    evaluation::MatchingStrategy             matching_strategy{evaluation::MatchingStrategy::GreedyIoU};
    const QVariantMap                       &evaluation_config;
    std::shared_ptr<std::atomic_bool>        cancel;
    QString                                 *err_msg{nullptr};
};

/**
 * @brief 由图像记录构造异常分数分布图描述符。
 *
 * 正常图像取 max_prediction_score 作为正常样本，异常图像取
 * max_prediction_score 作为异常样本；图表由公共直方图构造实现生成。
 * @param images 图像记录列表。
 * @param classification_threshold 图像分数分类阈值。
 * @return 图表描述符。
 */
MODEL_API QVariantMap anomalyScoreChartForImages(const QList<EvaluationImageData> &images,
                                                 double classification_threshold);

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
 * 布局为 类别 x 类别 + 误检/FP 列、漏检/FN 行及合计行列，与主链路矩阵键
 * （行\x1f列）对应。FP/FN 保留类别错误与未匹配事件的聚合值，误检/漏检
 * 仅表示未匹配的预测/真实框。
 * @param classes 类别目录。
 * @param matrix 矩阵计数（行\x1f列 -> 计数）。
 * @param total_count 全部评估单元计数：检测方法为实例事件数，异常检测为图像数。
 * @return 单元格映射列表。
 */
MODEL_API QVariantList evaluationConfusionCells(const QMap<int, QString> &classes, const QMap<QString, qint64> &matrix,
                                                qint64 total_count);

/**
 * @brief 组装完整评估结果映射。
 *
 * 主链路 evaluate 完成计数与事件收集后，由本函数完成结果序列化：图像记录、
 * 按类别指标、类别目录、混淆矩阵、官方指标/图表与能力声明。
 * @param context 评估结果组装上下文；引用字段只需在调用期间保持有效。
 * @return 评估结果映射；取消或失败时返回空映射。
 */
MODEL_API QVariantMap assembleEvaluationResult(const EvaluationResultContext &context);

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
 * 检测方法生成 confidence-IoU 工作点的实例指标与 Precision-Recall 曲线；
 * 异常检测方法生成 score-above-threshold 的图像级二元指标和异常分数分布图。
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
 * @brief 按 Ultralytics 核心流程构造目标检测 Precision-Recall 曲线。
 *
 * 预测按类别分别完成 IoU 匹配、置信度排序、累计 TP/FP、Precision envelope
 * 和均匀 Recall 插值；平均曲线为所有有效类别曲线的逐点平均。返回的数据集
 * 同时包含平均曲线和各类别曲线，界面可按 series_kind/class_id 选择展示。
 * @param images 评估图像记录。
 * @param class_catalog 类别 ID 到显示名称的目录。
 * @param iou_threshold IoU 匹配阈值。
 * @param strategy 匹配策略。
 * @param class_ids 可选类别过滤列表；空表示全部类别。
 * @param cancel 协作取消令牌，可为空。
 * @return 图表描述符。
 */
MODEL_API QVariantMap precisionRecallChartForImages(
    const QList<EvaluationImageData> &images, const QMap<int, QString> &class_catalog, double iou_threshold,
    evaluation::MatchingStrategy strategy, const QVariantList &class_ids = {},
    const std::shared_ptr<std::atomic_bool> &cancel = {});

}
