#pragma once

#include "dltool/model/Export.h"
#include "model/EvaluationCharts.h"
#include "model/EvaluationData.h"
#include "model/EvaluationThresholdSearch.h"
#include "model/ModelEvaluationModels.h"
#include "model/ModelEvaluationProtocol.h"

#include <QList>
#include <QMap>
#include <QMetaType>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <memory>

namespace dltool::model {

/**
 * @brief 一次完整评估产生的强类型内存结果。
 *
 * 该值对象由评估引擎在后台线程构造，通过 shared_ptr + Qt metatype 跨线程
 * 传回 GUI 线程；ModelEvaluationViewModel 消费它并填充各类
 * QAbstractItemModel。与协议 QVariantMap 快照不同，该结构体的字段有
 * 编译器类型检查，只有图表描述符保留 QVariantMap（Chart.js 边界）。
 */
struct MODEL_API EvaluationResult
{
    evaluation::Method method{evaluation::Method::Unknown};

    QMap<qint64, EvaluationImageData> images;        ///< 按图像 ID 索引的图像记录。
    QMap<int, QString>                class_catalog; ///< 类别 ID -> 类别名称。
    QMap<int, QString>                class_colors;  ///< 类别 ID -> 类别颜色。
    QMap<int, EvaluationCounts>       per_class;     ///< 类别级实例计数。
    EvaluationCounts                  overall;       ///< 全局实例计数。
    EvaluationCounts                  image_counts;  ///< 图像级计数。

    QVector<EvaluationConfusionCell>  matrix_cells;     ///< 混淆矩阵单元格。
    QMap<QString, qint64>             matrix;           ///< 原始矩阵键（行\x1f列）-> 计数。
    QVector<EvaluationInstanceRecord> instance_records; ///< 实例事件值对象。

    int  prediction_count{0};         ///< 参与评估的预测总数。
    bool has_confusion_matrix{false}; ///< 当前方法是否产出矩阵。
    bool has_instance_metrics{false}; ///< 是否产出实例级指标。
    bool has_image_metrics{false};    ///< 是否产出图像级指标。
    bool has_instance_events{false};  ///< 是否产出实例事件。

    QList<QVariantMap>              charts;                  ///< 图表描述符（Chart.js 边界保留 QVariantMap）。
    QStringList                     chart_kinds;             ///< 图表渲染类型 key 列表。
    QVariantMap                     official_metrics;       ///< 当前阈值工作点的官方指标。
    QVariantMap                     image_metric_definition; ///< 图像级指标定义。
    EvaluationThresholdSearchResult threshold_search;        ///< 当前评估的进程内阈值搜索结果。

    double                       confidence_threshold{evaluation::kDefaultConfidenceThreshold};
    double                       iou_threshold{evaluation::kDefaultIouThreshold};
    evaluation::MatchingStrategy matching_strategy{evaluation::MatchingStrategy::GreedyIoU};
    QVariantMap                  evaluation_config; ///< 规范化评估配置缓存键。
};

} // namespace dltool::model

Q_DECLARE_METATYPE(dltool::model::EvaluationResult)
Q_DECLARE_METATYPE(std::shared_ptr<dltool::model::EvaluationResult>)
