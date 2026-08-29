#include "model/IEvaluationEngine.h"

#include "model/EvaluationCommon.h"
#include "model/EvaluationDataset.h"
#include "model/EvaluationResult.h"
#include "model/ModelEvaluationOptions.h"

#include <spdlog/spdlog.h>

#include <QDir>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QHash>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <utility>

namespace dltool::model {

namespace {

bool sourceImageExists(const QString &path, const QString &dataset_root)
{
    QFileInfo image(path);
    if (!image.isAbsolute() && !image.exists() && !dataset_root.isEmpty())
        image = QFileInfo(QDir(dataset_root), path);
    return image.exists() && image.isFile();
}

class ThresholdSearchCache final
{
public:
    bool find(const QString &key, EvaluationThresholdSearchResult &result)
    {
        QMutexLocker locker(&mutex_);
        const auto    found = values_.constFind(key);
        if (found == values_.cend())
            return false;
        result = found.value();
        order_.removeAll(key);
        order_.push_back(key);
        return true;
    }

    void insert(const QString &key, const EvaluationThresholdSearchResult &result)
    {
        QMutexLocker locker(&mutex_);
        values_.insert(key, result);
        order_.removeAll(key);
        order_.push_back(key);
        while (order_.size() > 32)
            values_.remove(order_.takeFirst());
    }

private:
    QMutex                                      mutex_;
    QHash<QString, EvaluationThresholdSearchResult> values_;
    QStringList                                 order_;
};

ThresholdSearchCache &thresholdSearchCache()
{
    static ThresholdSearchCache cache;
    return cache;
}

QString thresholdSearchCacheKey(const ModelEvaluationOptions &options,
                                const QMap<qint64, EvaluationImageData> &images)
{
    QByteArray payload;
    const auto append = [&payload](const QByteArray &value)
    {
        payload.append(value);
        payload.append('\0');
    };
    const auto appendDouble = [&append](const double value)
    {
        append(QByteArray::number(value, 'g', 17));
    };
    const auto appendBox = [&appendDouble](const EvaluationBox &box)
    {
        appendDouble(box.x);
        appendDouble(box.y);
        appendDouble(box.w);
        appendDouble(box.h);
    };
    const auto appendGeometry = [&append](const QVariantMap &geometry)
    {
        append(QJsonDocument::fromVariant(geometry).toJson(QJsonDocument::Compact));
    };

    append(options.model_uuid.toUtf8());
    append(options.test_task_uuid.toUtf8());
    append(options.task_directory.toUtf8());
    append(options.project_database_path.toUtf8());
    append(options.dataset_file_list_path.toUtf8());
    append(options.task_database_path.toUtf8());
    append(options.prediction_dir.toUtf8());
    append(options.prediction_snapshot.toUtf8());
    append(QByteArray::number(static_cast<int>(options.method)));
    appendDouble(options.iou_threshold);
    append(QByteArray::number(static_cast<int>(options.matching_strategy)));

    for (auto image_it = images.cbegin(); image_it != images.cend(); ++image_it)
    {
        const EvaluationImageData &image = image_it.value();
        append(QByteArray::number(image.id));
        append(QByteArray::number(image.dataset_id));
        append(image.path.toUtf8());
        append(QByteArray::number(image.width));
        append(QByteArray::number(image.height));
        for (const EvaluationGroundTruthData &ground_truth : image.gt)
        {
            append(QByteArray::number(ground_truth.label_id));
            append(QByteArray::number(ground_truth.class_id));
            append(ground_truth.class_name.toUtf8());
            appendBox(ground_truth.box);
            appendGeometry(ground_truth.geometry);
            appendGeometry(ground_truth.bounds);
        }
        for (const EvaluationPredictionData &prediction : image.predictions)
        {
            append(prediction.prediction_id.toUtf8());
            append(QByteArray::number(prediction.image_id));
            append(QByteArray::number(prediction.class_id));
            append(prediction.class_name.toUtf8());
            appendDouble(prediction.score);
            appendBox(prediction.box);
            appendGeometry(prediction.geometry);
            appendGeometry(prediction.bounds);
        }
        if (image.anomaly_score_map != nullptr)
        {
            append(QByteArray::number(image.anomaly_score_map->width));
            append(QByteArray::number(image.anomaly_score_map->height));
            append(QByteArray::number(image.anomaly_score_map->values.size()));
            for (const double value : image.anomaly_score_map->values)
            {
                if (std::isfinite(value))
                    appendDouble(value);
                else if (std::isnan(value))
                    append(QByteArrayLiteral("nan"));
                else
                    append(value > 0.0 ? QByteArrayLiteral("positive-infinity")
                                      : QByteArrayLiteral("negative-infinity"));
            }
        }
        else
            append(QByteArrayLiteral("no-score-map"));
    }
    return QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
}

} // namespace

bool IEvaluationEngine::cancelled(const std::shared_ptr<std::atomic_bool> &cancel_token) const
{
    return cancel_token != nullptr && cancel_token->load(std::memory_order_relaxed);
}

void IEvaluationEngine::buildClasses(const QMap<qint64, EvaluationImageData> &, QMap<int, QString> &)
{
    // 默认空实现：子类按方法填充类别目录。
}

bool IEvaluationEngine::collectThresholdSearchData(const QMap<qint64, EvaluationImageData> &, QVector<double> &,
                                                   qint64 &, QString *)
{
    return false;
}

bool IEvaluationEngine::hasImageLevelStats() const
{
    return false;
}

void IEvaluationEngine::resetComputationScratch(const double threshold, const bool collect_events)
{
    scratch_.matrix.clear();
    scratch_.events.clear();
    scratch_.per_class.clear();
    scratch_.overall       = {};
    scratch_.image_counts  = {};
    scratch_.confidence    = threshold;
    scratch_.collect_events = collect_events;
}

EvaluationCounts IEvaluationEngine::thresholdSearchCounts() const
{
    return evaluation::isAnomaly(method()) ? scratch_.image_counts : scratch_.overall;
}

bool IEvaluationEngine::evaluate(const ModelEvaluationOptions &options, EvaluationResult *result, QString *err_msg)
{
    const auto fail = [err_msg](const QString &message)
    {
        if (err_msg)
            *err_msg = message;
        return false;
    };

    if (err_msg != nullptr)
        err_msg->clear();

    // (a) 协作取消检查。
    if (cancelled(options.cancel_token))
        return fail(QString("评估已取消"));

    // (b) 评估路径完整性校验。
    if (options.project_database_path.isEmpty() || options.dataset_file_list_path.isEmpty()
        || options.task_database_path.isEmpty() || options.prediction_dir.isEmpty())
        return fail(QString("评估路径参数不完整"));

    // (c) 加载图像与真值。
    QMap<qint64, EvaluationImageData> images;
    QMap<int, QString>                global_class_catalog;
    QMap<int, QString>                global_class_colors;
    int                               missing_database_images  = 0;
    int                               ignored_selection_images = 0;
    if (!loadEvaluationImages(options.dataset_file_list_path, options.project_database_path, options.task_database_path,
                              method(), images, options.cancel_token, err_msg, &missing_database_images,
                              &ignored_selection_images, options.image_dimensions_provider, &global_class_catalog,
                              &global_class_colors))
        return false;
    if (missing_database_images > 0)
        spdlog::warn("测试任务文件列表中有 {} 个图像已不在当前项目数据库中，已跳过", missing_database_images);
    if (ignored_selection_images > 0)
        spdlog::warn("测试任务文件列表中有 {} 个图像不属于当前数据集或类别选择，已跳过", ignored_selection_images);

    // (d) 剔除源文件不存在的图像。
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

    // (e) 加载预测。
    int prediction_count         = 0;
    int ignored_prediction_count = 0;
    if (!loadEvaluationPredictions(options.task_database_path, options.prediction_dir, images,
                                   evaluation::isAnomaly(method()), &prediction_count, options.cancel_token, err_msg,
                                   &ignored_prediction_count))
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

    // (f) 取消检查。
    if (cancelled(options.cancel_token))
        return fail(QString("评估已取消"));

    // 重置子类共享暂存区，并把钩子需要的输入标量暂存其中。
    scratch_                         = ComputeScratch{};
    scratch_.dataset_root            = dataset_root;
    scratch_.prediction_root         = options.prediction_dir;
    scratch_.confidence              = options.confidence_threshold;
    scratch_.iou                     = options.iou_threshold;
    scratch_.matching_strategy       = options.matching_strategy;
    scratch_.preprocessing_config    = options.preprocessing_config;
    scratch_.cancel_token            = options.cancel_token;
    scratch_.collect_events          = true;

    QMap<int, QString> classes = global_class_catalog;
    // The anomaly engine also contributes the implicit Good category used by
    // its image-level matrix.  Instance-matching engines use the same hook to
    // fill categories that are present only in predictions or labels.
    buildClasses(images, classes);
    classes.remove(-1);

    // 先遍历所有真实分数切分点。专用搜索器只预计算一次几何关系，并在
    // 同一轮扫描中产出全局 micro 与各类别曲线，避免逐阈值重复执行正式
    // 评估和随后再次计算 PR 曲线。
    QVector<double> threshold_scores;
    qint64           positive_ground_truth_count = 0;
    QString          threshold_error;
    const bool threshold_data_collected
        = collectThresholdSearchData(images, threshold_scores, positive_ground_truth_count, &threshold_error);
    if (!threshold_data_collected)
    {
        if (cancelled(options.cancel_token))
            return fail(QStringLiteral("评估已取消"));
        if (!threshold_error.isEmpty())
            return fail(threshold_error);
    }
    else
    {
        const QString threshold_cache_key = thresholdSearchCacheKey(options, images);
        if (!thresholdSearchCache().find(threshold_cache_key, scratch_.threshold_search))
        {
            QList<EvaluationImageData> threshold_images;
            threshold_images.reserve(images.size());
            for (const EvaluationImageData &image : images)
                threshold_images.push_back(image);
            if (evaluation::isAnomaly(method()))
                scratch_.threshold_search
                    = searchAnomalyThresholdForImages(threshold_images, options.cancel_token, &threshold_error);
            else
                scratch_.threshold_search = searchInstanceThresholdForImages(
                    threshold_images, options.iou_threshold, options.matching_strategy, {}, options.cancel_token,
                    &threshold_error);
            if (!cancelled(options.cancel_token) && threshold_error.isEmpty())
                thresholdSearchCache().insert(threshold_cache_key, scratch_.threshold_search);
        }
        if (cancelled(options.cancel_token))
            return fail(QString("评估已取消"));
        if (!threshold_error.isEmpty())
            return fail(threshold_error);
    }

    const double effective_threshold
        = options.apply_best_threshold && scratch_.threshold_search.available
            ? scratch_.threshold_search.best_point.threshold
            : options.confidence_threshold;
    resetComputationScratch(effective_threshold, true);

    // (h) 实例级计数。
    if (!computeInstanceCounts(images, classes, scratch_.per_class, scratch_.overall, err_msg))
        return false;
    // (i) 图像级计数。
    if (!computeImageCounts(images, scratch_.image_counts, err_msg))
        return false;
    // (j) 实例事件。
    if (!buildEvents(images, scratch_.events, err_msg))
        return false;
    // (k) 图表与图表类型。
    const QList<QVariantMap> charts      = buildCharts(images, classes, scratch_.overall, scratch_.image_counts,
                                                       scratch_.per_class, scratch_.matrix, scratch_.events, err_msg);
    const QStringList        chart_kinds = chartKinds();
    // (l) 混淆矩阵。
    QVector<EvaluationConfusionCell> matrix_cells;
    if (hasConfusionMatrix())
        matrix_cells = buildConfusionMatrix(classes, scratch_.matrix);

    if (cancelled(options.cancel_token))
        return fail(QString("评估已取消"));

    // (m) 组装强类型结果。
    EvaluationResult output;
    output.method           = method();
    output.images           = images;
    output.class_catalog    = classes;
    output.class_colors     = global_class_colors;
    output.per_class        = scratch_.per_class;
    output.overall          = scratch_.overall;
    output.image_counts     = scratch_.image_counts;
    output.matrix_cells     = std::move(matrix_cells);
    output.matrix           = scratch_.matrix;
    output.instance_records = scratch_.events;
    output.prediction_count = prediction_count;

    output.has_confusion_matrix = evaluation::hasConfusionMatrix(method());
    output.has_instance_metrics = evaluation::hasInstanceMetrics(method());
    output.has_image_metrics    = evaluation::hasImageMetrics(method());
    output.has_instance_events  = evaluation::hasInstanceEvents(method());

    output.charts      = charts;
    output.chart_kinds = chart_kinds;
    output.official_metrics        = scratch_.official_metrics;
    output.image_metric_definition = scratch_.image_metric_definition;

    output.confidence_threshold = effective_threshold;
    output.iou_threshold        = options.iou_threshold;
    output.matching_strategy    = options.matching_strategy;
    output.threshold_search     = scratch_.threshold_search;
    QVariantMap effective_config = options.evaluation_config;
    effective_config.insert(evaluation::isAnomaly(method()) ? QStringLiteral("classification_threshold")
                                                              : QStringLiteral("conf"),
                            effective_threshold);
    output.evaluation_config = evaluation::normalizedEvaluationConfig(effective_config);

    if (result)
        *result = std::move(output);
    return true;
}

} // namespace dltool::model
