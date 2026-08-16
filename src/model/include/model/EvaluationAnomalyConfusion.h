#pragma once

#include "dltool/model/Export.h"
#include "model/ModelEvaluationModels.h"

#include <QList>
#include <QMap>
#include <QString>
#include <vector>

namespace dltool::model {

/**
 * @brief 一个图像级异常混淆矩阵样本。
 *
 * FP/FN 边缘遵循混淆矩阵布局：FP 按预测行累计，FN 按 GT 列累计。
 * 因此一个类别错配会同时贡献到两个错误边缘。
 */
struct MODEL_API AnomalyConfusionSample
{
    int     category_id{0};
    QString category_name;
    bool    category_anomaly{false};
    bool    predicted_anomaly{false};
};

/**
 * @brief 从图像级异常样本构造混淆矩阵单元格。
 * @param samples 已完成 GT 类别和预测异常状态适配的样本。
 * @param class_catalog 项目数据库中的全局类别目录。
 * @return 二元预测行 x 全局 GT 类别列 + FN/FP/合计行列的单元格列表。
 */
MODEL_API std::vector<EvaluationConfusionCell>
buildAnomalyConfusionCells(const QList<AnomalyConfusionSample> &samples, const QMap<int, QString> &class_catalog);

} // namespace dltool::model
