#include "model/AggregateEvaluation.h"

#include "model/EvaluationAnomalyConfusion.h"
#include "model/EvaluationCharts.h"
#include "model/ModelEvaluationProtocol.h"

#include <QSet>
#include <algorithm>

namespace dltool::model {

namespace {

/**
 * @brief 聚合统计计数（真正例/假正例/假负例）。
 */
struct AggregateCounts
{
    qint64 tp{0};
    qint64 fp{0};
    qint64 fn{0};
};

/**
 * @brief 判断图像记录是否为异常（按预测或 GT 类别包含类别 1）。
 * @param record 图像记录。
 * @param threshold 置信度阈值（仅预测侧使用）。
 * @param predicted 按预测判断时为 true，否则按 GT 判断。
 * @return 是异常返回 true。
 */
bool isAnomalyImage(const EvaluationImageRecord &record, const double threshold, const bool predicted)
{
    if (predicted)
        return predClassIds(record, threshold).contains(1);
    return std::any_of(record.gt.cbegin(), record.gt.cend(),
                       [](const EvaluationGroundTruthRecord &ground_truth) { return ground_truth.anomaly; });
}

const EvaluationGroundTruthRecord *primaryGroundTruth(const EvaluationImageRecord &image)
{
    const EvaluationGroundTruthRecord *result
        = image.gt.isEmpty() ? nullptr : &image.gt.front();
    for (const EvaluationGroundTruthRecord &ground_truth : image.gt)
        if (ground_truth.label_id < 0)
            return &ground_truth;
    return result;
}

QList<AnomalyConfusionSample> anomalyConfusionSamples(const QList<EvaluationImageRecord> &images,
                                                      const double threshold)
{
    QList<AnomalyConfusionSample> samples;
    samples.reserve(images.size());
    for (const EvaluationImageRecord &image : images)
    {
        const EvaluationGroundTruthRecord *ground_truth = primaryGroundTruth(image);
        const int category_id = ground_truth != nullptr && ground_truth->class_id >= 0 ? ground_truth->class_id : 0;
        const QString category_name = ground_truth != nullptr && !ground_truth->class_name.isEmpty()
            ? ground_truth->class_name
            : evaluation::displayText(evaluation::DisplayText::Good);
        const bool category_anomaly = ground_truth != nullptr && ground_truth->anomaly;
        const bool predicted_anomaly = isAnomalyImage(image, threshold, true);
        samples.push_back(AnomalyConfusionSample{category_id, category_name, category_anomaly, predicted_anomaly});
    }
    return samples;
}

/**
 * @brief 由聚合计数构造指标记录。
 * @param key 指标键。
 * @param label 显示名称。
 * @param class_id 类别 ID。
 * @param counts 聚合计数。
 * @return 指标记录。
 */
EvaluationMetricRecord aggregateMetric(const QString &key, const QString &label, const int class_id,
                                       const AggregateCounts &counts)
{
    EvaluationMetricRecord value;
    value.key               = key;
    value.label             = label;
    value.class_name        = label;
    value.class_id          = class_id;
    value.tp                = counts.tp;
    value.fp                = counts.fp;
    value.fn                = counts.fn;
    value.precision_defined = counts.tp + counts.fp > 0;
    value.recall_defined    = counts.tp + counts.fn > 0;
    value.precision         = value.precision_defined ? static_cast<double>(counts.tp) / (counts.tp + counts.fp) : 0.0;
    value.recall            = value.recall_defined ? static_cast<double>(counts.tp) / (counts.tp + counts.fn) : 0.0;
    value.f1_defined        = value.precision_defined && value.recall_defined && value.precision + value.recall > 0.0;
    value.f1 = value.f1_defined ? 2.0 * value.precision * value.recall / (value.precision + value.recall) : 0.0;
    return value;
}

/**
 * @brief 判断类别 ID 是否允许参与聚合。
 * @param class_ids 类别过滤列表（空表示全部允许）。
 * @param class_id 类别 ID。
 * @return 允许返回 true。
 */
bool aggregateClassAllowed(const QVariantList &class_ids, const int class_id)
{
    if (class_ids.isEmpty() || class_id < 0)
        return true;
    for (const QVariant &value : class_ids)
        if (value.toInt() == class_id)
            return true;
    return false;
}

}

const QList<int> &gtClassIds(const EvaluationImageRecord &record)
{
    return record.gt_class_ids;
}

QList<int> predClassIds(const EvaluationImageRecord &record, const double threshold)
{
    QList<int> ids;
    for (const EvaluationPredictionRecord &prediction : record.predictions)
        if (prediction.class_id >= 0 && prediction.score >= threshold && !ids.contains(prediction.class_id))
            ids.push_back(prediction.class_id);
    return ids;
}

bool hasPredictions(const EvaluationImageRecord &record, const double threshold)
{
    return std::any_of(record.predictions.cbegin(), record.predictions.cend(),
                       [threshold](const EvaluationPredictionRecord &prediction)
                       { return prediction.score >= threshold; });
}

bool hasGroundTruth(const EvaluationImageRecord &record)
{
    return record.has_gt;
}

double imageScore(const EvaluationImageRecord &record)
{
    return record.max_prediction_score;
}


EvaluationAggregateOutput aggregateEvaluation(const EvaluationAggregateInput &input)
{
    EvaluationAggregateOutput  output;
    const QString              matrix_fn    = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::FalseNegative);
    const QString              matrix_fp    = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::FalsePositive);
    const QString              matrix_unmatched_fn
        = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::UnmatchedGroundTruth);
    const QString              matrix_unmatched_fp
        = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::UnmatchedPrediction);
    const QString              matrix_total = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::Total);
    QMap<int, AggregateCounts> classes;
    QMap<int, QString>         class_names = input.class_catalog;
    QMap<QString, qint64>      matrix;
    AggregateCounts            overall;
    for (const EvaluationAggregateInput::InstanceEvent &record : input.instances)
    {
        // 异常检测事件的预测类别是内部 Good/Anomaly 二元展示值，不能混入
        // 项目数据库提供的全局类别目录。
        if (!input.anomaly_detection)
        {
            if (record.gt_class_id >= 0)
                class_names.insert(record.gt_class_id,
                                   record.gt_class.isEmpty() ? QString::number(record.gt_class_id) : record.gt_class);
            if (record.pred_class_id >= 0)
                class_names.insert(record.pred_class_id, record.pred_class.isEmpty()
                                                               ? QString::number(record.pred_class_id)
                                                               : record.pred_class);
        }
        if (record.status == evaluation::Status::TruePositive)
        {
            ++overall.tp;
            ++classes[record.pred_class_id].tp;
            ++matrix[QString::number(record.pred_class_id) + QLatin1Char('\x1f') + QString::number(record.gt_class_id)];
        }
        else if (record.status == evaluation::Status::ClassMismatch)
        {
            ++overall.fp;
            ++overall.fn;
            ++classes[record.pred_class_id].fp;
            ++classes[record.gt_class_id].fn;
            ++matrix[QString::number(record.pred_class_id) + QLatin1Char('\x1f') + QString::number(record.gt_class_id)];
        }
        else if (record.status == evaluation::Status::FalsePositive)
        {
            ++overall.fp;
            ++classes[record.pred_class_id].fp;
            ++matrix[QString::number(record.pred_class_id) + QLatin1Char('\x1f') + matrix_fp];
        }
        else if (record.status == evaluation::Status::FalseNegative)
        {
            ++overall.fn;
            ++classes[record.gt_class_id].fn;
            ++matrix[matrix_fn + QLatin1Char('\x1f') + QString::number(record.gt_class_id)];
        }
    }

    /**
     * @brief 异常项目按图像级评估。
     *
     * 正常图像在项目数据库或任务选择中没有 GT 标签，上面的实例事件矩阵
     * 无法表达真负，因此这里显式构造与检测方法布局一致的二元图像矩阵。
     */
    if (input.anomaly_detection)
    {
        matrix.clear();
        for (const EvaluationImageRecord &image : input.images)
        {
            const bool    ground_truth_anomaly = isAnomalyImage(image, input.confidence_threshold, false);
            const bool    predicted_anomaly    = isAnomalyImage(image, input.confidence_threshold, true);
            const QString row                  = predicted_anomaly ? QStringLiteral("1") : QStringLiteral("0");
            const QString column               = ground_truth_anomaly ? QStringLiteral("1") : QStringLiteral("0");
            ++matrix[row + QLatin1Char('\x1f') + column];
        }
    }

    AggregateCounts image_counts;
    if (input.anomaly_detection)
    {
        /**
         * @brief 异常方法是二元图像分类器，指标只看图像异常标记。
         *
         * GT 可能包含多个语义类别，但按类别 ID 累计会把一张图像拆成
         * 多个 FP/FN，使图像指标与二元混淆矩阵不一致。
         */
        for (const EvaluationImageRecord &image : input.images)
        {
            const bool ground_truth_anomaly = isAnomalyImage(image, input.confidence_threshold, false);
            const bool predicted_anomaly    = isAnomalyImage(image, input.confidence_threshold, true);
            if (ground_truth_anomaly && predicted_anomaly)
                ++image_counts.tp;
            else if (predicted_anomaly)
                ++image_counts.fp;
            else if (ground_truth_anomaly)
                ++image_counts.fn;
        }
    }
    else
    {
        for (const EvaluationImageRecord &image : input.images)
        {
            QSet<int> gt_classes;
            QSet<int> pred_classes;
            for (const int class_id : gtClassIds(image))
                if (aggregateClassAllowed(input.class_ids, class_id))
                    gt_classes.insert(class_id);
            for (const int class_id : predClassIds(image, input.confidence_threshold))
                if (aggregateClassAllowed(input.class_ids, class_id))
                    pred_classes.insert(class_id);
            for (const int class_id : pred_classes)
            {
                if (gt_classes.contains(class_id))
                    ++image_counts.tp;
                else
                    ++image_counts.fp;
            }
            for (const int class_id : gt_classes)
                if (!pred_classes.contains(class_id))
                    ++image_counts.fn;
            if (gt_classes.isEmpty() && pred_classes.isEmpty())
            {
                const bool has_gt   = hasGroundTruth(image) && input.class_ids.isEmpty();
                const bool has_pred = hasPredictions(image, input.confidence_threshold) && input.class_ids.isEmpty();
                if (has_gt && has_pred)
                    ++image_counts.tp;
                else if (has_pred)
                    ++image_counts.fp;
                else if (has_gt)
                    ++image_counts.fn;
            }
        }
    }

    output.instance_metrics.push_back(aggregateMetric(QStringLiteral("overall"), QString("整体"), -1, overall));
    for (auto it = class_names.cbegin(); it != class_names.cend(); ++it)
    {
        output.per_class_metrics.push_back(
            aggregateMetric(QString::number(it.key()), it.value(), it.key(), classes.value(it.key())));
    }
    output.image_metrics.push_back(aggregateMetric(QStringLiteral("image"), QString("图像"), -1, image_counts));

    if (input.anomaly_detection)
    {
        output.confusion = buildAnomalyConfusionCells(anomalyConfusionSamples(input.images,
                                                                               input.confidence_threshold),
                                                       class_names);
    }
    else
    {
    // 混淆矩阵单元格：类别 x 类别 + 误检/FP 列、漏检/FN 行及合计行列。
    std::vector<EvaluationConfusionCell> cells;
    const auto appendCell = [&](const QString &row, const QString &column, qint64 count,
                                const evaluation::CellKind kind, bool selectable, bool diagonal, bool error)
    {
        const bool    row_fn              = row == matrix_fn;
        const bool    row_unmatched_fn    = row == matrix_unmatched_fn;
        const bool    row_total           = row == matrix_total;
        const bool    column_fp           = column == matrix_fp;
        const bool    column_unmatched_fp = column == matrix_unmatched_fp;
        const bool    column_total        = column == matrix_total;
        const int     row_id              = row_fn || row_unmatched_fn || row_total ? -1 : row.toInt();
        const int     column_id           = column_fp || column_unmatched_fp || column_total ? -1 : column.toInt();
        const QString total_label         = evaluation::displayText(evaluation::DisplayText::Total);
        const QString row_label           = row_fn
                                                ? evaluation::matrixAxisLabel(
                                                      evaluation::MatrixAxisKey::FalseNegative)
                                                : (row_unmatched_fn
                                                       ? evaluation::matrixAxisLabel(
                                                             evaluation::MatrixAxisKey::UnmatchedGroundTruth)
                                                       : (row_total ? total_label : class_names.value(row_id)));
        const QString column_label
            = column_fp
                  ? evaluation::matrixAxisLabel(evaluation::MatrixAxisKey::FalsePositive)
                  : (column_unmatched_fp
                         ? evaluation::matrixAxisLabel(evaluation::MatrixAxisKey::UnmatchedPrediction)
                         : (column_total ? total_label : class_names.value(column_id)));
        EvaluationConfusionCell cell;
        cell.row_key         = row;
        cell.column_key      = column;
        cell.row_label       = row_label;
        cell.column_label    = column_label;
        cell.row_class_id    = row_id;
        cell.column_class_id = column_id;
        cell.count           = count;
        cell.cell_kind       = kind;
        cell.selectable      = selectable;
        cell.diagonal        = diagonal;
        cell.error           = error;
        cells.push_back(cell);
    };
    QMap<int, qint64> pred_totals;
    QMap<int, qint64> gt_totals;
    QMap<int, qint64> row_fp_totals;
    QMap<int, qint64> column_fn_totals;
    QMap<int, qint64> unmatched_pred_totals;
    QMap<int, qint64> unmatched_gt_totals;
    qint64            unmatched_fp = 0;
    qint64            unmatched_fn = 0;
    qint64            mismatch_total = 0;
    for (auto it = matrix.cbegin(); it != matrix.cend(); ++it)
    {
        const QList<QString> keys = it.key().split(QLatin1Char('\x1f'));
        if (keys.size() != 2)
            continue;
        const QString &row_key    = keys.at(0);
        const QString &column_key = keys.at(1);
        const qint64   count      = it.value();
        const bool     row_fn     = row_key == matrix_fn;
        const bool     column_fp  = column_key == matrix_fp;
        if (row_fn)
        {
            unmatched_fn += it.value();
            if (!column_fp)
            {
                unmatched_gt_totals[column_key.toInt()] += count;
                column_fn_totals[column_key.toInt()] += count;
            }
        }
        else
            pred_totals[row_key.toInt()] += count;
        if (column_fp)
        {
            unmatched_fp += it.value();
            if (!row_fn)
            {
                unmatched_pred_totals[row_key.toInt()] += count;
                row_fp_totals[row_key.toInt()] += count;
            }
        }
        else
            gt_totals[column_key.toInt()] += count;
        if (!row_fn && !column_fp && row_key != column_key)
        {
            row_fp_totals[row_key.toInt()] += count;
            column_fn_totals[column_key.toInt()] += count;
            mismatch_total += count;
        }
    }
    for (auto row_it = class_names.cbegin(); row_it != class_names.cend(); ++row_it)
    {
        const QString row = QString::number(row_it.key());
        for (auto column_it = class_names.cbegin(); column_it != class_names.cend(); ++column_it)
        {
            const QString column   = QString::number(column_it.key());
            const bool    diagonal = row_it.key() == column_it.key();
            appendCell(row, column, matrix.value(row + QLatin1Char('\x1f') + column),
                       diagonal ? evaluation::CellKind::Match : evaluation::CellKind::ClassMismatch, true, diagonal,
                       !diagonal);
        }
        appendCell(row, matrix_unmatched_fp, unmatched_pred_totals.value(row_it.key()),
                   evaluation::CellKind::FalsePositive, true, false, true);
        appendCell(row, matrix_fp, row_fp_totals.value(row_it.key()),
                   evaluation::CellKind::FalsePositive, true, false, true);
        appendCell(row, matrix_total, pred_totals.value(row_it.key()), evaluation::CellKind::PredTotal, true, false,
                   false);
    }
    for (auto column_it = class_names.cbegin(); column_it != class_names.cend(); ++column_it)
    {
        const QString column = QString::number(column_it.key());
        appendCell(matrix_unmatched_fn, column, unmatched_gt_totals.value(column_it.key()),
                   evaluation::CellKind::FalseNegative, true, false, true);
    }
    appendCell(matrix_unmatched_fn, matrix_unmatched_fp, 0, evaluation::CellKind::NotApplicable, true, false, false);
    appendCell(matrix_unmatched_fn, matrix_fp, unmatched_fn, evaluation::CellKind::FalseNegative, true, false, true);
    appendCell(matrix_unmatched_fn, matrix_total, unmatched_fn, evaluation::CellKind::FalseNegativeTotal, true, false,
               true);
    for (auto column_it = class_names.cbegin(); column_it != class_names.cend(); ++column_it)
    {
        const QString column = QString::number(column_it.key());
        appendCell(matrix_fn, column, column_fn_totals.value(column_it.key()),
                   evaluation::CellKind::FalseNegative, true, false, true);
    }
    appendCell(matrix_fn, matrix_unmatched_fp, unmatched_fp, evaluation::CellKind::FalsePositive, true, false, true);
    appendCell(matrix_fn, matrix_fp, mismatch_total + unmatched_fp + unmatched_fn,
               evaluation::CellKind::NotApplicable, true, false, false);
    appendCell(matrix_fn, matrix_total, mismatch_total + unmatched_fn,
               evaluation::CellKind::FalseNegativeTotal, true, false, true);
    for (auto column_it = class_names.cbegin(); column_it != class_names.cend(); ++column_it)
    {
        const QString column = QString::number(column_it.key());
        appendCell(matrix_total, column, gt_totals.value(column_it.key()), evaluation::CellKind::GtTotal, true, false,
                   false);
    }
    appendCell(matrix_total, matrix_unmatched_fp, unmatched_fp, evaluation::CellKind::FalsePositiveTotal, true, false,
               true);
    appendCell(matrix_total, matrix_fp, mismatch_total + unmatched_fp,
               evaluation::CellKind::FalsePositiveTotal, true, false, true);
    appendCell(matrix_total, matrix_total, input.instances.size(),
               evaluation::CellKind::All, true, false, false);
    output.confusion = std::move(cells);
    }

    if (input.anomaly_detection)
        output.charts.push_back(anomalyScoreChartForImages(input.images, input.confidence_threshold));

    /**
     * @brief 沿用 Service 图表描述符，并按聚合输入重算阈值指标。
     *
     * 异常分布图已在本地派生，因此跳过对应的 Service 图表。
     */
    for (const QVariantMap &descriptor : input.chart_descriptors)
    {
        const QString chart_id    = descriptor.value(evaluation::fieldName(evaluation::Field::ChartId)).toString();
        const QString filter_kind = descriptor.value(evaluation::fieldName(evaluation::Field::FilterKind)).toString();
        if (chart_id == evaluation::chartIdKey(evaluation::ChartId::AnomalyScoreDistribution))
            continue;
        if (descriptor.value(evaluation::fieldName(evaluation::Field::Kind)).toString()
                == evaluation::chartKindKey(evaluation::ChartKind::Bar)
            && (filter_kind == evaluation::filterKindKey(evaluation::FilterKind::PerClassMetrics)
                || chart_id == evaluation::chartIdKey(evaluation::ChartId::PerClassMetrics)))
            continue;
        QVariantMap filtered = descriptor;
        if (filter_kind == evaluation::filterKindKey(evaluation::FilterKind::PrecisionRecall)
            || chart_id == evaluation::chartIdKey(evaluation::ChartId::PrecisionRecall))
        {
            filtered = precisionRecallChartForImages(input.images, input.class_catalog, input.iou_threshold,
                                                     input.matching_strategy, input.class_ids);
        }
        else if (filter_kind == evaluation::filterKindKey(evaluation::FilterKind::ImageScore))
        {
            QVariantList labels;
            QVariantList scores;
            for (const EvaluationImageRecord &image : input.images)
            {
                labels.push_back(image.name);
                scores.push_back(imageScore(image));
            }
            filtered.insert(evaluation::fieldName(evaluation::Field::Data),
                            QVariantMap{
                                {  evaluation::fieldName(evaluation::Field::Labels),labels                                                                                },
                                {evaluation::fieldName(evaluation::Field::Datasets),
                                 QVariantList{QVariantMap{
                                 {evaluation::fieldName(evaluation::Field::Label), QStringLiteral("score")},
                                 {evaluation::fieldName(evaluation::Field::Data), scores}}}}
            });
        }
        output.charts.push_back(std::move(filtered));
    }
    return output;
}

}
