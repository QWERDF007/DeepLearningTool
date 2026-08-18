#include "model/InstanceMatchingEvaluationEngine.h"

#include "model/EvaluationCharts.h"
#include "model/EvaluationCommon.h"
#include "model/EvaluationMatching.h"

#include <QSet>
#include <QVariantList>
#include <algorithm>

namespace dltool::model {

namespace {

/**
 * @brief 将混淆矩阵单元格映射转换为值对象。
 *
 * 与 ModelEvaluationViewModel::loadEvaluation 中的反序列化保持一致。
 */
EvaluationConfusionCell confusionCellFromMap(const QVariantMap &map)
{
    EvaluationConfusionCell cell;
    cell.row_key         = recordText(map, evaluation::Field::RowKey);
    cell.column_key      = recordText(map, evaluation::Field::ColumnKey);
    cell.row_label       = recordText(map, evaluation::Field::RowLabel);
    cell.column_label    = recordText(map, evaluation::Field::ColumnLabel);
    cell.row_class_id    = recordInt(map, evaluation::Field::RowClassId);
    cell.column_class_id = recordInt(map, evaluation::Field::ColumnClassId);
    cell.count           = recordLong(map, evaluation::Field::Count);
    cell.cell_kind       = evaluation::cellKindFromKey(recordText(map, evaluation::Field::CellKind));
    cell.selectable      = map.value(evaluation::fieldName(evaluation::Field::Selectable)).toBool();
    cell.diagonal        = map.value(evaluation::fieldName(evaluation::Field::IsDiagonal)).toBool();
    cell.error           = map.value(evaluation::fieldName(evaluation::Field::IsError)).toBool();
    return cell;
}

} // namespace

InstanceMatchingEvaluationEngine::InstanceMatchingEvaluationEngine(const evaluation::Method method)
    : IEvaluationEngine()
    , method_(method)
{
}

evaluation::Method InstanceMatchingEvaluationEngine::method() const
{
    return method_;
}

void InstanceMatchingEvaluationEngine::buildClasses(const QMap<qint64, EvaluationImageData> &images,
                                                    QMap<int, QString>                      &classes)
{
    // 与旧 Service 非异常分支一致：从图像 GT 与预测补充类别名。
    for (const EvaluationImageData &image : images)
    {
        if (cancelled(scratch_.cancel_token))
            return;
        for (const EvaluationGroundTruthData &gt : image.gt)
            classes.insert(gt.class_id, gt.class_name.isEmpty() ? QString::number(gt.class_id) : gt.class_name);
    }
    for (const EvaluationImageData &image : images)
    {
        if (cancelled(scratch_.cancel_token))
            return;
        for (const EvaluationPredictionData &prediction : image.predictions)
            classes.insert(prediction.class_id, prediction.class_name.isEmpty() ? QString::number(prediction.class_id)
                                                                                : prediction.class_name);
    }
    classes.remove(-1);
}

bool InstanceMatchingEvaluationEngine::runDetectionLoop(const QMap<qint64, EvaluationImageData> &images,
                                                        QString                                 *err_msg)
{
    const auto fail = [err_msg](const QString &message)
    {
        if (err_msg)
            *err_msg = message;
        return false;
    };
    const QString dataset_root_path    = scratch_.dataset_root;
    const QString prediction_task_root = scratch_.prediction_root;
    const QString matrix_fn            = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::FalseNegative);
    const QString matrix_fp            = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::FalsePositive);
    const auto    incrementMatrix      = [this](const QString &row, const QString &column, qint64 count = 1)
    {
        scratch_.matrix[row + QLatin1Char('\x1f') + column] += count;
    };

    for (auto image_it = images.begin(); image_it != images.end(); ++image_it)
    {
        if (cancelled(scratch_.cancel_token))
            return fail(QString("评估已取消"));
        const EvaluationImageData      &image = image_it.value();
        QList<EvaluationPredictionData> predictions;
        for (const EvaluationPredictionData &prediction : image.predictions)
            if (prediction.score >= scratch_.confidence)
                predictions.push_back(prediction);
        std::stable_sort(predictions.begin(), predictions.end(),
                         [](const EvaluationPredictionData &a, const EvaluationPredictionData &b)
                         { return a.score > b.score; });

        QVector<bool>          used_gt(image.gt.size(), false);
        QVector<bool>          used_pred(predictions.size(), false);
        const QList<MatchPair> pairs
            = matchPredictions(predictions, image.gt, scratch_.iou, scratch_.matching_strategy, scratch_.cancel_token);
        const auto appendEvent = [&](const evaluation::Status status, const EvaluationGroundTruthData *gt,
                                     const EvaluationPredictionData *pred, double iou)
        {
            const QVariantMap event_map
                = buildInstanceEvent(image, status, gt, pred, iou, dataset_root_path, prediction_task_root,
                                     static_cast<qint64>(scratch_.events.size() + 1));
            scratch_.events.push_back(instanceFromMap(event_map));
        };

        for (const MatchPair &pair : pairs)
        {
            if (cancelled(scratch_.cancel_token))
                return fail(QString("评估已取消"));
            if (used_gt.at(pair.ground_truth) || used_pred.at(pair.prediction))
                continue;
            used_gt[pair.ground_truth]                  = true;
            used_pred[pair.prediction]                  = true;
            const EvaluationGroundTruthData &gt         = image.gt.at(pair.ground_truth);
            const EvaluationPredictionData  &pred       = predictions.at(pair.prediction);
            const bool                       same_class = gt.class_id == pred.class_id;
            if (same_class)
            {
                ++scratch_.overall.tp;
                ++scratch_.per_class[pred.class_id].tp;
                incrementMatrix(QString::number(pred.class_id), QString::number(gt.class_id));
                appendEvent(evaluation::Status::TruePositive, &gt, &pred, pair.iou);
            }
            else
            {
                ++scratch_.overall.fp;
                ++scratch_.overall.fn;
                ++scratch_.per_class[pred.class_id].fp;
                ++scratch_.per_class[gt.class_id].fn;
                incrementMatrix(QString::number(pred.class_id), QString::number(gt.class_id));
                appendEvent(evaluation::Status::ClassMismatch, &gt, &pred, pair.iou);
            }
        }
        for (int p = 0; p < predictions.size(); ++p)
        {
            if (used_pred.at(p))
                continue;
            const EvaluationPredictionData &pred = predictions.at(p);
            ++scratch_.overall.fp;
            ++scratch_.per_class[pred.class_id].fp;
            incrementMatrix(QString::number(pred.class_id), matrix_fp);
            appendEvent(evaluation::Status::FalsePositive, nullptr, &pred, 0.0);
        }
        for (int g = 0; g < image.gt.size(); ++g)
        {
            if (used_gt.at(g))
                continue;
            const EvaluationGroundTruthData &gt = image.gt.at(g);
            ++scratch_.overall.fn;
            ++scratch_.per_class[gt.class_id].fn;
            incrementMatrix(matrix_fn, QString::number(gt.class_id));
            appendEvent(evaluation::Status::FalseNegative, &gt, nullptr, 0.0);
        }

        /**
         * @brief 图像级指标按整图 OK / NG 二分类模型（良品/不良品判定）统计：
         * - 图像有标注（NG 不良品图）+ 模型有检出 -> TP（不良品检出）
         * - 图像无标注（OK 良品图）  + 模型有检出 -> FP（良品误报 / 过杀）
         * - 图像有标注（NG 不良品图）+ 模型无检出 -> FN（不良品漏检 / 漏杀）
         * - 图像无标注（OK 良品图）  + 模型无检出 -> TN（良品放行）
         */
        const bool has_gt   = !image.gt.isEmpty();
        const bool has_pred = !predictions.isEmpty();
        if (has_gt && has_pred)
            ++scratch_.image_counts.tp;
        else if (has_pred)
            ++scratch_.image_counts.fp;
        else if (has_gt)
            ++scratch_.image_counts.fn;
    }
    return true;
}

bool InstanceMatchingEvaluationEngine::computeInstanceCounts(const QMap<qint64, EvaluationImageData> &images,
                                                             const QMap<int, QString> &,
                                                             QMap<int, EvaluationCounts> &per_class,
                                                             EvaluationCounts &overall, QString *err_msg)
{
    // 单次 per-image 匹配循环同时产出实例计数、矩阵、事件与图像级计数，
    // 后续钩子从共享 scratch_ 读取，避免重复匹配。
    if (!runDetectionLoop(images, err_msg))
        return false;
    per_class = scratch_.per_class;
    overall   = scratch_.overall;
    return true;
}

bool InstanceMatchingEvaluationEngine::computeImageCounts(const QMap<qint64, EvaluationImageData> &,
                                                          EvaluationCounts &image_counts, QString *)
{
    // 图像级计数已在 runDetectionLoop 中按旧逻辑计算，此处从共享暂存区读取。
    image_counts = scratch_.image_counts;
    return true;
}

bool InstanceMatchingEvaluationEngine::buildEvents(const QMap<qint64, EvaluationImageData> &,
                                                   QList<EvaluationInstanceRecord> &events, QString *)
{
    // 实例事件与矩阵已在 runDetectionLoop 中生成（事件序号 1-based），共享暂存区读取。
    events = scratch_.events;
    return true;
}

QList<QVariantMap> InstanceMatchingEvaluationEngine::buildCharts(
    const QMap<qint64, EvaluationImageData> &images, const QMap<int, QString> &classes, const EvaluationCounts &overall,
    const EvaluationCounts &image_counts, const QMap<int, EvaluationCounts> &per_class, const QMap<QString, qint64> &,
    const QList<EvaluationInstanceRecord> &, QString *)
{
    // 构造与 assembleEvaluationResult 一致的诊断指标，供实例匹配图表构建器消费。
    QVariantList per_class_metrics;
    for (auto it = classes.cbegin(); it != classes.cend(); ++it)
    {
        const EvaluationCounts counts = per_class.value(it.key());
        QVariantMap            metric = evaluationMetricMap(counts.tp, counts.fp, counts.fn);
        metric.insert(evaluation::fieldName(evaluation::Field::ClassId), it.key());
        metric.insert(evaluation::fieldName(evaluation::Field::ClassName), it.value());
        per_class_metrics.push_back(metric);
    }
    const QVariantMap diagnostic = {
        {evaluation::fieldName(evaluation::Field::Instance),
         QVariantMap{{evaluation::fieldName(evaluation::Field::Overall),
                      evaluationMetricMap(overall.tp, overall.fp, overall.fn)},
                     {evaluation::fieldName(evaluation::Field::PerClass), per_class_metrics}}},
        {evaluation::fieldName(evaluation::Field::Image),
         evaluationMetricMap(image_counts.tp, image_counts.fp, image_counts.fn)}
    };
    const EvaluationChartOutput official = buildInstanceMatchingEvaluationCharts(
        images, scratch_.confidence, scratch_.iou, scratch_.matching_strategy, diagnostic, scratch_.cancel_token);
    QList<QVariantMap> charts;
    charts.reserve(official.charts.size());
    for (const QVariant &value : official.charts) charts.push_back(value.toMap());
    return charts;
}

QVector<EvaluationConfusionCell> InstanceMatchingEvaluationEngine::buildConfusionMatrix(
    const QMap<int, QString> &classes, const QMap<QString, qint64> &matrix)
{
    // 检测方法 total_count = 实例事件数（与 assembleEvaluationResult 一致）。
    const QVariantList               cell_maps = evaluationConfusionCells(classes, matrix, scratch_.events.size());
    QVector<EvaluationConfusionCell> cells;
    cells.reserve(cell_maps.size());
    for (const QVariant &value : cell_maps) cells.push_back(confusionCellFromMap(value.toMap()));
    return cells;
}

bool InstanceMatchingEvaluationEngine::hasConfusionMatrix() const
{
    return true;
}

QStringList InstanceMatchingEvaluationEngine::chartKinds() const
{
    // 与 evaluationCapabilitiesForMethod 的检测/分割分支一致：bar + line。
    return {evaluation::chartKindKey(evaluation::ChartKind::Bar),
            evaluation::chartKindKey(evaluation::ChartKind::Line)};
}

} // namespace dltool::model
