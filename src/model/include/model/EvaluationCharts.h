#pragma once

#include "dltool/model/Export.h"
#include "model/EvaluationData.h"
#include "model/ModelEvaluationModels.h"
#include "model/EvaluationThresholdSearch.h"
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
 * @brief Precision-Recall 曲线的固定插值点数。
 *
 * 该值对应 Ultralytics PR 曲线的均匀 Recall 网格；调整后会同时影响
 * 各类别曲线、平均曲线以及 AP 的数值积分。
 */
inline constexpr int kPrecisionRecallInterpolationPoints = 100;

/**
 * @brief 由图像记录构造异常分数分布图描述符。
 *
 * 正常图像和异常图像均从原始异常分数图取最大分数；缺少有效分数图的
 * 图像不会回退到 task.db 中的 image_score 或其他预测字段。
 * @param images 图像记录列表。
 * @param classification_threshold 图像分数分类阈值。
 * @return 图表描述符。
 */
MODEL_API QVariantMap anomalyScoreChartForImages(
    const QList<EvaluationImageData> &images, double classification_threshold,
    const EvaluationThresholdSearchResult *threshold_search = nullptr);

/**
 * @brief 构造异常检测的图像级 Precision-Recall 曲线。
 *
 * 异常检测使用每张图像原始异常分数图的最大值作为唯一分数，不参与
 * 检测/分割的实例 IoU 匹配。返回总体 micro 曲线和只读最佳阈值点。
 * @param images 评估图像记录列表。
 * @param threshold_search 已完成的图像级阈值搜索结果，可为空。
 * @param cancel 协作取消令牌，可为空。
 * @return 图表描述符。
 */
MODEL_API QVariantMap anomalyPrecisionRecallChartForImages(
    const QList<EvaluationImageData> &images,
    const EvaluationThresholdSearchResult *threshold_search = nullptr,
    const std::shared_ptr<std::atomic_bool> &cancel = {});

/**
 * @brief 构造目标检测/语义分割的总体置信度分布图。
 *
 * 使用全部有限预测分数在固定 [0, 1] 范围内分成 24 个等宽箱，并在有
 * 最佳阈值时附加只读参考线。
 */
MODEL_API QVariantMap confidenceDistributionChartForImages(
    const QList<EvaluationImageData> &images,
    const EvaluationThresholdSearchResult *threshold_search = nullptr);

/**
 * @brief 从异常检测图像记录搜索全局图像级最佳阈值。
 *
 * 分数取原始异常分数图的有限像素最大值，搜索计数与正式异常评估一致。
 * @param images 图像记录列表。
 * @param cancel 协作取消令牌，可为空。
 * @param err_msg 搜索失败或取消时的错误信息，可为空。
 * @return 阈值搜索结果。
 */
MODEL_API EvaluationThresholdSearchResult searchAnomalyThresholdForImages(
    const QList<EvaluationImageData> &images, const std::shared_ptr<std::atomic_bool> &cancel = {},
    QString *err_msg = nullptr);

/**
 * @brief 从检测/分割图像记录搜索全局 micro-F1 最佳置信度阈值。
 *
 * 每个候选阈值都按正式 IoU 与匹配策略重新匹配，并汇总所有类别的
 * TP/FP/FN；类别过滤列表为空表示全部类别。
 * @param images 图像记录列表。
 * @param iou_threshold IoU 匹配阈值。
 * @param strategy 匹配策略。
 * @param class_ids 可选类别过滤列表。
 * @param cancel 协作取消令牌，可为空。
 * @param err_msg 搜索失败或取消时的错误信息，可为空。
 * @return 阈值搜索结果。
 */
MODEL_API EvaluationThresholdSearchResult searchInstanceThresholdForImages(
    const QList<EvaluationImageData> &images, double iou_threshold, evaluation::MatchingStrategy strategy,
    const QVariantList &class_ids = {}, const std::shared_ptr<std::atomic_bool> &cancel = {},
    QString *err_msg = nullptr);

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
 * @brief 直接构造评估内部使用的强类型实例事件。
 *
 * 评估引擎直接消费值对象，避免在后台循环中经过 QVariantMap 协议边界再
 * 反解析；buildInstanceEvent 仍保留给协议调用方。
 */
MODEL_API EvaluationInstanceRecord buildInstanceRecord(const EvaluationImageData &image, evaluation::Status status,
                                                       const EvaluationGroundTruthData *gt,
                                                       const EvaluationPredictionData *pred, double iou,
                                                       const QString &dataset_root, const QString &prediction_root,
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
 * @brief 由 TP/FP/FN 构造评估协议指标映射。
 * @param tp 真正例数。
 * @param fp 假正例数。
 * @param fn 假负例数。
 * @return 指标映射（precision/recall/f1 及定义标记）。
 */
MODEL_API QVariantMap evaluationMetricMap(qint64 tp, qint64 fp, qint64 fn);

/**
 * @brief 构建异常检测方法图表（分数分布 + 图像级二元指标）。
 * @param images 图像记录。
 * @param diagnostic 诊断指标（消费 image 分项）。
 * @param confidence 置信度阈值。
 * @param threshold_search 当前评估的全局阈值搜索结果。
 * @return 异常方法官方评估输出。
 */
MODEL_API EvaluationChartOutput buildAnomalyEvaluationCharts(const QMap<qint64, EvaluationImageData> &images,
                                                              const QVariantMap &diagnostic, double confidence,
                                                              const EvaluationThresholdSearchResult *threshold_search
                                                              = nullptr);

/**
 * @brief 构建检测/分割实例匹配方法图表（Precision-Recall 曲线 + 实例指标）。
 * @param images 图像记录。
 * @param confidence 置信度阈值。
 * @param iou_threshold IoU 阈值。
 * @param strategy 匹配策略。
 * @param diagnostic 诊断指标（消费 instance 分项）。
 * @param cancel 协作取消令牌，可为空。
 * @return 实例匹配方法官方评估输出。
 */
MODEL_API EvaluationChartOutput buildInstanceMatchingEvaluationCharts(const QMap<qint64, EvaluationImageData> &images,
                                                                      double confidence, double iou_threshold,
                                                                       evaluation::MatchingStrategy strategy,
                                                                       const QVariantMap           &diagnostic,
                                                                       const std::shared_ptr<std::atomic_bool> &cancel
                                                                       = {},
                                                                       const EvaluationThresholdSearchResult *threshold_search
                                                                       = nullptr);

/**
 * @brief 构造目标检测/语义分割的全局 micro PR 曲线及类别曲线。
 *
 * 后端使用全部真实预测分数切分点计算正式匹配计数，前端边界只接收固定数量
 * 的 Recall 展示点，并额外接收全局最佳阈值操作点。返回的数据集包含 micro
 * 总体曲线与各类别曲线，不产生宏平均曲线。
 * @param images 评估图像记录。
 * @param class_catalog 类别 ID 到显示名称的目录。
 * @param iou_threshold IoU 匹配阈值。
 * @param strategy 匹配策略。
 * @param class_ids 可选类别过滤列表；空表示全部类别。
 * @param cancel 协作取消令牌，可为空。
 * @return 图表描述符。
 */
MODEL_API QVariantMap precisionRecallChartForImages(const QList<EvaluationImageData> &images,
                                                    const QMap<int, QString> &class_catalog, double iou_threshold,
                                                     evaluation::MatchingStrategy             strategy,
                                                     const QVariantList                      &class_ids = {},
                                                     const std::shared_ptr<std::atomic_bool> &cancel    = {},
                                                     const EvaluationThresholdSearchResult *threshold_search = nullptr);

} // namespace dltool::model
