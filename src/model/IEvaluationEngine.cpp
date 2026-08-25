#include "model/IEvaluationEngine.h"

#include "model/EvaluationCommon.h"
#include "model/EvaluationDataset.h"
#include "model/EvaluationResult.h"
#include "model/ModelEvaluationOptions.h"

#include <spdlog/spdlog.h>

#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <atomic>
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

} // namespace

bool IEvaluationEngine::cancelled(const std::shared_ptr<std::atomic_bool> &cancel_token) const
{
    return cancel_token != nullptr && cancel_token->load(std::memory_order_relaxed);
}

void IEvaluationEngine::buildClasses(const QMap<qint64, EvaluationImageData> &, QMap<int, QString> &)
{
    // 默认空实现：子类按方法填充类别目录。
}

bool IEvaluationEngine::hasImageLevelStats() const
{
    return false;
}

bool IEvaluationEngine::evaluate(const ModelEvaluationOptions &options, EvaluationResult *result, QString *err_msg)
{
    const auto fail = [err_msg](const QString &message)
    {
        if (err_msg)
            *err_msg = message;
        return false;
    };

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
    scratch_                   = ComputeScratch{};
    scratch_.dataset_root      = dataset_root;
    scratch_.prediction_root   = options.prediction_dir;
    scratch_.confidence        = options.confidence_threshold;
    scratch_.iou               = options.iou_threshold;
    scratch_.matching_strategy = options.matching_strategy;
    scratch_.preprocessing_config = options.preprocessing_config;
    scratch_.cancel_token      = options.cancel_token;

    QMap<int, QString> classes = global_class_catalog;
    // The anomaly engine also contributes the implicit Good category used by
    // its image-level matrix.  Instance-matching engines use the same hook to
    // fill categories that are present only in predictions or labels.
    buildClasses(images, classes);
    classes.remove(-1);

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
    output.event_maps.reserve(scratch_.events.size());
    for (const EvaluationInstanceRecord &record : scratch_.events)
        output.event_maps.push_back(instanceToProtocolMap(record));
    output.prediction_count = prediction_count;

    output.has_confusion_matrix = evaluation::hasConfusionMatrix(method());
    output.has_instance_metrics = evaluation::hasInstanceMetrics(method());
    output.has_image_metrics    = evaluation::hasImageMetrics(method());
    output.has_instance_events  = evaluation::hasInstanceEvents(method());

    output.charts      = charts;
    output.chart_kinds = chart_kinds;

    output.confidence_threshold = options.confidence_threshold;
    output.iou_threshold        = options.iou_threshold;
    output.matching_strategy    = options.matching_strategy;
    output.evaluation_config    = options.evaluation_config;

    if (result)
        *result = std::move(output);
    return true;
}

} // namespace dltool::model
