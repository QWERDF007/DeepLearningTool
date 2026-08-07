#pragma once

#include "dltool/model/Export.h"
#include "model/EvaluationGeometry.h"

#include <QList>
#include <QString>
#include <QVariantMap>

namespace dltool::model {

/**
 * @brief 评估主链路与图表构造共用的真值记录。
 */
struct MODEL_API EvaluationGroundTruthData
{
    qint64        label_id{-1}; ///< 标签 ID。
    int           class_id{-1}; ///< 类别 ID。
    QString       class_name;   ///< 类别名称。
    QVariantMap   geometry;     ///< 规范化几何记录。
    QVariantMap   bounds;       ///< 几何包围盒映射。
    EvaluationBox box;          ///< 解析出的包围盒。
    bool          anomaly{false}; ///< 该 GT 类别是否属于异常组。
};

/**
 * @brief 评估主链路与图表构造共用的预测记录。
 */
struct MODEL_API EvaluationPredictionData
{
    QString       prediction_id; ///< 预测实例 ID。
    qint64        image_id{-1};  ///< 所属图像 ID。
    int           class_id{-1};  ///< 预测类别 ID。
    QString       class_name;    ///< 预测类别名称。
    double        score{0.0};    ///< 置信度分数。
    QVariantMap   geometry;      ///< 规范化几何记录。
    QVariantMap   bounds;        ///< 几何包围盒映射。
    EvaluationBox box;           ///< 解析出的包围盒。
};

/**
 * @brief 评估主链路与图表构造共用的图像记录。
 */
struct MODEL_API EvaluationImageData
{
    qint64                           id{-1};         ///< 图像 ID。
    qint64                           dataset_id{-1}; ///< 所属数据集 ID。
    QString                          path;           ///< 图像路径。
    QString                          name;           ///< 图像名称。
    int                              width{0};       ///< 图像宽度。
    int                              height{0};      ///< 图像高度。
    QList<EvaluationGroundTruthData> gt;             ///< 真值列表。
    QList<EvaluationPredictionData>  predictions;    ///< 预测列表。
};

} // namespace dltool::model
