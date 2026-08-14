#include "model/AggregateEvaluation.h"

#include "model/EvaluationAnomalyConfusion.h"
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

struct AggregateCachedMatchCandidate
{
    int    prediction{-1};
    int    ground_truth{-1};
    double iou{0.0};
};

/**
 * @brief 聚合图像的阈值匹配缓存。
 *
 * prediction_indices/ground_truth_indices 将缓存矩阵映射回原始图像记录；
 * active_predictions 按置信度阈值递增激活，避免每个阈值复制和重排预测列表。
 */
struct AggregateCurveImageCache
{
    const EvaluationImageRecord           *image{nullptr};
    QVector<int>                           prediction_indices;
    QVector<int>                           ground_truth_indices;
    QVector<QVector<double>>               ious;
    QVector<AggregateCachedMatchCandidate> greedy_candidates;
    QVector<int>                           score_order;
    QVector<int>                           score_rank;
    QVector<bool>                          active_predictions;
    std::shared_ptr<IncrementalHungarianMatcher> hungarian;
    int                                    hungarian_added{0};
    int                                    next_score{0};
};

AggregateCurveImageCache makeAggregateCurveImageCache(const EvaluationImageRecord &image,
                                                       const QVariantList &class_ids, const double iou_threshold)
{
    AggregateCurveImageCache cache;
    cache.image = &image;
    for (int index = 0; index < image.predictions.size(); ++index)
        if (aggregateClassAllowed(class_ids, image.predictions.at(index).class_id))
            cache.prediction_indices.push_back(index);
    for (int index = 0; index < image.gt.size(); ++index)
        if (aggregateClassAllowed(class_ids, image.gt.at(index).class_id))
            cache.ground_truth_indices.push_back(index);

    cache.ious.resize(cache.prediction_indices.size());
    cache.active_predictions.resize(cache.prediction_indices.size());
    cache.active_predictions.fill(false);
    for (int prediction = 0; prediction < cache.prediction_indices.size(); ++prediction)
    {
        cache.ious[prediction].resize(cache.ground_truth_indices.size());
        const auto &prediction_record = image.predictions.at(cache.prediction_indices.at(prediction));
        for (int ground_truth = 0; ground_truth < cache.ground_truth_indices.size(); ++ground_truth)
        {
            const auto &ground_truth_record = image.gt.at(cache.ground_truth_indices.at(ground_truth));
            const double iou = aggregateIou(prediction_record.geometry, ground_truth_record.geometry);
            cache.ious[prediction][ground_truth] = iou;
            if (iou >= iou_threshold)
                cache.greedy_candidates.push_back({prediction, ground_truth, iou});
        }
        cache.score_order.push_back(prediction);
    }

    std::sort(cache.score_order.begin(), cache.score_order.end(),
              [&cache](const int lhs, const int rhs)
              {
                  const double lhs_score = cache.image->predictions.at(cache.prediction_indices.at(lhs)).score;
                  const double rhs_score = cache.image->predictions.at(cache.prediction_indices.at(rhs)).score;
                  const bool   lhs_nan   = std::isnan(lhs_score);
                  const bool   rhs_nan   = std::isnan(rhs_score);
                  if (lhs_nan != rhs_nan)
                      return !lhs_nan;
                  if (!lhs_nan && lhs_score != rhs_score)
                      return lhs_score > rhs_score;
                  return lhs < rhs;
              });
    cache.score_rank.resize(cache.score_order.size());
    for (int rank = 0; rank < cache.score_order.size(); ++rank)
        cache.score_rank[cache.score_order.at(rank)] = rank;

    std::sort(cache.greedy_candidates.begin(), cache.greedy_candidates.end(),
              [&cache](const AggregateCachedMatchCandidate &lhs, const AggregateCachedMatchCandidate &rhs)
              {
                  if (lhs.iou != rhs.iou)
                      return lhs.iou > rhs.iou;
                  const int lhs_rank = cache.score_rank.at(lhs.prediction);
                  const int rhs_rank = cache.score_rank.at(rhs.prediction);
                  if (lhs_rank != rhs_rank)
                      return lhs_rank < rhs_rank;
                  return lhs.ground_truth < rhs.ground_truth;
              });
    return cache;
}

void activateAggregateCurvePredictions(AggregateCurveImageCache &cache, const double threshold)
{
    while (cache.next_score < cache.score_order.size())
    {
        const int    prediction = cache.score_order.at(cache.next_score);
        const double score = cache.image->predictions.at(cache.prediction_indices.at(prediction)).score;
        ++cache.next_score;
        if (std::isnan(score))
            continue;
        if (score < threshold)
        {
            --cache.next_score;
            return;
        }
        cache.active_predictions[prediction] = true;
    }
}

QList<MatchPair> aggregateGreedyMatches(const AggregateCurveImageCache &cache)
{
    QVector<bool>    used_prediction(cache.prediction_indices.size(), false);
    QVector<bool>    used_ground_truth(cache.ground_truth_indices.size(), false);
    QList<MatchPair> result;
    for (const AggregateCachedMatchCandidate &candidate : cache.greedy_candidates)
    {
        if (!cache.active_predictions.at(candidate.prediction)
            || used_prediction.at(candidate.prediction) || used_ground_truth.at(candidate.ground_truth))
            continue;
        used_prediction[candidate.prediction]     = true;
        used_ground_truth[candidate.ground_truth] = true;
        result.push_back({candidate.prediction, candidate.ground_truth, candidate.iou});
    }
    return result;
}

QList<MatchPair> aggregateHungarianMatches(AggregateCurveImageCache &cache, const double iou_threshold)
{
    if (cache.hungarian == nullptr)
        cache.hungarian = std::make_shared<IncrementalHungarianMatcher>(
            cache.prediction_indices.size(), cache.ground_truth_indices.size(), cache.ious, iou_threshold);

    while (cache.hungarian_added < cache.next_score)
    {
        const int prediction = cache.score_order.at(cache.hungarian_added);
        ++cache.hungarian_added;
        if (!cache.active_predictions.at(prediction))
            continue;
        if (!cache.hungarian->addPrediction(prediction))
            return {};
    }
    return cache.hungarian->matches();
}

QList<MatchPair> aggregateCurveMatches(AggregateCurveImageCache &cache, const double iou_threshold,
                                       const evaluation::MatchingStrategy strategy)
{
    return strategy == evaluation::MatchingStrategy::HungarianIoU
        ? aggregateHungarianMatches(cache, iou_threshold)
        : aggregateGreedyMatches(cache);
}

/**
 * @brief 统计给定置信度阈值下的聚合 TP/FP/FN。
 * @param curve_images 已按类别过滤并完成 IoU 缓存的图像列表。
 * @param threshold 置信度阈值。
 * @param iou_threshold IoU 阈值。
 * @param matching_strategy 匹配策略。
 * @return 聚合计数。
 */
AggregateCounts aggregateThresholdCounts(QList<AggregateCurveImageCache> &curve_images, const double threshold,
                                         const double iou_threshold,
                                         const evaluation::MatchingStrategy matching_strategy)
{
    AggregateCounts counts;
    for (AggregateCurveImageCache &curve_image : curve_images)
    {
        activateAggregateCurvePredictions(curve_image, threshold);
        const QList<MatchPair> matches = aggregateCurveMatches(curve_image, iou_threshold, matching_strategy);
        QVector<bool>               used_prediction(curve_image.prediction_indices.size(), false);
        QVector<bool>               used_gt(curve_image.ground_truth_indices.size(), false);
        for (const MatchPair &candidate : matches)
        {
            if (candidate.prediction < 0 || candidate.prediction >= used_prediction.size()
                || candidate.ground_truth < 0 || candidate.ground_truth >= used_gt.size()
                || used_prediction.at(candidate.prediction) || used_gt.at(candidate.ground_truth))
                continue;
            used_prediction[candidate.prediction] = true;
            used_gt[candidate.ground_truth]       = true;
            const auto &prediction
                = curve_image.image->predictions.at(curve_image.prediction_indices.at(candidate.prediction));
            const auto &ground_truth
                = curve_image.image->gt.at(curve_image.ground_truth_indices.at(candidate.ground_truth));
            if (prediction.class_id == ground_truth.class_id)
                ++counts.tp;
            else
            {
                ++counts.fp;
                ++counts.fn;
            }
        }
        for (int index = 0; index < used_prediction.size(); ++index)
            if (curve_image.active_predictions.at(index) && !used_prediction.at(index))
                ++counts.fp;
        for (int index = 0; index < used_gt.size(); ++index)
            if (!used_gt.at(index))
                ++counts.fn;
    }
    return counts;
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

    /**
     * @brief 异常项目按图像级评估。
     *
     * 正常图像在项目数据库或任务选择中没有 GT 标签，上面的实例事件矩阵
     * 无法表达真负，因此这里显式构造与检测方法布局一致的二元图像矩阵。
     */
    if (input.anomaly_detection)
    {
        class_names.clear();
        class_names.insert(0, evaluation::displayText(evaluation::DisplayText::Good));
        class_names.insert(1, evaluation::displayText(evaluation::DisplayText::Anomaly));
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
                                                                               input.confidence_threshold));
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

    /**
     * @brief 构造当前过滤结果的按类别指标图和异常分数分布图。
     *
     * Service 的初始图表不能直接复用，因为这里必须基于过滤后的图像集合重算。
     */
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
        output.charts.push_back(anomalyScoreChartForImages(input.images));

    /**
     * @brief 沿用 Service 图表描述符，并按聚合输入重算阈值指标。
     *
     * 异常分布图和按类别指标图已在本地派生，因此跳过对应的 Service 图表。
     */
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
            QList<double> prediction_scores;
            for (const EvaluationImageRecord &image : input.images)
                for (const EvaluationPredictionRecord &prediction : image.predictions)
                    if (std::isfinite(prediction.score) && aggregateClassAllowed(input.class_ids, prediction.class_id))
                        prediction_scores.push_back(prediction.score);
            const QList<double> unique = confidenceThresholds(prediction_scores, input.confidence_threshold);
            QList<AggregateCurveImageCache> curve_images;
            curve_images.reserve(input.images.size());
            for (const EvaluationImageRecord &image : input.images)
                curve_images.push_back(
                    makeAggregateCurveImageCache(image, input.class_ids, input.iou_threshold));
            QVariantList labels;
            QVariantList precision;
            QVariantList recall;
            for (const double threshold : unique)
            {
                const AggregateCounts counts
                    = aggregateThresholdCounts(curve_images, threshold, input.iou_threshold, input.matching_strategy);
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
