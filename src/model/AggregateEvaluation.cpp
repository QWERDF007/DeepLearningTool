#include "model/AggregateEvaluation.h"

#include "model/EvaluationCharts.h"
#include "model/EvaluationGeometry.h"
#include "model/EvaluationMatching.h"
#include "model/ModelEvaluationProtocol.h"

#include <QSet>
#include <QVector>
#include <algorithm>
#include <cmath>

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
 * @brief 计算两条 QVariantMap 几何记录的 IoU。
 *
 * 与 Service 的匹配规则一致：两框均无效视为完全匹配（IoU=1）；单边无效
 * 返回 0。解析与交并比计算复用 EvaluationGeometry 公共实现。
 * @param lhs 左侧几何记录。
 * @param rhs 右侧几何记录。
 * @return IoU 值。
 */
double aggregateIou(const QVariantMap &lhs, const QVariantMap &rhs)
{
    EvaluationBox a;
    EvaluationBox b;
    const bool    has_a = readBox(lhs, a);
    const bool    has_b = readBox(rhs, b);
    if (!has_a && !has_b)
        return 1.0;
    if (!has_a || !has_b)
        return 0.0;
    return intersectionOverUnion(a, b);
}

/**
 * @brief 聚合匹配对（预测/真值下标与 IoU）。
 */
struct AggregateMatch
{
    int    prediction{-1};   ///< 预测下标。
    int    ground_truth{-1}; ///< 真值下标。
    double iou{0.0};         ///< 匹配 IoU。
};

/**
 * @brief 聚合匹配：委托公共匹配模块，注入 QVariantMap 几何 IoU。
 * @param predictions 预测记录列表。
 * @param ground_truth 真值记录列表。
 * @param threshold IoU 阈值。
 * @param strategy 匹配策略。
 * @return 匹配对列表（按预测下标升序）。
 */
QList<AggregateMatch> aggregateMatches(const QList<EvaluationPredictionRecord>  &predictions,
                                       const QList<EvaluationGroundTruthRecord> &ground_truth, const double threshold,
                                       const evaluation::MatchingStrategy strategy)
{
    // 几何 IoU 注入：ViewModel 使用 QVariantMap 几何记录，Service 使用 Box；
    // 匹配算法（贪心/Hungarian）由 EvaluationMatching 公共模块提供。
    const auto iou_fn = [&predictions, &ground_truth](const int prediction, const int gt)
    {
        return aggregateIou(predictions.at(prediction).geometry, ground_truth.at(gt).geometry);
    };
    const QList<MatchPair> matches = strategy == evaluation::MatchingStrategy::HungarianIoU
                                       ? hungarianIoUMatches(predictions.size(), ground_truth.size(), iou_fn, threshold)
                                       : greedyIoUMatches(predictions.size(), ground_truth.size(), iou_fn, threshold);

    QList<AggregateMatch> result;
    result.reserve(matches.size());
    for (const MatchPair &pair : matches) result.push_back({pair.prediction, pair.ground_truth, pair.iou});
    return result;
}

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
    return std::any_of(record.gt_instances.cbegin(), record.gt_instances.cend(),
                       [](const EvaluationGroundTruthRecord &ground_truth) { return ground_truth.anomaly; });
}

struct AnomalyGroundTruthColumn
{
    QString name;
    bool    anomaly{false};
};

const EvaluationGroundTruthRecord *primaryGroundTruth(const EvaluationImageRecord &image)
{
    const EvaluationGroundTruthRecord *result
        = image.gt_instances.isEmpty() ? nullptr : &image.gt_instances.front();
    for (const EvaluationGroundTruthRecord &ground_truth : image.gt_instances)
        if (ground_truth.label_id < 0)
            return &ground_truth;
    return result;
}

std::vector<EvaluationConfusionCell> anomalyConfusionCells(const QList<EvaluationImageRecord> &images,
                                                           const double threshold)
{
    const QString matrix_fn    = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::FalseNegative);
    const QString matrix_fp    = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::FalsePositive);
    const QString matrix_total = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::Total);
    const QString separator(QLatin1Char('\x1f'));
    QMap<int, AnomalyGroundTruthColumn> categories;
    QMap<QString, qint64>               counts;
    QMap<int, qint64>                   row_totals;
    QMap<int, qint64>                   row_errors;
    QMap<int, qint64>                   column_totals;
    QMap<int, qint64>                   column_errors;
    qint64                              error_total = 0;

    for (const EvaluationImageRecord &image : images)
    {
        const EvaluationGroundTruthRecord *ground_truth = primaryGroundTruth(image);
        const int category_id = ground_truth != nullptr && ground_truth->class_id >= 0 ? ground_truth->class_id : 0;
        const QString category_name = ground_truth != nullptr && !ground_truth->class_name.isEmpty()
            ? ground_truth->class_name
            : QStringLiteral("GOOD");
        const bool category_anomaly = ground_truth != nullptr && ground_truth->anomaly;
        categories[category_id] = AnomalyGroundTruthColumn{category_name, category_anomaly};

        const int row_id = isAnomalyImage(image, threshold, true) ? 1 : 0;
        ++counts[QString::number(row_id) + separator + QString::number(category_id)];
        ++row_totals[row_id];
        ++column_totals[category_id];
        if ((row_id == 1) != category_anomaly)
        {
            ++row_errors[row_id];
            ++column_errors[category_id];
            ++error_total;
        }
    }

    std::vector<EvaluationConfusionCell> cells;
    const auto appendCell = [&cells](const QString &row_key, const QString &row_label, const int row_class_id,
                                     const QString &column_key, const QString &column_label,
                                     const int column_class_id, const qint64 count,
                                     const evaluation::CellKind kind, const bool selectable,
                                     const bool diagonal, const bool error)
    {
        EvaluationConfusionCell cell;
        cell.row_key         = row_key;
        cell.column_key      = column_key;
        cell.row_label       = row_label;
        cell.column_label    = column_label;
        cell.row_class_id    = row_class_id;
        cell.column_class_id = column_class_id;
        cell.count           = count;
        cell.cell_kind       = kind;
        cell.selectable      = selectable;
        cell.diagonal        = diagonal;
        cell.error           = error;
        cells.push_back(std::move(cell));
    };

    const QString total_label = QStringLiteral("合计");
    for (const int row_id : {0, 1})
    {
        const QString row_key   = QString::number(row_id);
        const QString row_label = row_id == 0 ? QStringLiteral("GOOD") : QStringLiteral("Anomaly");
        for (auto category = categories.cbegin(); category != categories.cend(); ++category)
        {
            const qint64 count = counts.value(row_key + separator + QString::number(category.key()));
            const bool correct = (row_id == 1) == category.value().anomaly;
            appendCell(row_key, row_label, row_id, QString::number(category.key()), category.value().name,
                       category.key(), count,
                       correct ? evaluation::CellKind::Match : evaluation::CellKind::ClassMismatch,
                       true, correct, !correct);
        }
        appendCell(row_key, row_label, row_id, matrix_fp, matrix_fp, -1, row_errors.value(row_id),
                   evaluation::CellKind::FalsePositive, true, false, true);
        appendCell(row_key, row_label, row_id, matrix_total, total_label, -1, row_totals.value(row_id),
                   evaluation::CellKind::PredTotal, true, false, false);
    }
    for (auto category = categories.cbegin(); category != categories.cend(); ++category)
        appendCell(matrix_fn, matrix_fn, -1, QString::number(category.key()), category.value().name, category.key(),
                   column_errors.value(category.key()), evaluation::CellKind::FalseNegative, true, false, true);
    appendCell(matrix_fn, matrix_fn, -1, matrix_fp, matrix_fp, -1, error_total,
               evaluation::CellKind::NotApplicable, false, false, true);
    appendCell(matrix_fn, matrix_fn, -1, matrix_total, total_label, -1, error_total,
               evaluation::CellKind::FalseNegativeTotal, true, false, true);
    for (auto category = categories.cbegin(); category != categories.cend(); ++category)
        appendCell(matrix_total, total_label, -1, QString::number(category.key()), category.value().name,
                   category.key(), column_totals.value(category.key()), evaluation::CellKind::GtTotal,
                   true, false, false);
    appendCell(matrix_total, total_label, -1, matrix_fp, matrix_fp, -1, error_total,
               evaluation::CellKind::FalsePositiveTotal, true, false, true);
    appendCell(matrix_total, total_label, -1, matrix_total, total_label, -1, images.size(),
               evaluation::CellKind::All, true, false, false);
    return cells;
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

/**
 * @brief 统计给定置信度阈值下的聚合 TP/FP/FN。
 * @param images 图像记录列表。
 * @param class_ids 类别过滤列表。
 * @param threshold 置信度阈值。
 * @param iou_threshold IoU 阈值。
 * @param matching_strategy 匹配策略。
 * @return 聚合计数。
 */
AggregateCounts aggregateThresholdCounts(const QList<EvaluationImageRecord> &images, const QVariantList &class_ids,
                                         const double threshold, const double iou_threshold,
                                         const evaluation::MatchingStrategy matching_strategy)
{
    AggregateCounts counts;
    for (const EvaluationImageRecord &image : images)
    {
        QList<EvaluationPredictionRecord>  predictions;
        QList<EvaluationGroundTruthRecord> ground_truth;
        for (const EvaluationPredictionRecord &prediction : image.predictions)
        {
            if (prediction.score >= threshold && aggregateClassAllowed(class_ids, prediction.class_id))
                predictions.push_back(prediction);
        }
        for (const EvaluationGroundTruthRecord &gt : image.gt_instances)
        {
            if (aggregateClassAllowed(class_ids, gt.class_id))
                ground_truth.push_back(gt);
        }
        std::sort(predictions.begin(), predictions.end(),
                  [](const auto &lhs, const auto &rhs) { return lhs.score > rhs.score; });
        const QList<AggregateMatch> matches
            = aggregateMatches(predictions, ground_truth, iou_threshold, matching_strategy);
        QVector<bool> used_prediction(predictions.size(), false);
        QVector<bool> used_gt(ground_truth.size(), false);
        for (const AggregateMatch &candidate : matches)
        {
            if (used_prediction.at(candidate.prediction) || used_gt.at(candidate.ground_truth))
                continue;
            used_prediction[candidate.prediction] = true;
            used_gt[candidate.ground_truth]       = true;
            if (predictions.at(candidate.prediction).class_id == ground_truth.at(candidate.ground_truth).class_id)
                ++counts.tp;
            else
            {
                ++counts.fp;
                ++counts.fn;
            }
        }
        for (int index = 0; index < used_prediction.size(); ++index)
            if (!used_prediction.at(index))
                ++counts.fp;
        for (int index = 0; index < used_gt.size(); ++index)
            if (!used_gt.at(index))
                ++counts.fn;
    }
    return counts;
}

} // namespace

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
    const QString              matrix_total = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::Total);
    QMap<int, AggregateCounts> classes;
    QMap<int, QString>         class_names = input.class_catalog;
    QMap<QString, qint64>      matrix;
    AggregateCounts            overall;
    for (const EvaluationAggregateInput::InstanceEvent &record : input.instances)
    {
        if (record.gt_class_id >= 0)
            class_names.insert(record.gt_class_id,
                               record.gt_class.isEmpty() ? QString::number(record.gt_class_id) : record.gt_class);
        if (record.pred_class_id >= 0)
            class_names.insert(record.pred_class_id,
                               record.pred_class.isEmpty() ? QString::number(record.pred_class_id) : record.pred_class);
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

    // 异常项目按图像级评估。GOOD 图像在项目数据库/任务选择中没有 GT 标签，
    // 上面的实例事件矩阵无法表达真负；这里显式构造二元图像矩阵，行列布局
    // 与检测方法保持一致。
    if (input.anomaly_detection)
    {
        class_names.clear();
        class_names.insert(0, QStringLiteral("GOOD"));
        class_names.insert(1, QStringLiteral("Anomaly"));
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
        // Anomaly methods are binary image-level classifiers.  The GT may
        // contain several semantic classes, but only the anomaly flag of the
        // image matters; counting every class ID here turns one image into
        // multiple FP/FN events and makes the image metrics disagree with the
        // binary confusion matrix.
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
        output.confusion = anomalyConfusionCells(input.images, input.confidence_threshold);
    }
    else
    {
    // 混淆矩阵单元格：类别 x 类别 + FN/FP/合计行列。
    std::vector<EvaluationConfusionCell> cells;
    const auto appendCell = [&](const QString &row, const QString &column, qint64 count,
                                const evaluation::CellKind kind, bool selectable, bool diagonal, bool error)
    {
        const bool    row_fn       = row == matrix_fn;
        const bool    row_total    = row == matrix_total;
        const bool    column_fp    = column == matrix_fp;
        const bool    column_total = column == matrix_total;
        const int     row_id       = row_fn || row_total ? -1 : row.toInt();
        const int     column_id    = column_fp || column_total ? -1 : column.toInt();
        const QString total_label  = QString("合计");
        const QString row_label    = row_fn ? matrix_fn : (row_total ? total_label : class_names.value(row_id));
        const QString column_label
            = column_fp ? matrix_fp : (column_total ? total_label : class_names.value(column_id));
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
                column_fn_totals[column_key.toInt()] += count;
        }
        else
            pred_totals[row_key.toInt()] += count;
        if (column_fp)
        {
            unmatched_fp += it.value();
            if (!row_fn)
                row_fp_totals[row_key.toInt()] += count;
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
        appendCell(row, matrix_fp, row_fp_totals.value(row_it.key()),
                   evaluation::CellKind::FalsePositive, true, false, true);
        appendCell(row, matrix_total, pred_totals.value(row_it.key()), evaluation::CellKind::PredTotal, true, false,
                   false);
    }
    for (auto column_it = class_names.cbegin(); column_it != class_names.cend(); ++column_it)
    {
        const QString column = QString::number(column_it.key());
        appendCell(matrix_fn, column, column_fn_totals.value(column_it.key()),
                   evaluation::CellKind::FalseNegative, true, false, true);
    }
    appendCell(matrix_fn, matrix_fp, mismatch_total + unmatched_fp + unmatched_fn,
               evaluation::CellKind::NotApplicable, false, false, false);
    appendCell(matrix_fn, matrix_total, mismatch_total + unmatched_fn,
               evaluation::CellKind::FalseNegativeTotal, true, false, true);
    for (auto column_it = class_names.cbegin(); column_it != class_names.cend(); ++column_it)
    {
        const QString column = QString::number(column_it.key());
        appendCell(matrix_total, column, gt_totals.value(column_it.key()), evaluation::CellKind::GtTotal, true, false,
                   false);
    }
    appendCell(matrix_total, matrix_fp, mismatch_total + unmatched_fp,
               evaluation::CellKind::FalsePositiveTotal, true, false, true);
    appendCell(matrix_total, matrix_total, input.instances.size(),
               evaluation::CellKind::All, true, false, false);
    output.confusion = std::move(cells);
    }

    // 派生图表：按类别指标柱状图（聚合后重算）与异常分数分布图。
    if (input.has_instance_metrics)
    {
        QVariantList labels;
        QVariantList precision;
        QVariantList recall;
        QVariantList f1;
        for (const EvaluationMetricRecord &record : output.per_class_metrics)
        {
            labels.push_back(record.class_name);
            precision.push_back(record.precision);
            recall.push_back(record.recall);
            f1.push_back(record.f1);
        }
        output.charts.push_back(perClassMetricsChart(labels, precision, recall, f1));
    }

    if (input.anomaly_detection)
    {
        QList<EvaluationImageRecord> images;
        images.reserve(input.images.size());
        for (const EvaluationImageRecord &image : input.images) images.push_back(image);
        output.charts.push_back(anomalyScoreChartForImages(images));
    }

    // 沿用 Service 图表描述符：PR 曲线按聚合输入重算，过滤掉已被本地派生
    // 图表替换的异常分布与按类别指标图。
    for (const QVariantMap &descriptor : input.chart_descriptors)
    {
        const QString chart_id    = descriptor.value(evaluation::fieldName(evaluation::Field::ChartId)).toString();
        const QString filter_kind = descriptor.value(evaluation::fieldName(evaluation::Field::FilterKind)).toString();
        if (chart_id == QStringLiteral("anomaly_score_distribution"))
            continue;
        if (descriptor.value(evaluation::fieldName(evaluation::Field::Kind)).toString() == QStringLiteral("bar")
            && (filter_kind == QStringLiteral("per_class_metrics") || chart_id == QStringLiteral("per_class_metrics")))
            continue;
        QVariantMap filtered = descriptor;
        if (filter_kind == QStringLiteral("precision_recall") || chart_id == QStringLiteral("precision_recall"))
        {
            QList<double> thresholds{1.0, input.confidence_threshold};
            for (const EvaluationImageRecord &image : input.images)
                for (const EvaluationPredictionRecord &prediction : image.predictions)
                    if (std::isfinite(prediction.score) && aggregateClassAllowed(input.class_ids, prediction.class_id))
                        thresholds.push_back(std::clamp(prediction.score, 0.0, 1.0));
            std::sort(thresholds.begin(), thresholds.end(), std::greater<double>());
            QList<double> unique;
            for (const double value : thresholds)
                if (unique.isEmpty() || !qFuzzyCompare(unique.back() + 1.0, value + 1.0))
                    unique.push_back(value);
            QVariantList labels;
            QVariantList precision;
            QVariantList recall;
            for (const double threshold : unique)
            {
                const AggregateCounts counts = aggregateThresholdCounts(input.images, input.class_ids, threshold,
                                                                        input.iou_threshold, input.matching_strategy);
                labels.push_back(threshold);
                precision.push_back(counts.tp + counts.fp > 0 ? static_cast<double>(counts.tp) / (counts.tp + counts.fp)
                                                              : 0.0);
                recall.push_back(counts.tp + counts.fn > 0 ? static_cast<double>(counts.tp) / (counts.tp + counts.fn)
                                                           : 0.0);
            }
            filtered.insert(
                evaluation::fieldName(evaluation::Field::Data),
                QVariantMap{
                    {  evaluation::fieldName(evaluation::Field::Labels),labels                                                        },
                    {evaluation::fieldName(evaluation::Field::Datasets),
                     QVariantList{
                     QVariantMap{{evaluation::fieldName(evaluation::Field::Label), QStringLiteral("Precision")},
                     {evaluation::fieldName(evaluation::Field::Data), precision}},
                     QVariantMap{{evaluation::fieldName(evaluation::Field::Label), QStringLiteral("Recall")},
                     {evaluation::fieldName(evaluation::Field::Data), recall}}}}
            });
        }
        else if (filter_kind == QStringLiteral("image_score"))
        {
            QVariantList labels;
            QVariantList scores;
            for (const EvaluationImageRecord &image : input.images)
            {
                labels.push_back(image.image_name);
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

} // namespace dltool::model
