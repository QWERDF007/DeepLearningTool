#pragma once

#include "dltool/model/Export.h"
#include "model/EvaluationThresholdSearch.h"
#include "model/ModelEvaluationModels.h"
#include "model/ModelEvaluationProtocol.h"

#include <QList>
#include <QMap>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <vector>

namespace dltool::model {

/**
 * @brief 聚合评估输入：实例事件、图像记录、类别目录与过滤参数。
 */
struct MODEL_API EvaluationAggregateInput
{
    /**
     * @brief 单条实例事件（TP/FP/FN/类别错误）。
     */
    struct InstanceEvent
    {
        evaluation::Status status{evaluation::Status::Unknown}; ///< 事件状态。
        QString            gt_class;                            ///< GT 类别名称。
        QString            pred_class;                          ///< 预测类别名称。
        int                gt_class_id{-1};                     ///< GT 类别 ID。
        int                pred_class_id{-1};                   ///< 预测类别 ID。
    };

    QList<InstanceEvent>         instances;                 ///< 实例事件列表。
    QList<EvaluationImageRecord> images;                    ///< 图像记录列表。
    QMap<int, QString>           class_catalog;             ///< 类别 ID 到名称。
    QList<QVariantMap>           chart_descriptors;         ///< Service 图表描述符。
    QVariantList                 class_ids;                 ///< 类别过滤列表（空表示全部）。
    double                       confidence_threshold{0.5}; ///< 置信度阈值。
    double                       iou_threshold{0.5};        ///< IoU 阈值。
    evaluation::MatchingStrategy matching_strategy{evaluation::MatchingStrategy::GreedyIoU}; ///< 匹配策略。
    bool                         has_instance_metrics{false};                                ///< 是否包含实例指标。
    bool                         has_image_metrics{false};                                   ///< 是否包含图像指标。
    bool                         has_confusion_matrix{false};                                ///< 是否包含混淆矩阵。
    bool                         anomaly_detection{false};                                   ///< 是否为异常检测。
    EvaluationThresholdSearchResult threshold_search;                                        ///< 当前完整评估的阈值搜索结果。
    bool                         threshold_search_is_complete{false};                       ///< 当前图像范围是否仍是完整评估范围。
};

/**
 * @brief 聚合评估输出：各指标表、混淆矩阵与图表。
 */
struct MODEL_API EvaluationAggregateOutput
{
    std::vector<EvaluationMetricRecord>  instance_metrics;  ///< 实例级指标。
    std::vector<EvaluationMetricRecord>  image_metrics;     ///< 图像级指标。
    std::vector<EvaluationMetricRecord>  per_class_metrics; ///< 按类别指标。
    std::vector<EvaluationConfusionCell> confusion;         ///< 混淆矩阵单元格。
    QList<QVariantMap>                   charts;            ///< 图表描述符。
};

/**
 * @brief 获取图像记录的 GT 类别 ID 列表。
 * @param record 图像记录。
 * @return 类别 ID 列表（可能为空）。
 */
MODEL_API const QList<int> &gtClassIds(const EvaluationImageRecord &record);

/**
 * @brief 获取图像记录中达到阈值的预测类别 ID 列表（去重）。
 * @param record 图像记录。
 * @param threshold 置信度阈值。
 * @return 类别 ID 列表。
 */
MODEL_API QList<int> predClassIds(const EvaluationImageRecord &record, double threshold);

/**
 * @brief 判断图像记录是否存在达到阈值的预测。
 * @param record 图像记录。
 * @param threshold 置信度阈值。
 * @return 存在返回 true。
 */
MODEL_API bool hasPredictions(const EvaluationImageRecord &record, double threshold);

/**
 * @brief 判断图像记录是否包含 GT。
 * @param record 图像记录。
 * @return 包含 GT 返回 true。
 */
MODEL_API bool hasGroundTruth(const EvaluationImageRecord &record);

/**
 * @brief 获取图像记录的最大预测分数。
 * @param record 图像记录。
 * @return 最大分数。
 */
MODEL_API double imageScore(const EvaluationImageRecord &record);

/**
 * @brief 执行聚合评估：实例/图像指标、混淆矩阵与派生图表。
 *
 * 输入经 ViewModel 过滤后进入此函数；匹配与 IoU 复用 EvaluationMatching /
 * EvaluationGeometry 公共实现，图表复用 Service 生成的描述符。
 * @param input 聚合评估输入。
 * @return 聚合评估输出。
 */
MODEL_API EvaluationAggregateOutput aggregateEvaluation(const EvaluationAggregateInput &input);

} // namespace dltool::model
