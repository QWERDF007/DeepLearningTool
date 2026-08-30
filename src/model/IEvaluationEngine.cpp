#include "model/IEvaluationEngine.h"

#include "model/EvaluationCommon.h"
#include "model/EvaluationDataset.h"
#include "model/EvaluationResult.h"
#include "model/ModelEvaluationOptions.h"

#include <spdlog/spdlog.h>

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
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

QString thresholdSearchCacheKey(const ModelEvaluationOptions &options)
{
    if (options.prediction_snapshot.trimmed().isEmpty())
        return {};

    // prediction_snapshot already identifies the project/task input and every
    // prediction artifact.  Do not serialize EvaluationImageData here: for
    // anomaly detection that would turn every TIFF pixel into cache-key work,
    // although threshold search only consumes the image-level maximum score.
    return QStringLiteral("%1|method=%2|iou=%3|matching=%4")
        .arg(options.prediction_snapshot)
        .arg(static_cast<int>(options.method))
        .arg(QString::number(options.iou_threshold, 'g', 17))
        .arg(static_cast<int>(options.matching_strategy));
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

bool IEvaluationEngine::supportsThresholdSearch() const
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
    QElapsedTimer total_timer;
    total_timer.start();
    QElapsedTimer phase_timer;
    phase_timer.start();
    const auto fail = [&err_msg, &options, &total_timer](const QString &message)
    {
        if (err_msg)
            *err_msg = message;
        spdlog::debug("[评估耗时] 任务 {} 失败，总耗时 {} ms: {}", options.test_task_uuid.toUtf8().constData(),
                      total_timer.elapsed(), message.toUtf8().constData());
        return false;
    };

    if (err_msg != nullptr)
        err_msg->clear();

    spdlog::debug("[评估耗时] 任务 {} 开始: method={}, file_list={}, prediction_dir={}, confidence={}, iou={}",
                  options.test_task_uuid.toUtf8().constData(), static_cast<int>(options.method),
                  options.dataset_file_list_path.toUtf8().constData(), options.prediction_dir.toUtf8().constData(),
                  options.confidence_threshold, options.iou_threshold);

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
    spdlog::debug("[评估耗时] 任务 {} 阶段 load-images 完成: {} ms, images={}, classes={}, missing_database={}, "
                  "ignored_selection={}",
                  options.test_task_uuid.toUtf8().constData(), phase_timer.elapsed(), images.size(),
                  global_class_catalog.size(), missing_database_images, ignored_selection_images);
    phase_timer.restart();
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
    spdlog::debug("[评估耗时] 任务 {} 阶段 source-validation 完成: {} ms, images={}, missing_source={}",
                  options.test_task_uuid.toUtf8().constData(), phase_timer.elapsed(), images.size(),
                  missing_source_images);
    phase_timer.restart();

    // (e) 加载预测。
    int prediction_count         = 0;
    int ignored_prediction_count = 0;
    if (!loadEvaluationPredictions(options.task_database_path, options.prediction_dir, images,
                                   evaluation::isAnomaly(method()), &prediction_count, options.cancel_token, err_msg,
                                   &ignored_prediction_count, false,
                                   evaluation::isAnomaly(method()) ? options.confidence_threshold
                                                                    : std::numeric_limits<double>::quiet_NaN()))
        return false;
    spdlog::debug("[评估耗时] 任务 {} 阶段 load-predictions 完成: {} ms, images={}, predictions={}, "
                  "ignored_predictions={}",
                  options.test_task_uuid.toUtf8().constData(), phase_timer.elapsed(), images.size(), prediction_count,
                  ignored_prediction_count);
    phase_timer.restart();
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
    scratch_.image_dimensions_provider = options.image_dimensions_provider;
    scratch_.collect_events          = true;

    QMap<int, QString> classes = global_class_catalog;
    // The anomaly engine also contributes the implicit Good category used by
    // its image-level matrix.  Instance-matching engines use the same hook to
    // fill categories that are present only in predictions or labels.
    buildClasses(images, classes);
    classes.remove(-1);

    QString threshold_error;
    const bool threshold_search_supported = supportsThresholdSearch();
    if (!threshold_search_supported)
    {
        if (cancelled(options.cancel_token))
            return fail(QStringLiteral("评估已取消"));
    }
    else
    {
        const QString threshold_cache_key = thresholdSearchCacheKey(options);
        const bool threshold_cacheable = !threshold_cache_key.isEmpty();
        const bool threshold_cache_hit
            = threshold_cacheable && thresholdSearchCache().find(threshold_cache_key, scratch_.threshold_search);
        spdlog::debug("[评估耗时] 任务 {} 阶段 threshold-cache 完成: {} ms, hit={}, key={}",
                      options.test_task_uuid.toUtf8().constData(), phase_timer.elapsed(), threshold_cache_hit,
                      threshold_cacheable ? threshold_cache_key.toUtf8().constData() : "<disabled:no-snapshot>");
        phase_timer.restart();
        if (!threshold_cache_hit)
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
            if (threshold_cacheable && !cancelled(options.cancel_token) && threshold_error.isEmpty())
                thresholdSearchCache().insert(threshold_cache_key, scratch_.threshold_search);
        }
        spdlog::debug("[评估耗时] 任务 {} 阶段 threshold-search 完成: {} ms, available={}, points={}, "
                      "best_threshold={}, best_f1={}",
                      options.test_task_uuid.toUtf8().constData(), phase_timer.elapsed(),
                      scratch_.threshold_search.available, scratch_.threshold_search.points.size(),
                      scratch_.threshold_search.best_point.threshold, scratch_.threshold_search.best_point.f1);
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
    phase_timer.restart();

    // (h) 实例级计数。
    if (!computeInstanceCounts(images, classes, scratch_.per_class, scratch_.overall, err_msg))
        return false;
    spdlog::debug("[评估耗时] 任务 {} 阶段 instance-counts 完成: {} ms, tp={}, fp={}, fn={}",
                  options.test_task_uuid.toUtf8().constData(), phase_timer.elapsed(), scratch_.overall.tp,
                  scratch_.overall.fp, scratch_.overall.fn);
    phase_timer.restart();
    // (i) 图像级计数。
    if (!computeImageCounts(images, scratch_.image_counts, err_msg))
        return false;
    spdlog::debug("[评估耗时] 任务 {} 阶段 image-counts 完成: {} ms, tp={}, fp={}, fn={}",
                  options.test_task_uuid.toUtf8().constData(), phase_timer.elapsed(), scratch_.image_counts.tp,
                  scratch_.image_counts.fp, scratch_.image_counts.fn);
    phase_timer.restart();
    // (j) 实例事件。
    if (!buildEvents(images, scratch_.events, err_msg))
        return false;
    spdlog::debug("[评估耗时] 任务 {} 阶段 events 完成: {} ms, count={}", options.test_task_uuid.toUtf8().constData(),
                  phase_timer.elapsed(), scratch_.events.size());
    phase_timer.restart();
    // (k) 图表与图表类型。
    const QList<QVariantMap> charts      = buildCharts(images, classes, scratch_.overall, scratch_.image_counts,
                                                       scratch_.per_class, scratch_.matrix, scratch_.events, err_msg);
    const QStringList        chart_kinds = chartKinds();
    spdlog::debug("[评估耗时] 任务 {} 阶段 charts 完成: {} ms, charts={}", options.test_task_uuid.toUtf8().constData(),
                  phase_timer.elapsed(), charts.size());
    phase_timer.restart();
    // (l) 混淆矩阵。
    QVector<EvaluationConfusionCell> matrix_cells;
    if (hasConfusionMatrix())
        matrix_cells = buildConfusionMatrix(classes, scratch_.matrix);
    spdlog::debug("[评估耗时] 任务 {} 阶段 confusion-matrix 完成: {} ms, cells={}",
                  options.test_task_uuid.toUtf8().constData(), phase_timer.elapsed(), matrix_cells.size());
    phase_timer.restart();

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
    spdlog::debug("[评估耗时] 任务 {} 完成: total={} ms, images={}, predictions={}, events={}, charts={}",
                  options.test_task_uuid.toUtf8().constData(), total_timer.elapsed(), images.size(), prediction_count,
                  scratch_.events.size(), charts.size());
    return true;
}

} // namespace dltool::model
