#include "model/ModelEvaluationService.h"

#include "model/EvaluationCharts.h"
#include "model/EvaluationCommon.h"
#include "model/EvaluationData.h"
#include "model/EvaluationDataset.h"
#include "model/EvaluationGeometry.h"
#include "model/EvaluationMatching.h"

#include <spdlog/spdlog.h>

#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QVariantList>
#include <algorithm>
#include <cmath>
#include <memory>

namespace dltool::model {

namespace {

bool sourceImageExists(const QString &path, const QString &dataset_root)
{
    QFileInfo image(path);
    if (!image.isAbsolute() && !image.exists() && !dataset_root.isEmpty())
        image = QFileInfo(QDir(dataset_root), path);
    return image.exists() && image.isFile();
}

bool isCancelled(const std::shared_ptr<std::atomic_bool> &cancel_token)
{
    return cancel_token != nullptr && cancel_token->load(std::memory_order_relaxed);
}

} // namespace

bool ModelEvaluationService::evaluate(const ModelEvaluationOptions &options, ModelEvaluationResult *result,
                                      QString *err_msg)
{
    if (isCancelled(options.cancel_token))
    {
        if (err_msg)
            *err_msg = QString("评估已取消");
        return false;
    }
    if (options.project_database_path.isEmpty() || options.dataset_file_list_path.isEmpty()
        || options.task_database_path.isEmpty() || options.prediction_dir.isEmpty())
    {
        if (err_msg)
            *err_msg = QString("评估路径参数不完整");
        return false;
    }

    QMap<qint64, EvaluationImageData> images;
    int                               missing_database_images  = 0;
    int                               ignored_selection_images = 0;
    if (!loadEvaluationImages(options.dataset_file_list_path, options.project_database_path, options.task_database_path,
                               options.method, images, options.cancel_token, err_msg, &missing_database_images,
                               &ignored_selection_images, options.image_dimensions_provider))
        return false;
    if (missing_database_images > 0)
        spdlog::warn("测试任务文件列表中有 {} 个图像已不在当前项目数据库中，已跳过", missing_database_images);
    if (ignored_selection_images > 0)
        spdlog::warn("测试任务文件列表中有 {} 个图像不属于当前数据集或类别选择，已跳过", ignored_selection_images);

    const QString dataset_root          = QFileInfo(options.project_database_path).absolutePath();
    int           missing_source_images = 0;
    for (auto it = images.begin(); it != images.end();)
    {
        if (sourceImageExists(it->path, dataset_root))
            ++it;
        else
        {
            ++missing_source_images;
            it = images.erase(it);
        }
    }
    if (missing_source_images > 0)
        spdlog::warn("测试评估跳过 {} 个不存在的源图像", missing_source_images);

    int prediction_count         = 0;
    int ignored_prediction_count = 0;
    if (!loadEvaluationPredictions(options.task_database_path, options.prediction_dir, images,
                                   evaluation::isAnomaly(options.method), &prediction_count, options.cancel_token,
                                   err_msg, &ignored_prediction_count))
        return false;
    if (ignored_prediction_count > 0)
        spdlog::warn("预测结果中有 {} 条记录不属于当前可用图像，已跳过", ignored_prediction_count);
    int images_without_predictions = 0;
    for (const EvaluationImageData &image : images)
    {
        if (image.predictions.isEmpty())
            ++images_without_predictions;
    }
    if (images_without_predictions > 0)
        spdlog::warn("{} 个图像没有推理结果，按空预测进行评估", images_without_predictions);
    prediction_count = 0;
    for (const EvaluationImageData &image : images) prediction_count += image.predictions.size();
    if (isCancelled(options.cancel_token))
    {
        if (err_msg)
            *err_msg = QString("评估已取消");
        return false;
    }
    const bool         anomaly_method = evaluation::isAnomaly(options.method);
    QMap<int, QString> classes;
    if (anomaly_method)
    {
        // 异常检测为图像级二元分类：GOOD 是隐式负类（正常样本没有 GT 标签）。
        classes.insert(0, QStringLiteral("GOOD"));
        classes.insert(1, QStringLiteral("Anomaly"));
    }
    else
    {
        for (const EvaluationImageData &image : images)
        {
            if (isCancelled(options.cancel_token))
            {
                if (err_msg)
                    *err_msg = QString("评估已取消");
                return false;
            }
            for (const EvaluationGroundTruthData &gt : image.gt)
                classes.insert(gt.class_id, gt.class_name.isEmpty() ? QString::number(gt.class_id) : gt.class_name);
        }
        for (const EvaluationImageData &image : images)
        {
            if (isCancelled(options.cancel_token))
            {
                if (err_msg)
                    *err_msg = QString("评估已取消");
                return false;
            }
            for (const EvaluationPredictionData &prediction : image.predictions)
                classes.insert(prediction.class_id, prediction.class_name.isEmpty()
                                                        ? QString::number(prediction.class_id)
                                                        : prediction.class_name);
        }
        classes.remove(-1);
    }

    EvaluationCounts            overall, image_counts;
    QMap<int, EvaluationCounts> per_class;
    QMap<QString, qint64>       matrix;
    QVariantList                event_records;
    const QString               dataset_root_path    = dataset_root;
    const QString               prediction_task_root = options.prediction_dir;
    const QString               matrix_fn       = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::FalseNegative);
    const QString               matrix_fp       = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::FalsePositive);
    const QString               matrix_total    = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::Total);
    const auto                  incrementMatrix = [&matrix](const QString &row, const QString &column, qint64 count = 1)
    {
        matrix[row + QLatin1Char('\x1f') + column] += count;
    };

    for (auto image_it = images.begin(); image_it != images.end(); ++image_it)
    {
        if (isCancelled(options.cancel_token))
        {
            if (err_msg)
                *err_msg = QString("评估已取消");
            return false;
        }
        EvaluationImageData            &image = image_it.value();
        QList<EvaluationPredictionData> predictions;
        for (const EvaluationPredictionData &prediction : image.predictions)
            if (prediction.score >= options.confidence_threshold)
                predictions.push_back(prediction);
        std::sort(predictions.begin(), predictions.end(),
                  [](const EvaluationPredictionData &a, const EvaluationPredictionData &b)
                  { return a.score > b.score; });
        QVector<bool>          used_gt(image.gt.size(), false);
        QVector<bool>          used_pred(predictions.size(), false);
        const QList<MatchPair> pairs       = matchPredictions(predictions, image.gt, options.iou_threshold,
                                                              options.matching_strategy, options.cancel_token);
        const auto             appendEvent = [&](const evaluation::Status status, const EvaluationGroundTruthData *gt,
                                                 const EvaluationPredictionData *pred, double iou)
        {
            event_records.push_back(buildInstanceEvent(image, status, gt, pred, iou, dataset_root_path,
                                                       prediction_task_root,
                                                       static_cast<qint64>(event_records.size() + 1)));
        };

        if (anomaly_method)
        {
            // 异常检测为图像级评估：每幅图像生成一条事件，供 UI 事件模型
            // 统一消费（包括没有原始事件的真负样本）。
            const EvaluationGroundTruthData *category_gt = nullptr;
            bool                             ground_truth_anomaly = false;
            for (const EvaluationGroundTruthData &gt : image.gt)
            {
                ground_truth_anomaly = ground_truth_anomaly || gt.anomaly;
                if (category_gt == nullptr || gt.label_id < 0)
                    category_gt = &gt;
            }
            const EvaluationPredictionData *anomaly_prediction = nullptr;
            double                          image_score        = 0.0;
            for (const EvaluationPredictionData &prediction : image.predictions)
            {
                image_score = std::max(image_score, prediction.score);
                if (prediction.class_id == 1 && prediction.score >= options.confidence_threshold
                    && (anomaly_prediction == nullptr || prediction.score > anomaly_prediction->score))
                    anomaly_prediction = &prediction;
            }
            const bool               predicted_anomaly    = anomaly_prediction != nullptr;
            const evaluation::Status status = ground_truth_anomaly && predicted_anomaly
                                                ? evaluation::Status::TruePositive
                                                : (!ground_truth_anomaly && !predicted_anomaly
                                                       ? evaluation::Status::TrueNegative
                                                       : (predicted_anomaly ? evaluation::Status::FalsePositive
                                                                            : evaluation::Status::FalseNegative));
            if (status == evaluation::Status::TruePositive)
                ++image_counts.tp;
            else if (status == evaluation::Status::FalsePositive)
                ++image_counts.fp;
            else if (status == evaluation::Status::FalseNegative)
                ++image_counts.fn;

            EvaluationGroundTruthData display_gt = category_gt != nullptr ? *category_gt : EvaluationGroundTruthData{};
            if (category_gt == nullptr)
            {
                display_gt.class_id   = 0;
                display_gt.class_name = QStringLiteral("GOOD");
                display_gt.anomaly    = false;
            }
            EvaluationPredictionData display_prediction
                = anomaly_prediction != nullptr ? *anomaly_prediction : EvaluationPredictionData{};
            display_prediction.class_id   = predicted_anomaly ? 1 : 0;
            display_prediction.class_name = predicted_anomaly ? QStringLiteral("Anomaly") : QStringLiteral("GOOD");
            display_prediction.score      = image_score;
            appendEvent(status, &display_gt, &display_prediction, 0.0);
            continue;
        }

        for (const MatchPair &pair : pairs)
        {
            if (isCancelled(options.cancel_token))
            {
                if (err_msg)
                    *err_msg = QString("评估已取消");
                return false;
            }
            if (used_gt.at(pair.ground_truth) || used_pred.at(pair.prediction))
                continue;
            used_gt[pair.ground_truth]                  = true;
            used_pred[pair.prediction]                  = true;
            const EvaluationGroundTruthData &gt         = image.gt.at(pair.ground_truth);
            const EvaluationPredictionData  &pred       = predictions.at(pair.prediction);
            const bool                       same_class = gt.class_id == pred.class_id;
            if (same_class)
            {
                ++overall.tp;
                ++per_class[pred.class_id].tp;
                incrementMatrix(QString::number(pred.class_id), QString::number(gt.class_id));
                appendEvent(evaluation::Status::TruePositive, &gt, &pred, pair.iou);
            }
            else
            {
                ++overall.fp;
                ++overall.fn;
                ++per_class[pred.class_id].fp;
                ++per_class[gt.class_id].fn;
                incrementMatrix(QString::number(pred.class_id), QString::number(gt.class_id));
                appendEvent(evaluation::Status::ClassMismatch, &gt, &pred, pair.iou);
            }
        }
        for (int p = 0; p < predictions.size(); ++p)
        {
            if (used_pred.at(p))
                continue;
            const EvaluationPredictionData &pred = predictions.at(p);
            ++overall.fp;
            ++per_class[pred.class_id].fp;
            incrementMatrix(QString::number(pred.class_id), matrix_fp);
            appendEvent(evaluation::Status::FalsePositive, nullptr, &pred, 0.0);
        }
        for (int g = 0; g < image.gt.size(); ++g)
        {
            if (used_gt.at(g))
                continue;
            const EvaluationGroundTruthData &gt = image.gt.at(g);
            ++overall.fn;
            ++per_class[gt.class_id].fn;
            incrementMatrix(matrix_fn, QString::number(gt.class_id));
            appendEvent(evaluation::Status::FalseNegative, &gt, nullptr, 0.0);
        }

        // 图像级指标按类别 presence 统计，而不是只要图像同时有 GT/PRED
        // 就记为 TP；这样类别错误和多类别图像的 FP/FN 不会被吞掉。
        QSet<int> image_gt_classes;
        QSet<int> image_pred_classes;
        for (const EvaluationGroundTruthData &gt : image.gt)
            if (gt.class_id >= 0)
                image_gt_classes.insert(gt.class_id);
        for (const EvaluationPredictionData &prediction : predictions)
            if (prediction.class_id >= 0)
                image_pred_classes.insert(prediction.class_id);
        if (image_gt_classes.isEmpty() && image_pred_classes.isEmpty())
        {
            const bool has_gt   = !image.gt.isEmpty();
            const bool has_pred = !predictions.isEmpty();
            if (has_gt && has_pred)
                ++image_counts.tp;
            else if (has_pred)
                ++image_counts.fp;
            else if (has_gt)
                ++image_counts.fn;
        }
        else
        {
            for (const int class_id : image_pred_classes)
            {
                if (image_gt_classes.contains(class_id))
                    ++image_counts.tp;
                else
                    ++image_counts.fp;
            }
            for (const int class_id : image_gt_classes)
                if (!image_pred_classes.contains(class_id))
                    ++image_counts.fn;
        }
    }

    if (isCancelled(options.cancel_token))
    {
        if (err_msg)
            *err_msg = QString("评估已取消");
        return false;
    }
    const QVariantMap evaluation_data = assembleEvaluationResult(
        images, classes, per_class, overall, image_counts, matrix, event_records, prediction_count, options.method,
        options.confidence_threshold, options.iou_threshold, options.matching_strategy, options.evaluation_config,
        options.cancel_token, err_msg);
    if (evaluation_data.isEmpty())
        return false;
    if (result)
    {
        result->evaluation_data = evaluation_data;
    }
    return true;
}

} // namespace dltool::model
