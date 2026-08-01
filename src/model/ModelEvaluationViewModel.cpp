#include "model/ModelEvaluationViewModel.h"
#include "model/ModelEvaluationService.h"

#include "common/YamlUtils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QVariantList>
#include <QMap>
#include <QSet>
#include <QMetaObject>
#include <QThreadPool>
#include <QPointer>
#include <QUrl>
#include <QUrlQuery>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <cmath>
#include <limits>
#include <utility>

namespace dltool::model {

namespace {

constexpr qint64 kMaxEvaluationYamlBytes = 256LL * 1024LL * 1024LL;
constexpr std::size_t kMaxEvaluationRecords = 5'000'000;

QString textValue(const QVariantMap &map, const QString &name, const QString &fallback = {})
{
    const QString value = map.value(name).toString();
    return value.isEmpty() ? fallback : value;
}

QString statusDisplayText(const QString &status)
{
    if (status == QStringLiteral("true_positive"))
        return QString("正确匹配");
    if (status == QStringLiteral("class_mismatch"))
        return QString("类别错误");
    if (status == QStringLiteral("false_positive"))
        return QString("误检");
    if (status == QStringLiteral("false_negative"))
        return QString("漏检");
    if (status == QStringLiteral("ignored"))
        return QString("已忽略");
    return status;
}

int intValue(const QVariantMap &map, const QString &name, const int fallback = -1)
{
    bool ok = false;
    const int value = map.value(name).toInt(&ok);
    return ok ? value : fallback;
}

qint64 longValue(const QVariantMap &map, const QString &name, const qint64 fallback = 0)
{
    bool ok = false;
    const qint64 value = map.value(name).toLongLong(&ok);
    return ok ? value : fallback;
}

double realValue(const QVariantMap &map, const QString &name, const double fallback = 0.0)
{
    bool ok = false;
    const double value = map.value(name).toDouble(&ok);
    return ok ? value : fallback;
}

bool pathWithin(const QString &root, const QString &path)
{
    const QString clean_root = QDir::fromNativeSeparators(QFileInfo(root).absoluteFilePath());
    const QString clean_path = QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath());
    return !clean_root.isEmpty() && !clean_path.isEmpty()
        && (clean_path.compare(clean_root, Qt::CaseInsensitive) == 0
            || clean_path.startsWith(clean_root + QLatin1Char('/'), Qt::CaseInsensitive));
}

QString resolveReportReference(const QString &base_dir, const QString &value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty())
        return {};
    const QFileInfo info(trimmed);
    return info.isAbsolute() ? info.absoluteFilePath() : QDir(base_dir).filePath(trimmed);
}

void requireReportField(const QVariantMap &root, const QString &name)
{
    if (!root.contains(name))
        throw std::runtime_error(QString("评估报告缺少字段: %1").arg(name).toUtf8().constData());
}

void validateReportProtocol(const QVariantMap &root, const QFileInfo &report_file)
{
    if (report_file.size() > kMaxEvaluationYamlBytes)
        throw std::runtime_error("评估报告超过大小限制");
    const QVariant version = root.value(QStringLiteral("schema_version"));
    bool version_ok = false;
    const int schema_version = version.toInt(&version_ok);
    if (!version_ok || schema_version != 2)
        throw std::runtime_error("评估报告 schema_version 必须为 2");
    for (const QString &field : {QStringLiteral("model_uuid"), QStringLiteral("test_task_uuid"),
                                 QStringLiteral("method"), QStringLiteral("primary_metric_set"),
                                 QStringLiteral("evaluation_digest"), QStringLiteral("evaluation_config"),
                                 QStringLiteral("capabilities"), QStringLiteral("diagnostic_metrics"),
                                  QStringLiteral("confusion_matrix"), QStringLiteral("charts"),
                                  QStringLiteral("image_records"), QStringLiteral("dataset_manifest"),
                                  QStringLiteral("event_count")})
        requireReportField(root, field);
    if (root.value(QStringLiteral("model_uuid")).toString().trimmed().isEmpty()
        || root.value(QStringLiteral("test_task_uuid")).toString().trimmed().isEmpty()
        || root.value(QStringLiteral("method")).toString().trimmed().isEmpty())
        throw std::runtime_error("评估报告 model_uuid/test_task_uuid/method 无效");
    if (root.value(QStringLiteral("evaluation_config")).toMap().isEmpty()
        || root.value(QStringLiteral("capabilities")).toMap().isEmpty())
        throw std::runtime_error("评估报告 evaluation_config/capabilities 无效");
    if (root.value(QStringLiteral("image_records")).toList().size()
        > static_cast<int>(kMaxEvaluationRecords))
        throw std::runtime_error("评估报告 image_records 数量超过限制");

    const QString evaluation_dir = report_file.absolutePath();
    const QString task_root = QFileInfo(evaluation_dir).absoluteDir().absolutePath();
    const QString instances = resolveReportReference(
        evaluation_dir, root.value(QStringLiteral("instances_file")).toString().isEmpty()
            ? QStringLiteral("instances.yaml") : root.value(QStringLiteral("instances_file")).toString());
    if (instances.isEmpty() || !pathWithin(evaluation_dir, instances))
        throw std::runtime_error("评估报告 instances_file 路径越界");

    for (const auto &reference : {std::pair<QString, QString>(QStringLiteral("prediction_manifest"),
                                                               QDir(task_root).filePath(QStringLiteral("pred/manifest.yaml"))),
                                  std::pair<QString, QString>(QStringLiteral("image_list"),
                                                               QDir(task_root).filePath(QStringLiteral("pred/images.txt"))),
                                  std::pair<QString, QString>(QStringLiteral("dataset_manifest"), QString())})
    {
        const QString configured = root.value(reference.first).toString();
        if (reference.first == QStringLiteral("dataset_manifest") && configured.trimmed().isEmpty())
            throw std::runtime_error("评估报告缺少 dataset_manifest");
        const QString target = configured.isEmpty() ? reference.second : resolveReportReference(evaluation_dir, configured);
        if (target.isEmpty() || !pathWithin(task_root, target))
            throw std::runtime_error(QString("评估报告 %1 路径越界").arg(reference.first).toUtf8().constData());
        if (reference.first != QStringLiteral("dataset_manifest")
            && QFileInfo(target).absoluteFilePath().compare(QFileInfo(reference.second).absoluteFilePath(),
                                                            Qt::CaseInsensitive) != 0)
            throw std::runtime_error(QString("评估报告 %1 未指向当前测试产物").arg(reference.first).toUtf8().constData());
        if (!QFileInfo::exists(target) || !QFileInfo(target).isFile())
            throw std::runtime_error(QString("评估报告引用文件不存在: %1").arg(target).toUtf8().constData());
    }

    const QString prediction_config = QDir(task_root).filePath(QStringLiteral("pred/config.yaml"));
    const QString evaluation_config = QDir(evaluation_dir).filePath(QStringLiteral("config.yaml"));
    if (!QFileInfo::exists(prediction_config) || !QFileInfo(prediction_config).isFile()
        || !QFileInfo::exists(evaluation_config) || !QFileInfo(evaluation_config).isFile())
        throw std::runtime_error("评估报告缺少 pred/config.yaml 或 evaluation/config.yaml");
}

QString derivedResultPath(const QFileInfo &report_file)
{
    const QDir evaluation_dir = report_file.absoluteDir();
    const QDir task_dir = QFileInfo(evaluation_dir.absolutePath()).absoluteDir();
    return task_dir.filePath(QStringLiteral("result.yaml"));
}

QString resolveTaskReference(const QFileInfo &result_file, const QString &value, const QString &fallback)
{
    const QString candidate = value.trimmed().isEmpty() ? fallback : value.trimmed();
    if (candidate.isEmpty())
        return {};
    const QFileInfo info(candidate);
    return info.isAbsolute() ? info.absoluteFilePath() : result_file.absoluteDir().filePath(candidate);
}

QString fileDigest(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd())
    {
        const QByteArray chunk = file.read(1024 * 1024);
        if (chunk.isEmpty() && !file.atEnd())
            return {};
        hash.addData(chunk);
    }
    return QString("sha256:%1").arg(QString::fromLatin1(hash.result().toHex()));
}

void validateResultProtocol(const QVariantMap &result, const QFileInfo &result_file, const QFileInfo &report_file,
                            bool *inference_outdated, bool *evaluation_outdated)
{
    if (result.value(QStringLiteral("schema_version")).toInt() != 1
        || result.value(QStringLiteral("status")).toString() != QStringLiteral("finished")
        || result.value(QStringLiteral("model_uuid")).toString().trimmed().isEmpty()
        || result.value(QStringLiteral("test_task_uuid")).toString().trimmed().isEmpty())
        throw std::runtime_error("测试结果摘要无效");

    const QString task_root = result_file.absoluteDir().absolutePath();
    const auto resolveInsideTask = [&result_file, &task_root](const QString &value, const QString &fallback)
    {
        const QString path = resolveTaskReference(result_file, value, fallback);
        if (path.isEmpty() || !pathWithin(task_root, path))
            throw std::runtime_error("测试结果引用路径越界");
        return path;
    };

    const QString prediction_config = resolveInsideTask({}, QStringLiteral("pred/config.yaml"));
    const QString prediction_dir = resolveInsideTask(result.value(QStringLiteral("prediction_dir")).toString(),
                                                      QStringLiteral("pred"));
    const QString prediction_images = resolveInsideTask(result.value(QStringLiteral("prediction_images")).toString(),
                                                         QStringLiteral("pred/images.txt"));
    const QString prediction_manifest = resolveInsideTask({}, QStringLiteral("pred/manifest.yaml"));
    const QString evaluation_config = resolveInsideTask({}, QStringLiteral("evaluation/config.yaml"));
    const QString configured_report = resolveInsideTask(result.value(QStringLiteral("evaluation_report")).toString(),
                                                        QStringLiteral("evaluation/report.yaml"));
    if (QFileInfo(configured_report).absoluteFilePath().compare(report_file.absoluteFilePath(), Qt::CaseInsensitive) != 0)
        throw std::runtime_error("测试结果 evaluation_report 与当前报告不一致");

    if (!QFileInfo(prediction_dir).isDir())
        throw std::runtime_error("测试结果 prediction_dir 不是目录");
    for (const QString &path : {prediction_config, prediction_images, prediction_manifest, evaluation_config,
                                configured_report})
        if (!QFileInfo::exists(path) || !QFileInfo(path).isFile())
            throw std::runtime_error(QString("测试结果引用文件不存在: %1").arg(path).toUtf8().constData());

    QVariantMap prediction;
    QVariantMap evaluation;
    QVariantMap report;
    try
    {
        prediction = common::yaml::nodeVariant(common::yaml::loadFile(QFileInfo(prediction_config))).toMap();
        evaluation = common::yaml::nodeVariant(common::yaml::loadFile(QFileInfo(evaluation_config))).toMap();
        report = common::yaml::nodeVariant(common::yaml::loadFile(report_file)).toMap();
    }
    catch (const std::exception &exception)
    {
        throw std::runtime_error(QString("测试结果配置无法读取: %1").arg(QString(exception.what())).toUtf8().constData());
    }
    const QString result_model = result.value(QStringLiteral("model_uuid")).toString().trimmed();
    const QString report_model = report.value(QStringLiteral("model_uuid")).toString().trimmed();
    const QString result_method = result.value(QStringLiteral("method")).toString().trimmed();
    const QString report_method = report.value(QStringLiteral("method")).toString().trimmed();
    const QString result_task = result.value(QStringLiteral("test_task_uuid")).toString().trimmed();
    const QString report_task = report.value(QStringLiteral("test_task_uuid")).toString().trimmed();
    const QString prediction_model = prediction.value(QStringLiteral("model_uuid")).toString().trimmed();
    const QString prediction_task = prediction.value(QStringLiteral("test_task_uuid")).toString().trimmed();
    const QString prediction_method = prediction.value(QStringLiteral("method")).toString().trimmed();
    const QString evaluation_model = evaluation.value(QStringLiteral("model_uuid")).toString().trimmed();
    const QString evaluation_task = evaluation.value(QStringLiteral("test_task_uuid")).toString().trimmed();
    const QString evaluation_method = evaluation.value(QStringLiteral("method")).toString().trimmed();
    if (result_model.isEmpty() || report_model.isEmpty() || result_model != report_model
        || (!prediction_model.isEmpty() && prediction_model != result_model)
        || (!evaluation_model.isEmpty() && evaluation_model != result_model)
        || result_method.isEmpty() || report_method.isEmpty() || result_method != report_method
        || prediction_method.isEmpty() || prediction_method != report_method
        || evaluation_method.isEmpty() || evaluation_method != report_method
        || report_task.isEmpty() || report_task != result_task
        || (!prediction_task.isEmpty() && prediction_task != result_task)
        || (!evaluation_task.isEmpty() && evaluation_task != result_task))
        throw std::runtime_error("测试结果 model_uuid/test_task_uuid/method 不一致");

    const QString dataset_manifest = resolveReportReference(
        report_file.absolutePath(), report.value(QStringLiteral("dataset_manifest")).toString());
    if (dataset_manifest.isEmpty() || !pathWithin(task_root, dataset_manifest)
        || !QFileInfo::exists(dataset_manifest) || !QFileInfo(dataset_manifest).isFile())
        throw std::runtime_error("测试结果 dataset_manifest 无效");

    QString prediction_error;
    if (!ModelEvaluationService::validatePrediction(prediction_images, prediction_manifest, nullptr, nullptr,
                                                    &prediction_error, result_model, result_task, report_method))
        throw std::runtime_error(QString("预测协议无效: %1").arg(prediction_error).toUtf8().constData());

    const QString checkpoint_value = prediction.value(QStringLiteral("checkpoint_path")).toString();
    if (!checkpoint_value.trimmed().isEmpty())
    {
        // A test task consumes the model checkpoint from the shared
        // train/weights directory.  The prediction config stores that path
        // relative to the test task (normally ../../train/weights/...), so
        // it is intentionally outside task_root but still inside the model's
        // controlled weights boundary.
        const QString model_root = QDir(task_root).filePath(QStringLiteral("../.."));
        const QString weights_root = QDir(model_root).filePath(QStringLiteral("train/weights"));
        const QString checkpoint = resolveTaskReference(result_file, checkpoint_value, {});
        if (!pathWithin(weights_root, checkpoint))
            throw std::runtime_error("测试结果 checkpoint 路径越界");
        if (!QFileInfo::exists(checkpoint) || !QFileInfo(checkpoint).isFile())
            throw std::runtime_error("测试结果 checkpoint 不存在");
        const QString actual_weight = fileDigest(checkpoint);
        if (!prediction.value(QStringLiteral("weight_digest")).toString().isEmpty()
            && actual_weight != prediction.value(QStringLiteral("weight_digest")).toString())
            throw std::runtime_error("测试结果 checkpoint 摘要不一致");
    }
    const QString result_inference = result.value(QStringLiteral("inference_digest")).toString();
    const QString prediction_inference = prediction.value(QStringLiteral("inference_digest")).toString();
    const QString report_inference = report.value(QStringLiteral("inference_digest")).toString();
    const QString result_input_data = result.value(QStringLiteral("input_data_digest")).toString();
    const QString prediction_input_data = prediction.value(QStringLiteral("input_data_digest")).toString();
    const QString evaluation_input_data = evaluation.value(QStringLiteral("input_data_digest")).toString();
    const QString report_input_data = report.value(QStringLiteral("input_data_digest")).toString();
    const QString result_evaluation = result.value(QStringLiteral("evaluation_digest")).toString();
    const QString evaluation_digest = evaluation.value(QStringLiteral("evaluation_digest")).toString();
    const QString report_evaluation = report.value(QStringLiteral("evaluation_digest")).toString();
    const QString result_weight = result.value(QStringLiteral("weight_digest")).toString();
    const QString prediction_weight = prediction.value(QStringLiteral("weight_digest")).toString();
    const QString report_weight = report.value(QStringLiteral("weight_digest")).toString();
    const QString result_gt = result.value(QStringLiteral("ground_truth_digest")).toString();
    const QString evaluation_gt = evaluation.value(QStringLiteral("ground_truth_digest")).toString();
    const QString report_gt = report.value(QStringLiteral("ground_truth_digest")).toString();
    const QString result_images = result.value(QStringLiteral("image_list_digest")).toString();
    const QString prediction_images_digest = prediction.value(QStringLiteral("image_list_digest")).toString();
    const QString evaluation_images = evaluation.value(QStringLiteral("image_list_digest")).toString();
    const QString report_images = report.value(QStringLiteral("image_list_digest")).toString();
    const QString current_images_digest = fileDigest(prediction_images);
    const QString current_dataset_digest = fileDigest(dataset_manifest);
    const bool image_list_changed = !prediction_images_digest.isEmpty()
        && prediction_images_digest != current_images_digest;
    const bool inference_changed = result_inference.isEmpty() || prediction_inference.isEmpty()
        || result_inference != prediction_inference || report_inference != prediction_inference
        || (!prediction_input_data.isEmpty() && result_input_data != prediction_input_data)
        || (!prediction_input_data.isEmpty() && evaluation_input_data != prediction_input_data)
        || (!prediction_input_data.isEmpty() && report_input_data != prediction_input_data)
        || result_weight != prediction_weight || (!report_weight.isEmpty() && report_weight != prediction_weight)
        || image_list_changed || (!result_images.isEmpty() && result_images != prediction_images_digest);
    const bool evaluation_changed = result_evaluation.isEmpty() || evaluation_digest.isEmpty()
        || result_evaluation != evaluation_digest || report_evaluation != evaluation_digest
        || (!result_gt.isEmpty() && result_gt != evaluation_gt)
        || (!report_gt.isEmpty() && report_gt != evaluation_gt)
        || (!evaluation_gt.isEmpty() && evaluation_gt != current_dataset_digest)
        || (!result_images.isEmpty() && result_images != evaluation_images)
        || (!report_images.isEmpty() && report_images != evaluation_images)
        || (!evaluation_images.isEmpty() && evaluation_images != current_images_digest);
    if (inference_outdated)
        *inference_outdated = inference_changed;
    if (evaluation_outdated)
        *evaluation_outdated = evaluation_changed;
}

EvaluationMetricRecord metricFromMap(const QString &key, const QVariantMap &map, const QString &fallback_label = {})
{
    EvaluationMetricRecord metric;
    metric.key = key;
    metric.label = fallback_label.isEmpty() ? key : fallback_label;
    metric.class_name = textValue(map, QStringLiteral("class_name"));
    metric.class_id = intValue(map, QStringLiteral("class_id"));
    metric.precision = realValue(map, QStringLiteral("precision"));
    metric.recall = realValue(map, QStringLiteral("recall"));
    metric.f1 = realValue(map, QStringLiteral("f1"));
    metric.tp = longValue(map, QStringLiteral("tp"));
    metric.fp = longValue(map, QStringLiteral("fp"));
    metric.fn = longValue(map, QStringLiteral("fn"));
    metric.precision_defined = map.contains(QStringLiteral("precision_defined"))
        ? map.value(QStringLiteral("precision_defined")).toBool() : metric.tp + metric.fp > 0;
    metric.recall_defined = map.contains(QStringLiteral("recall_defined"))
        ? map.value(QStringLiteral("recall_defined")).toBool() : metric.tp + metric.fn > 0;
    metric.f1_defined = map.contains(QStringLiteral("f1_defined"))
        ? map.value(QStringLiteral("f1_defined")).toBool() : metric.precision_defined && metric.recall_defined;
    return metric;
}

EvaluationInstanceRecord instanceFromMap(const QVariantMap &map)
{
    EvaluationInstanceRecord record;
    record.event_uuid = textValue(map, QStringLiteral("event_uuid"));
    record.image_id = longValue(map, QStringLiteral("image_id"), -1);
    record.dataset_id = longValue(map, QStringLiteral("dataset_id"), -1);
    record.image_name = textValue(map, QStringLiteral("image_name"));
    record.image_path = textValue(map, QStringLiteral("image_path"));
    record.image_width = intValue(map, QStringLiteral("image_width"), 0);
    record.image_height = intValue(map, QStringLiteral("image_height"), 0);
    record.status = textValue(map, QStringLiteral("status"));
    record.score = realValue(map, QStringLiteral("score"));
    record.iou = realValue(map, QStringLiteral("iou"));
    record.gt_bounds = map.value(QStringLiteral("gt")).toMap().value(QStringLiteral("bounds")).toMap();
    record.pred_bounds = map.value(QStringLiteral("pred")).toMap().value(QStringLiteral("bounds")).toMap();
    record.crop_bounds = map.value(QStringLiteral("crop_bounds")).toMap();
    record.gt_overlay_bounds = map.value(QStringLiteral("gt_overlay_bounds")).toMap();
    record.pred_overlay_bounds = map.value(QStringLiteral("pred_overlay_bounds")).toMap();
    const QVariantMap gt = map.value(QStringLiteral("gt")).toMap();
    const QVariantMap pred = map.value(QStringLiteral("pred")).toMap();
    record.gt_label_id = longValue(gt, QStringLiteral("label_id"), -1);
    record.gt_instance_id = textValue(gt, QStringLiteral("instance_id"));
    record.pred_instance_id = textValue(pred, QStringLiteral("instance_id"),
                                        textValue(pred, QStringLiteral("prediction_id")));
    record.gt_class_id = intValue(gt, QStringLiteral("class_id"));
    record.pred_class_id = intValue(pred, QStringLiteral("class_id"));
    record.gt_class = textValue(gt, QStringLiteral("class_name"));
    record.pred_class = textValue(pred, QStringLiteral("class_name"));
    record.gt_class_color = textValue(gt, QStringLiteral("class_color"));
    record.pred_class_color = textValue(pred, QStringLiteral("class_color"));
    record.gt_geometry = gt.value(QStringLiteral("geometry")).toMap();
    if (record.gt_geometry.isEmpty())
        record.gt_geometry = record.gt_bounds;
    record.pred_geometry = pred.value(QStringLiteral("geometry")).toMap();
    if (record.pred_geometry.isEmpty())
        record.pred_geometry = record.pred_bounds;
    record.gt_overlay_points = map.value(QStringLiteral("gt_overlay_points")).toList();
    record.pred_overlay_points = map.value(QStringLiteral("pred_overlay_points")).toList();
    record.gt_mask_url = textValue(map, QStringLiteral("gt_mask_url"));
    record.pred_mask_url = textValue(map, QStringLiteral("pred_mask_url"));
    if (record.image_name.isEmpty())
        record.image_name = QFileInfo(record.image_path).fileName();
    return record;
}

EvaluationImageRecord imageFromMap(const QVariantMap &map)
{
    EvaluationImageRecord record;
    record.image_id = longValue(map, QStringLiteral("image_id"), -1);
    record.dataset_id = longValue(map, QStringLiteral("dataset_id"), -1);
    record.image_name = textValue(map, QStringLiteral("image_name"));
    record.image_path = textValue(map, QStringLiteral("image_path"));
    record.image_width = intValue(map, QStringLiteral("image_width"), 0);
    record.image_height = intValue(map, QStringLiteral("image_height"), 0);
    for (const QVariant &value : map.value(QStringLiteral("gt_label_ids")).toList())
        record.gt_label_ids.push_back(value.toLongLong());
    for (const QVariant &value : map.value(QStringLiteral("gt_class_ids")).toList())
        record.gt_class_ids.push_back(value.toInt());
    for (const QVariant &value : map.value(QStringLiteral("pred_class_ids")).toList())
        record.pred_class_ids.push_back(value.toInt());
    record.score = realValue(map, QStringLiteral("score"));
    record.has_gt = map.value(QStringLiteral("has_gt")).toBool();
    record.has_pred = map.value(QStringLiteral("has_pred")).toBool();
    for (const QVariant &value : map.value(QStringLiteral("gt_instances")).toList())
    {
        const QVariantMap item = value.toMap();
        EvaluationGroundTruthRecord gt;
        gt.label_id = longValue(item, QStringLiteral("label_id"), -1);
        gt.class_id = intValue(item, QStringLiteral("class_id"), -1);
        gt.class_name = textValue(item, QStringLiteral("class_name"));
        gt.geometry = item.value(QStringLiteral("geometry")).toMap();
        gt.bounds = item.value(QStringLiteral("bounds")).toMap();
        record.gt_instances.push_back(std::move(gt));
    }
    for (const QVariant &value : map.value(QStringLiteral("predictions")).toList())
    {
        const QVariantMap item = value.toMap();
        EvaluationPredictionRecord prediction;
        prediction.prediction_id = textValue(item, QStringLiteral("prediction_id"));
        prediction.image_id = longValue(item, QStringLiteral("image_id"), record.image_id);
        prediction.dataset_id = longValue(item, QStringLiteral("dataset_id"), record.dataset_id);
        prediction.class_id = intValue(item, QStringLiteral("class_id"), -1);
        prediction.class_name = textValue(item, QStringLiteral("class_name"));
        prediction.score = realValue(item, QStringLiteral("score"));
        prediction.geometry = item.value(QStringLiteral("geometry")).toMap();
        prediction.bounds = item.value(QStringLiteral("bounds")).toMap();
        record.predictions.push_back(std::move(prediction));
    }
    if (record.image_name.isEmpty())
        record.image_name = QFileInfo(record.image_path).fileName();
    return record;
}

struct EvaluationAggregateInput
{
    QList<EvaluationInstanceRecord> instances;
    QList<EvaluationImageRecord> images;
    QMap<int, QString> class_catalog;
    QList<QVariantMap> chart_descriptors;
    QVariantList class_ids;
    double confidence_threshold{0.5};
    double iou_threshold{0.5};
    QString matching_strategy{QStringLiteral("greedy_iou")};
    bool has_instance_metrics{false};
    bool has_image_metrics{false};
    bool has_confusion_matrix{false};
};

struct EvaluationAggregateOutput
{
    std::vector<EvaluationMetricRecord> instance_metrics;
    std::vector<EvaluationMetricRecord> image_metrics;
    std::vector<EvaluationMetricRecord> per_class_metrics;
    std::vector<EvaluationConfusionCell> confusion;
    QList<QVariantMap> charts;
};

struct AggregateCounts
{
    qint64 tp{0};
    qint64 fp{0};
    qint64 fn{0};
};

struct AggregateBox
{
    double x{0.0};
    double y{0.0};
    double width{0.0};
    double height{0.0};
    bool valid{false};
};

AggregateBox aggregateBox(const QVariantMap &value)
{
    bool x_ok = false;
    bool y_ok = false;
    bool width_ok = false;
    bool height_ok = false;
    AggregateBox box;
    box.x = value.value(QStringLiteral("x")).toDouble(&x_ok);
    box.y = value.value(QStringLiteral("y")).toDouble(&y_ok);
    box.width = value.value(QStringLiteral("width")).toDouble(&width_ok);
    box.height = value.value(QStringLiteral("height")).toDouble(&height_ok);
    box.valid = x_ok && y_ok && width_ok && height_ok && box.width > 0.0 && box.height > 0.0;
    return box;
}

double aggregateIou(const QVariantMap &lhs, const QVariantMap &rhs)
{
    const AggregateBox a = aggregateBox(lhs);
    const AggregateBox b = aggregateBox(rhs);
    if (!a.valid && !b.valid)
        return 1.0;
    if (!a.valid || !b.valid)
        return 0.0;
    const double left = std::max(a.x, b.x);
    const double top = std::max(a.y, b.y);
    const double right = std::min(a.x + a.width, b.x + b.width);
    const double bottom = std::min(a.y + a.height, b.y + b.height);
    const double intersection = std::max(0.0, right - left) * std::max(0.0, bottom - top);
    const double area = a.width * a.height + b.width * b.height - intersection;
    return area > 0.0 ? intersection / area : 0.0;
}

struct AggregateMatch
{
    int prediction{-1};
    int ground_truth{-1};
    double iou{0.0};
};

QList<AggregateMatch> aggregateMatches(const QList<EvaluationPredictionRecord> &predictions,
                                        const QList<EvaluationGroundTruthRecord> &ground_truth,
                                        const double threshold, const QString &strategy)
{
    const QString normalized = strategy.trimmed().toLower();
    const bool use_hungarian = normalized == QStringLiteral("hungarian")
        || normalized == QStringLiteral("hungarian_iou");
    if (!use_hungarian)
    {
        struct Candidate
        {
            int prediction{-1};
            int ground_truth{-1};
            double iou{0.0};
        };
        QList<Candidate> candidates;
        for (int prediction = 0; prediction < predictions.size(); ++prediction)
            for (int gt = 0; gt < ground_truth.size(); ++gt)
            {
                const double overlap = aggregateIou(predictions.at(prediction).bounds, ground_truth.at(gt).bounds);
                if (overlap >= threshold)
                    candidates.push_back({prediction, gt, overlap});
            }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate &lhs, const Candidate &rhs)
                  {
                      if (lhs.iou != rhs.iou)
                          return lhs.iou > rhs.iou;
                      if (lhs.prediction != rhs.prediction)
                          return lhs.prediction < rhs.prediction;
                      return lhs.ground_truth < rhs.ground_truth;
                  });
        QVector<bool> used_prediction(predictions.size(), false);
        QVector<bool> used_gt(ground_truth.size(), false);
        QList<AggregateMatch> result;
        for (const Candidate &candidate : candidates)
        {
            if (used_prediction.at(candidate.prediction) || used_gt.at(candidate.ground_truth))
                continue;
            used_prediction[candidate.prediction] = true;
            used_gt[candidate.ground_truth] = true;
            result.push_back({candidate.prediction, candidate.ground_truth, candidate.iou});
        }
        return result;
    }

    const int size = std::max(predictions.size(), ground_truth.size());
    if (size <= 0)
        return {};
    QVector<QVector<double>> weight(size, QVector<double>(size, 0.0));
    for (int prediction = 0; prediction < predictions.size(); ++prediction)
        for (int gt = 0; gt < ground_truth.size(); ++gt)
        {
            const double overlap = aggregateIou(predictions.at(prediction).bounds, ground_truth.at(gt).bounds);
            if (overlap >= threshold)
                weight[prediction][gt] = overlap;
        }

    // Hungarian maximum-weight assignment.  Dummy rows/columns have zero
    // weight, so assignments below the IoU threshold remain unmatched.
    const int n = size;
    QVector<double> u(n + 1), v(n + 1);
    QVector<int> p(n + 1), way(n + 1);
    for (int row = 1; row <= n; ++row)
    {
        p[0] = row;
        int column0 = 0;
        QVector<double> minv(n + 1, std::numeric_limits<double>::max());
        QVector<bool> used(n + 1, false);
        do
        {
            used[column0] = true;
            const int row0 = p[column0];
            double delta = std::numeric_limits<double>::max();
            int column1 = 0;
            for (int column = 1; column <= n; ++column)
            {
                if (used[column])
                    continue;
                const double current = -weight[row0 - 1][column - 1] - u[row0] - v[column];
                if (current < minv[column])
                {
                    minv[column] = current;
                    way[column] = column0;
                }
                if (minv[column] < delta)
                {
                    delta = minv[column];
                    column1 = column;
                }
            }
            for (int column = 0; column <= n; ++column)
            {
                if (used[column])
                {
                    u[p[column]] += delta;
                    v[column] -= delta;
                }
                else
                    minv[column] -= delta;
            }
            column0 = column1;
        } while (p[column0] != 0);
        do
        {
            const int column1 = way[column0];
            p[column0] = p[column1];
            column0 = column1;
        } while (column0 != 0);
    }

    QList<AggregateMatch> result;
    for (int column = 1; column <= n; ++column)
    {
        const int prediction = p[column] - 1;
        const int gt = column - 1;
        if (prediction < 0 || prediction >= predictions.size() || gt < 0 || gt >= ground_truth.size())
            continue;
        const double overlap = weight[prediction][gt];
        if (overlap >= threshold)
            result.push_back({prediction, gt, overlap});
    }
    std::sort(result.begin(), result.end(), [](const AggregateMatch &lhs, const AggregateMatch &rhs)
              { return lhs.prediction < rhs.prediction; });
    return result;
}

EvaluationMetricRecord aggregateMetric(const QString &key, const QString &label, const int class_id,
                                       const AggregateCounts &counts)
{
    EvaluationMetricRecord value;
    value.key = key;
    value.label = label;
    value.class_name = label;
    value.class_id = class_id;
    value.tp = counts.tp;
    value.fp = counts.fp;
    value.fn = counts.fn;
    value.precision_defined = counts.tp + counts.fp > 0;
    value.recall_defined = counts.tp + counts.fn > 0;
    value.precision = value.precision_defined ? static_cast<double>(counts.tp) / (counts.tp + counts.fp) : 0.0;
    value.recall = value.recall_defined ? static_cast<double>(counts.tp) / (counts.tp + counts.fn) : 0.0;
    value.f1_defined = value.precision_defined && value.recall_defined && value.precision + value.recall > 0.0;
    value.f1 = value.f1_defined ? 2.0 * value.precision * value.recall / (value.precision + value.recall) : 0.0;
    return value;
}

bool aggregateClassAllowed(const QVariantList &class_ids, const int class_id)
{
    if (class_ids.isEmpty() || class_id < 0)
        return true;
    for (const QVariant &value : class_ids)
        if (value.toInt() == class_id)
            return true;
    return false;
}

AggregateCounts aggregateThresholdCounts(const QList<EvaluationImageRecord> &images, const QVariantList &class_ids,
                                          const double threshold, const double iou_threshold,
                                          const QString &matching_strategy)
{
    AggregateCounts counts;
    for (const EvaluationImageRecord &image : images)
    {
        QList<EvaluationPredictionRecord> predictions;
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
        std::sort(predictions.begin(), predictions.end(), [](const auto &lhs, const auto &rhs)
                  { return lhs.score > rhs.score; });
        const QList<AggregateMatch> matches
            = aggregateMatches(predictions, ground_truth, iou_threshold, matching_strategy);
        QVector<bool> used_prediction(predictions.size(), false);
        QVector<bool> used_gt(ground_truth.size(), false);
        for (const AggregateMatch &candidate : matches)
        {
            if (used_prediction.at(candidate.prediction) || used_gt.at(candidate.ground_truth))
                continue;
            used_prediction[candidate.prediction] = true;
            used_gt[candidate.ground_truth] = true;
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

EvaluationAggregateOutput aggregateEvaluation(const EvaluationAggregateInput &input)
{
    EvaluationAggregateOutput output;
    QMap<int, AggregateCounts> classes;
    QMap<int, QString> class_names = input.class_catalog;
    QMap<QString, qint64> matrix;
    AggregateCounts overall;
    for (const EvaluationInstanceRecord &record : input.instances)
    {
        if (record.gt_class_id >= 0)
            class_names.insert(record.gt_class_id,
                               record.gt_class.isEmpty() ? QString::number(record.gt_class_id) : record.gt_class);
        if (record.pred_class_id >= 0)
            class_names.insert(record.pred_class_id,
                               record.pred_class.isEmpty() ? QString::number(record.pred_class_id) : record.pred_class);
        if (record.status == QStringLiteral("true_positive"))
        {
            ++overall.tp;
            ++classes[record.pred_class_id].tp;
            ++matrix[QString::number(record.pred_class_id) + QLatin1Char('\x1f') + QString::number(record.gt_class_id)];
        }
        else if (record.status == QStringLiteral("class_mismatch"))
        {
            ++overall.fp;
            ++overall.fn;
            ++classes[record.pred_class_id].fp;
            ++classes[record.gt_class_id].fn;
            ++matrix[QString::number(record.pred_class_id) + QLatin1Char('\x1f') + QString::number(record.gt_class_id)];
        }
        else if (record.status == QStringLiteral("false_positive"))
        {
            ++overall.fp;
            ++classes[record.pred_class_id].fp;
            ++matrix[QString::number(record.pred_class_id) + QLatin1Char('\x1f') + QStringLiteral("FP")];
        }
        else if (record.status == QStringLiteral("false_negative"))
        {
            ++overall.fn;
            ++classes[record.gt_class_id].fn;
            ++matrix[QStringLiteral("FN") + QLatin1Char('\x1f') + QString::number(record.gt_class_id)];
        }
    }

    AggregateCounts image_counts;
    for (const EvaluationImageRecord &image : input.images)
    {
        QSet<int> gt_classes;
        QSet<int> pred_classes;
        for (const int class_id : image.gt_class_ids)
            if (aggregateClassAllowed(input.class_ids, class_id))
                gt_classes.insert(class_id);
        for (const int class_id : image.pred_class_ids)
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
            const bool has_gt = image.has_gt && input.class_ids.isEmpty();
            const bool has_pred = image.has_pred && input.class_ids.isEmpty();
            if (has_gt && has_pred)
                ++image_counts.tp;
            else if (has_pred)
                ++image_counts.fp;
            else if (has_gt)
                ++image_counts.fn;
        }
    }

    if (input.has_instance_metrics)
    {
        output.instance_metrics.push_back(aggregateMetric(QStringLiteral("overall"), QString("整体"), -1, overall));
        for (auto it = class_names.cbegin(); it != class_names.cend(); ++it)
            output.per_class_metrics.push_back(aggregateMetric(QString::number(it.key()), it.value(), it.key(),
                                                               classes.value(it.key())));
    }
    if (input.has_image_metrics)
        output.image_metrics.push_back(aggregateMetric(QStringLiteral("image"), QString("图像"), -1, image_counts));

    QMap<int, qint64> pred_totals;
    QMap<int, qint64> gt_totals;
    qint64 unmatched_fp = 0;
    qint64 unmatched_fn = 0;
    qint64 matched_pairs = 0;
    for (auto it = matrix.cbegin(); it != matrix.cend(); ++it)
    {
        const QList<QString> parts = it.key().split(QLatin1Char('\x1f'));
        if (parts.size() != 2)
            continue;
        if (parts.at(0) == QStringLiteral("FN"))
            unmatched_fn += it.value();
        else
            pred_totals[parts.at(0).toInt()] += it.value();
        if (parts.at(1) == QStringLiteral("FP"))
            unmatched_fp += it.value();
        else
        {
            gt_totals[parts.at(1).toInt()] += it.value();
            if (parts.at(0) != QStringLiteral("FN") && parts.at(1) != QStringLiteral("FP"))
                matched_pairs += it.value();
        }
    }
    const auto append_cell = [&output, &class_names](const QString &row, const QString &column, const qint64 count,
                                                       const QString &kind, const bool selectable,
                                                       const bool diagonal, const bool error)
    {
        const bool row_fn = row == QStringLiteral("FN");
        const bool row_total = row == QStringLiteral("TOTAL");
        const bool column_fp = column == QStringLiteral("FP");
        const bool column_total = column == QStringLiteral("TOTAL");
        const int row_id = row_fn || row_total ? -1 : row.toInt();
        const int column_id = column_fp || column_total ? -1 : column.toInt();
        const QString total = QString("合计");
        output.confusion.push_back({row, column,
                                    row_fn ? QStringLiteral("FN") : (row_total ? total : class_names.value(row_id)),
                                    column_fp ? QStringLiteral("FP")
                                              : (column_total ? total : class_names.value(column_id)),
                                    count, row_id, column_id, kind, QString(), selectable, diagonal, error});
    };
    if (input.has_confusion_matrix)
    {
        for (auto row_it = class_names.cbegin(); row_it != class_names.cend(); ++row_it)
        {
            const QString row = QString::number(row_it.key());
            for (auto column_it = class_names.cbegin(); column_it != class_names.cend(); ++column_it)
            {
                const QString column = QString::number(column_it.key());
                append_cell(row, column, matrix.value(row + QLatin1Char('\x1f') + column),
                            row_it.key() == column_it.key() ? QStringLiteral("match") : QStringLiteral("class_mismatch"),
                            true, row_it.key() == column_it.key(), row_it.key() != column_it.key());
            }
            append_cell(row, QStringLiteral("FP"), matrix.value(row + QLatin1Char('\x1f') + QStringLiteral("FP")),
                        QStringLiteral("false_positive"), true, false, true);
            append_cell(row, QStringLiteral("TOTAL"), pred_totals.value(row_it.key()), QStringLiteral("pred_total"),
                        true, false, false);
        }
        for (auto column_it = class_names.cbegin(); column_it != class_names.cend(); ++column_it)
        {
            const QString column = QString::number(column_it.key());
            append_cell(QStringLiteral("FN"), column,
                        matrix.value(QStringLiteral("FN") + QLatin1Char('\x1f') + column),
                        QStringLiteral("false_negative"), true, false, true);
        }
        append_cell(QStringLiteral("FN"), QStringLiteral("FP"), 0, QStringLiteral("not_applicable"), false, false, false);
        append_cell(QStringLiteral("FN"), QStringLiteral("TOTAL"), unmatched_fn,
                    QStringLiteral("false_negative_total"), true, false, true);
        for (auto column_it = class_names.cbegin(); column_it != class_names.cend(); ++column_it)
        {
            const QString column = QString::number(column_it.key());
            append_cell(QStringLiteral("TOTAL"), column, gt_totals.value(column_it.key()), QStringLiteral("gt_total"),
                        true, false, false);
        }
        append_cell(QStringLiteral("TOTAL"), QStringLiteral("FP"), unmatched_fp,
                    QStringLiteral("false_positive_total"), true, false, true);
        append_cell(QStringLiteral("TOTAL"), QStringLiteral("TOTAL"), matched_pairs + unmatched_fp + unmatched_fn,
                    QStringLiteral("all"), true, false, false);
    }

    if (input.has_instance_metrics)
    {
        QVariantList labels;
        QVariantList precision;
        QVariantList recall;
        QVariantList f1;
        for (const EvaluationMetricRecord &metric : output.per_class_metrics)
        {
            labels.push_back(metric.label);
            precision.push_back(metric.precision);
            recall.push_back(metric.recall);
            f1.push_back(metric.f1);
        }
        output.charts.push_back({{QStringLiteral("kind"), QStringLiteral("bar")},
                                 {QStringLiteral("chart_id"), QStringLiteral("per_class_metrics")},
                                 {QStringLiteral("filter_kind"), QStringLiteral("per_class_metrics")},
                                 {QStringLiteral("title"), QString("按类别指标")},
                                 {QStringLiteral("data"), QVariantMap{
                                     {QStringLiteral("labels"), labels},
                                     {QStringLiteral("datasets"), QVariantList{
                                         QVariantMap{{QStringLiteral("label"), QStringLiteral("Precision")},
                                                     {QStringLiteral("data"), precision}},
                                         QVariantMap{{QStringLiteral("label"), QStringLiteral("Recall")},
                                                     {QStringLiteral("data"), recall}},
                                         QVariantMap{{QStringLiteral("label"), QStringLiteral("F1")},
                                                     {QStringLiteral("data"), f1}}}}}},
                                 {QStringLiteral("options"), QVariantMap{{QStringLiteral("maintainAspectRatio"), false}}}});
    }
    for (const QVariantMap &descriptor : input.chart_descriptors)
    {
        const QString chart_id = descriptor.value(QStringLiteral("chart_id")).toString();
        const QString filter_kind = descriptor.value(QStringLiteral("filter_kind")).toString();
        if (descriptor.value(QStringLiteral("kind")).toString() == QStringLiteral("bar")
            && (filter_kind == QStringLiteral("per_class_metrics")
                || chart_id == QStringLiteral("per_class_metrics")))
            continue;
        QVariantMap filtered = descriptor;
        if (filter_kind == QStringLiteral("precision_recall")
            || chart_id == QStringLiteral("precision_recall"))
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
                const AggregateCounts counts
                    = aggregateThresholdCounts(input.images, input.class_ids, threshold, input.iou_threshold,
                                                input.matching_strategy);
                labels.push_back(threshold);
                precision.push_back(counts.tp + counts.fp > 0
                                        ? static_cast<double>(counts.tp) / (counts.tp + counts.fp)
                                        : 0.0);
                recall.push_back(counts.tp + counts.fn > 0
                                     ? static_cast<double>(counts.tp) / (counts.tp + counts.fn)
                                     : 0.0);
            }
            filtered.insert(QStringLiteral("data"), QVariantMap{
                {QStringLiteral("labels"), labels},
                {QStringLiteral("datasets"), QVariantList{
                    QVariantMap{{QStringLiteral("label"), QStringLiteral("Precision")}, {QStringLiteral("data"), precision}},
                    QVariantMap{{QStringLiteral("label"), QStringLiteral("Recall")}, {QStringLiteral("data"), recall}}}}});
        }
        else if (filter_kind == QStringLiteral("image_score")
                 || chart_id == QStringLiteral("anomaly_score_distribution"))
        {
            QVariantList labels;
            QVariantList scores;
            for (const EvaluationImageRecord &image : input.images)
            {
                labels.push_back(image.image_name);
                scores.push_back(image.score);
            }
            filtered.insert(QStringLiteral("data"), QVariantMap{
                {QStringLiteral("labels"), labels},
                {QStringLiteral("datasets"), QVariantList{
                    QVariantMap{{QStringLiteral("label"), QStringLiteral("score")}, {QStringLiteral("data"), scores}}}}});
        }
        output.charts.push_back(std::move(filtered));
    }
    return output;
}

} // namespace

ModelEvaluationViewModel::ModelEvaluationViewModel(QObject *parent)
    : QObject(parent)
    , instance_metrics_(new EvaluationMetricModel(this))
    , image_metrics_(new EvaluationMetricModel(this))
    , per_class_metrics_(new EvaluationMetricModel(this))
    , sorted_per_class_metrics_(new EvaluationMetricSortProxyModel(this))
    , confusion_matrix_(new EvaluationConfusionModel(this))
    , images_(new EvaluationImageModel(this))
    , filtered_images_(new EvaluationImageFilterProxyModel(this))
    , instances_(new EvaluationInstanceModel(this))
    , global_filtered_instances_(new EvaluationGlobalFilterProxyModel(this))
    , filtered_instances_(new EvaluationCellFilterProxyModel(this))
    , charts_(new EvaluationChartModel(this))
{
    sorted_per_class_metrics_->setSourceModel(per_class_metrics_);
    filtered_images_->setSourceModel(images_);
    global_filtered_instances_->setSourceModel(instances_);
    filtered_instances_->setSourceModel(global_filtered_instances_);
    connect(global_filtered_instances_, &EvaluationGlobalFilterProxyModel::filterChanged, this,
            [this]()
            {
                selected_proxy_row_ = -1;
                selected_instance_.clear();
                instances_->setSelectedEvent({});
                rebuildFilteredAggregates();
                emit selectedInstanceChanged();
            });
    connect(filtered_images_, &EvaluationImageFilterProxyModel::filterChanged, this,
            [this]() { rebuildFilteredAggregates(); });
    connect(filtered_instances_, &EvaluationCellFilterProxyModel::filterChanged, this,
            [this]()
            {
                selected_proxy_row_ = -1;
                selected_instance_.clear();
                instances_->setSelectedEvent({});
                emit selectedInstanceChanged();
            });
    connect(filtered_instances_, &QAbstractItemModel::modelReset, this,
            [this]() { selected_instance_ = {}; emit selectedInstanceChanged(); });
    connect(filtered_instances_, &QAbstractItemModel::rowsRemoved, this,
            [this]() { if (selected_proxy_row_ >= filtered_instances_->rowCount()) selectInstance(-1); });
}

bool ModelEvaluationViewModel::available() const { return available_; }
bool ModelEvaluationViewModel::loading() const { return loading_; }
QString ModelEvaluationViewModel::state() const { return state_; }
QString ModelEvaluationViewModel::error() const { return error_; }
bool ModelEvaluationViewModel::inferenceOutdated() const { return inference_outdated_; }
bool ModelEvaluationViewModel::evaluationOutdated() const { return evaluation_outdated_; }
QString ModelEvaluationViewModel::reportPath() const { return report_path_; }

void ModelEvaluationViewModel::setReportPath(const QString &path)
{
    const QString value = QDir::fromNativeSeparators(path.trimmed());
    if (report_path_ == value)
        return;
    report_path_ = value;
    instances_path_.clear();
    emit reportPathChanged();
    reload();
}

QString ModelEvaluationViewModel::resultPath() const { return result_path_; }

void ModelEvaluationViewModel::setResultPath(const QString &path)
{
    const QString value = QDir::fromNativeSeparators(path.trimmed());
    if (result_path_ == value)
        return;
    result_path_ = value;
    emit resultPathChanged();
    reload();
}

QString ModelEvaluationViewModel::primaryMetricSet() const { return primary_metric_set_; }
bool ModelEvaluationViewModel::globalFilterActive() const
{
    if (global_filter_ == nullptr)
        return false;
    bool active = false;
    QMetaObject::invokeMethod(global_filter_, "isActive", Qt::DirectConnection, Q_RETURN_ARG(bool, active));
    return active;
}

QString ModelEvaluationViewModel::globalFilterDescription() const
{
    if (global_filter_ != nullptr)
    {
        QString description;
        if (QMetaObject::invokeMethod(global_filter_, "description", Qt::DirectConnection,
                                      Q_RETURN_ARG(QString, description)) && !description.isEmpty())
            return description;
    }
    return globalFilterActive() ? QString("当前已应用全局过滤") : QString("全部测试样本");
}

QString ModelEvaluationViewModel::metricScopeDescription() const { return metric_scope_description_; }
QVariantMap ModelEvaluationViewModel::imageMetricDefinition() const { return image_metric_definition_; }
QString ModelEvaluationViewModel::resultRevision() const { return result_revision_; }
double ModelEvaluationViewModel::confidenceThreshold() const { return confidence_threshold_; }
double ModelEvaluationViewModel::iouThreshold() const { return iou_threshold_; }
QString ModelEvaluationViewModel::matchingStrategy() const { return matching_strategy_; }
bool ModelEvaluationViewModel::hasInstanceMetrics() const { return has_instance_metrics_; }
bool ModelEvaluationViewModel::hasImageMetrics() const { return has_image_metrics_; }
bool ModelEvaluationViewModel::hasConfusionMatrix() const { return has_confusion_matrix_; }
bool ModelEvaluationViewModel::hasInstanceEvents() const { return has_instance_events_; }
EvaluationMetricModel *ModelEvaluationViewModel::instanceMetrics() const { return instance_metrics_; }
EvaluationMetricModel *ModelEvaluationViewModel::imageMetrics() const { return image_metrics_; }
EvaluationMetricModel *ModelEvaluationViewModel::perClassMetrics() const { return per_class_metrics_; }
EvaluationMetricSortProxyModel *ModelEvaluationViewModel::sortedPerClassMetrics() const
{
    return sorted_per_class_metrics_;
}
EvaluationConfusionModel *ModelEvaluationViewModel::confusionMatrix() const { return confusion_matrix_; }
EvaluationImageModel *ModelEvaluationViewModel::images() const { return images_; }
EvaluationImageFilterProxyModel *ModelEvaluationViewModel::filteredImages() const { return filtered_images_; }
EvaluationInstanceModel *ModelEvaluationViewModel::instances() const { return instances_; }
EvaluationGlobalFilterProxyModel *ModelEvaluationViewModel::globalFilteredInstances() const
{
    return global_filtered_instances_;
}
EvaluationCellFilterProxyModel *ModelEvaluationViewModel::filteredInstances() const { return filtered_instances_; }
EvaluationChartModel *ModelEvaluationViewModel::charts() const { return charts_; }
QVariantMap ModelEvaluationViewModel::selectedInstance() const { return selected_instance_; }
QString ModelEvaluationViewModel::selectedEventUuid() const
{
    return selected_instance_.value(QStringLiteral("eventUuid")).toString();
}

int ModelEvaluationViewModel::selectedInstanceRow() const
{
    return selected_proxy_row_;
}

void ModelEvaluationViewModel::setLoading(const bool value)
{
    if (loading_ == value)
        return;
    loading_ = value;
    if (value)
        state_ = QStringLiteral("Loading");
    else if (state_ == QStringLiteral("Loading"))
        state_ = available_ ? QStringLiteral("Ready")
                            : (error_.isEmpty() ? QStringLiteral("NotRun") : QStringLiteral("Error"));
    emit loadingChanged();
}

void ModelEvaluationViewModel::clearReport(const QString &error, const QString &state)
{
    ++aggregation_revision_;
    available_ = false;
    error_ = error;
    state_ = state.isEmpty() ? (error.isEmpty() ? QStringLiteral("NotRun") : QStringLiteral("Error")) : state;
    inference_outdated_ = false;
    evaluation_outdated_ = false;
    primary_metric_set_.clear();
    metric_scope_description_.clear();
    image_metric_definition_.clear();
    result_revision_.clear();
    confidence_threshold_ = 0.0;
    iou_threshold_ = 0.0;
    matching_strategy_.clear();
    has_instance_metrics_ = false;
    has_image_metrics_ = false;
    has_confusion_matrix_ = false;
    has_instance_events_ = false;
    chart_descriptors_.clear();
    class_catalog_.clear();
    class_colors_.clear();
    instance_metrics_->setRecords({});
    image_metrics_->setRecords({});
    per_class_metrics_->setRecords({});
    confusion_matrix_->setRecords({});
    images_->setRecords({});
    instances_->setRecords({});
    global_filtered_instances_->setDatasetIds({});
    global_filtered_instances_->setClassIds({});
    filtered_instances_->setStatus({});
    filtered_instances_->setMatrixRow({});
    filtered_instances_->setMatrixColumn({});
    filtered_instances_->setPredClassIds({});
    charts_->setRecords({});
    selected_instance_.clear();
    selected_proxy_row_ = -1;
    instances_->setSelectedEvent({});
}

void ModelEvaluationViewModel::reload()
{
    const int revision = ++reload_revision_;
    setLoading(true);
    clearReport();
    if (report_path_.isEmpty())
    {
        state_ = QStringLiteral("MissingReport");
        setLoading(false);
        emit reportChanged();
        emit selectedInstanceChanged();
        return;
    }
    const QString report_path = report_path_;
    const QString result_path = result_path_;
    const QPointer<ModelEvaluationViewModel> guard(this);
    QThreadPool::globalInstance()->start([guard, revision, report_path, result_path]()
    {
        if (guard.isNull())
            return;
        QVariantMap report;
        QVariantList instance_records;
        QString instances_path;
        QString error;
        QString failure_state;
        bool inference_outdated = false;
        bool evaluation_outdated = false;
        try
        {
            const QFileInfo file(report_path);
            if (!file.exists() || !file.isFile())
            {
                failure_state = QStringLiteral("MissingReport");
                throw std::runtime_error("评估报告不存在");
            }
            const QFileInfo result_file(result_path.isEmpty() ? derivedResultPath(file) : result_path);
            // A direct report-only consumer is retained for unit-level loading,
            // while the project manager always supplies result.yaml.  Once a
            // result path is present, it is the sole validity gate.
            if (!result_path.isEmpty() || result_file.exists())
            {
                if (!result_file.exists() || !result_file.isFile())
                {
                    failure_state = QStringLiteral("MissingReport");
                    throw std::runtime_error("测试结果摘要不存在");
                }
                if (result_file.size() > kMaxEvaluationYamlBytes)
                    throw std::runtime_error("测试结果摘要超过大小限制");
                const YAML::Node result_root = common::yaml::loadFile(result_file);
                if (!result_root || !result_root.IsMap())
                    throw std::runtime_error("测试结果摘要不是 YAML map");
                const QVariantMap result_map = common::yaml::nodeVariant(result_root).toMap();
                validateResultProtocol(result_map, result_file, file, &inference_outdated, &evaluation_outdated);
            }
            const YAML::Node root = common::yaml::loadFile(file);
            if (!root || !root.IsMap())
                throw std::runtime_error("评估报告不是 YAML map");
            report = common::yaml::nodeVariant(root).toMap();
            validateReportProtocol(report, file);
            const QString configured_instances = report.value(QStringLiteral("instances_file")).toString();
            instances_path = configured_instances.isEmpty()
                ? QDir(file.absolutePath()).filePath(QStringLiteral("instances.yaml"))
                : (QFileInfo(configured_instances).isAbsolute()
                       ? configured_instances
                       : QDir(file.absolutePath()).filePath(configured_instances));
            const QFileInfo instances_file(instances_path);
            if (!instances_file.exists() || !instances_file.isFile())
                throw std::runtime_error("评估实例文件不存在");
            if (instances_file.size() > kMaxEvaluationYamlBytes)
                throw std::runtime_error("评估实例文件超过大小限制");
            if (instances_file.exists() && instances_file.isFile())
            {
                const YAML::Node instance_root = common::yaml::loadFile(instances_file);
                const YAML::Node records = instance_root.IsSequence() ? instance_root : instance_root["records"];
                if (records && records.IsSequence())
                {
                    if (records.size() > kMaxEvaluationRecords)
                        throw std::runtime_error("评估实例 records 数量超过限制");
                    if (!instance_root.IsSequence() && instance_root["record_count"] && instance_root["record_count"].IsScalar()
                        && instance_root["record_count"].as<int>() != static_cast<int>(records.size()))
                        throw std::runtime_error("评估实例 record_count 与 records 数量不一致");
                    QSet<QString> event_ids;
                    instance_records.reserve(static_cast<int>(records.size()));
                    for (const YAML::Node &entry : records)
                    {
                        if (!entry || !entry.IsMap())
                            throw std::runtime_error("评估实例记录必须为 YAML map");
                        const QVariantMap value = common::yaml::nodeVariant(entry).toMap();
                        const QString event_uuid = value.value(QStringLiteral("event_uuid")).toString().trimmed();
                        const QString status = value.value(QStringLiteral("status")).toString().trimmed();
                        bool score_ok = false;
                        bool iou_ok = false;
                        const double score = value.value(QStringLiteral("score")).toDouble(&score_ok);
                        const double iou = value.value(QStringLiteral("iou")).toDouble(&iou_ok);
                        if (event_uuid.isEmpty() || event_ids.contains(event_uuid)
                            || value.value(QStringLiteral("image_id")).toLongLong() < 0
                            || !score_ok || !std::isfinite(score) || !iou_ok || !std::isfinite(iou)
                            || iou < 0.0 || iou > 1.0
                            || (status != QStringLiteral("true_positive")
                                && status != QStringLiteral("class_mismatch")
                                && status != QStringLiteral("false_positive")
                                && status != QStringLiteral("false_negative")
                                && status != QStringLiteral("ignored")))
                            throw std::runtime_error("评估实例记录字段无效或 event_uuid 重复");
                        event_ids.insert(event_uuid);
                        instance_records.push_back(value);
                    }
                    const QVariant event_count = report.value(QStringLiteral("event_count"));
                    if (event_count.isValid() && event_count.toLongLong() != instance_records.size())
                        throw std::runtime_error("评估报告 event_count 与实例文件不一致");
                }
            }
        }
        catch (const std::exception &exception)
        {
            error = QString("%1").arg(QString(exception.what()));
            if (failure_state.isEmpty())
                failure_state = QStringLiteral("InvalidReport");
        }
        QMetaObject::invokeMethod(guard.data(), [guard, revision, report, instance_records, instances_path, error,
                                                  failure_state, inference_outdated, evaluation_outdated]()
        {
            if (guard.isNull() || guard->reload_revision_ != revision)
                return;
            if (!error.isEmpty())
            {
                guard->clearReport(error, failure_state);
                guard->setLoading(false);
                emit guard->reportChanged();
                emit guard->selectedInstanceChanged();
                return;
            }
            QVariantMap validated_report = report;
            if (inference_outdated)
                validated_report.insert(QStringLiteral("inference_outdated"), true);
            if (evaluation_outdated)
                validated_report.insert(QStringLiteral("evaluation_outdated"), true);
            guard->loadReport(validated_report);
            guard->instances_path_ = instances_path;
            guard->loadInstanceRecords(instance_records);
            if (!guard->has_instance_events_)
                guard->instances_->setRecords({});
        if (guard->images_->rowCount() == 0 && guard->instances_->rowCount() > 0)
        {
            QMap<qint64, EvaluationImageRecord> fallback;
            for (const EvaluationInstanceRecord &event : guard->instances_->records())
            {
                EvaluationImageRecord &image = fallback[event.image_id];
                image.image_id = event.image_id;
                image.dataset_id = event.dataset_id;
                image.image_name = event.image_name;
                image.image_path = event.image_path;
                image.image_width = event.image_width;
                image.image_height = event.image_height;
                if (event.gt_class_id >= 0)
                {
                    image.has_gt = true;
                    if (!image.gt_class_ids.contains(event.gt_class_id))
                        image.gt_class_ids.push_back(event.gt_class_id);
                }
                if (event.pred_class_id >= 0)
                {
                    image.has_pred = true;
                    image.score = std::max(image.score, event.score);
                    if (!image.pred_class_ids.contains(event.pred_class_id))
                        image.pred_class_ids.push_back(event.pred_class_id);
                }
            }
            std::vector<EvaluationImageRecord> values;
            values.reserve(fallback.size());
            for (auto it = fallback.cbegin(); it != fallback.cend(); ++it)
                values.push_back(it.value());
            guard->images_->setRecords(std::move(values));
        }
            if (guard->instances_->rowCount() > 0 || guard->images_->rowCount() > 0)
                guard->rebuildFilteredAggregates();
            guard->available_ = true;
            guard->state_ = QStringLiteral("Ready");
            guard->error_.clear();
            guard->setLoading(false);
            emit guard->reportChanged();
            emit guard->selectedInstanceChanged();
        }, Qt::QueuedConnection);
    });
}

void ModelEvaluationViewModel::refresh()
{
    reload();
}

void ModelEvaluationViewModel::setRuntimeState(const QString &state)
{
    const QString value = state.trimmed();
    if (value.isEmpty())
        return;
    if (state_ == value && !available_ && error_.isEmpty())
        return;
    if (value == QStringLiteral("Running") || value == QStringLiteral("Failed")
        || value == QStringLiteral("NotRun"))
    {
        clearReport({}, value);
        setLoading(false);
        emit reportChanged();
        emit selectedInstanceChanged();
    }
}

void ModelEvaluationViewModel::loadReport(const QVariantMap &root)
{
    inference_outdated_ = root.value(QStringLiteral("inference_outdated")).toBool();
    evaluation_outdated_ = root.value(QStringLiteral("evaluation_outdated")).toBool();
    primary_metric_set_ = root.value(QStringLiteral("primary_metric_set")).toString();
    result_revision_ = root.value(QStringLiteral("evaluation_digest")).toString();
    if (result_revision_.isEmpty())
        result_revision_ = root.value(QStringLiteral("inference_digest")).toString();
    metric_scope_description_ = primary_metric_set_ == QStringLiteral("official_metrics")
        ? QString("官方指标")
        : QString("诊断匹配指标");
    image_metric_definition_ = root.value(QStringLiteral("image_metric_definition")).toMap();
    const QVariantMap evaluation_config = root.value(QStringLiteral("evaluation_config")).toMap();
    confidence_threshold_ = realValue(evaluation_config, QStringLiteral("confidence_threshold"));
    iou_threshold_ = realValue(evaluation_config, QStringLiteral("iou_threshold"));
    matching_strategy_ = textValue(evaluation_config, QStringLiteral("matching_strategy"));
    const QVariantMap capabilities = root.value(QStringLiteral("capabilities")).toMap();
    has_instance_metrics_ = capabilities.value(QStringLiteral("has_instance_metrics")).toBool();
    has_image_metrics_ = capabilities.value(QStringLiteral("has_image_metrics")).toBool();
    has_confusion_matrix_ = capabilities.value(QStringLiteral("has_confusion_matrix")).toBool();
    has_instance_events_ = capabilities.value(QStringLiteral("has_instance_events")).toBool();
    class_catalog_.clear();
    class_colors_.clear();
    for (const QVariant &value : root.value(QStringLiteral("class_catalog")).toList())
    {
        const QVariantMap entry = value.toMap();
        bool ok = false;
        const int id = entry.value(QStringLiteral("id")).toInt(&ok);
        if (ok && id >= 0)
        {
            class_catalog_.insert(id, entry.value(QStringLiteral("name")).toString());
            class_colors_.insert(id, entry.value(QStringLiteral("color")).toString());
        }
    }
    const QVariantMap diagnostic = root.value(QStringLiteral("diagnostic_metrics")).toMap();
    const QVariantMap instance = diagnostic.value(QStringLiteral("instance")).toMap();
    const QVariantMap overall = instance.value(QStringLiteral("overall")).toMap();
    if (!overall.isEmpty())
        instance_metrics_->setRecords({metricFromMap(QStringLiteral("overall"), overall, QString("整体"))});
    const QVariantMap image = diagnostic.value(QStringLiteral("image")).toMap();
    if (!image.isEmpty())
        image_metrics_->setRecords({metricFromMap(QStringLiteral("image"), image, QString("图像"))});

    // The report keeps official and diagnostic metrics separate.  The
    // primary set only changes which values are shown in the overall panel;
    // matrix/events always remain diagnostic records.
    const QVariantMap official = root.value(QStringLiteral("official_metrics")).toMap();
    if (primary_metric_set_ == QStringLiteral("official_metrics") && official.value(QStringLiteral("available")).toBool())
    {
        const QVariantMap official_instance = official.value(QStringLiteral("instance")).toMap();
        if (!official_instance.isEmpty())
            instance_metrics_->setRecords({metricFromMap(QStringLiteral("overall"), official_instance,
                                                          QString("整体"))});
        const QVariantMap official_image = official.value(QStringLiteral("image")).toMap();
        if (!official_image.isEmpty())
            image_metrics_->setRecords({metricFromMap(QStringLiteral("image"), official_image,
                                                       QString("图像"))});
    }

    std::vector<EvaluationImageRecord> image_records;
    for (const QVariant &value : root.value(QStringLiteral("image_records")).toList())
        image_records.push_back(imageFromMap(value.toMap()));
    images_->setRecords(std::move(image_records));

    std::vector<EvaluationMetricRecord> per_class;
    for (const QVariant &value : instance.value(QStringLiteral("per_class")).toList())
    {
        const QVariantMap map = value.toMap();
        const QString key = map.value(QStringLiteral("class_id")).toString();
        per_class.push_back(metricFromMap(key, map, map.value(QStringLiteral("class_name")).toString()));
    }
    per_class_metrics_->setRecords(std::move(per_class));

    std::vector<EvaluationConfusionCell> cells;
    const QVariantMap matrix = root.value(QStringLiteral("confusion_matrix")).toMap();
    const QVariantList matrix_cells = matrix.value(QStringLiteral("cells")).toList();
    if (!matrix_cells.isEmpty())
    {
        for (const QVariant &value : matrix_cells)
        {
            const QVariantMap map = value.toMap();
            EvaluationConfusionCell cell;
            cell.row_key = map.value(QStringLiteral("row_key")).toString();
            cell.column_key = map.value(QStringLiteral("column_key")).toString();
            cell.row_label = map.value(QStringLiteral("row_label")).toString();
            cell.column_label = map.value(QStringLiteral("column_label")).toString();
            cell.count = longValue(map, QStringLiteral("count"));
            cell.row_class_id = intValue(map, QStringLiteral("row_class_id"));
            cell.column_class_id = intValue(map, QStringLiteral("column_class_id"));
            cell.cell_kind = map.value(QStringLiteral("cell_kind")).toString();
            cell.selectable = map.contains(QStringLiteral("selectable"))
                ? map.value(QStringLiteral("selectable")).toBool()
                : true;
            cell.diagonal = map.value(QStringLiteral("is_diagonal")).toBool();
            cell.error = map.value(QStringLiteral("is_error")).toBool();
            cells.push_back(cell);
        }
    }
    else
    {
        for (auto row_it = matrix.cbegin(); row_it != matrix.cend(); ++row_it)
        {
            const QVariantMap columns = row_it.value().toMap();
            for (auto column_it = columns.cbegin(); column_it != columns.cend(); ++column_it)
            {
                EvaluationConfusionCell cell;
                cell.row_key = row_it.key();
                cell.column_key = column_it.key();
                cell.row_label = row_it.key();
                cell.column_label = column_it.key();
                cell.count = column_it.value().toLongLong();
                cell.row_class_id = row_it.key().toInt();
                cell.column_class_id = column_it.key().toInt();
                const bool row_fn = cell.row_key == QStringLiteral("FN");
                const bool row_total = cell.row_key == QStringLiteral("TOTAL");
                const bool column_fp = cell.column_key == QStringLiteral("FP");
                const bool column_total = cell.column_key == QStringLiteral("TOTAL");
                cell.selectable = !row_total && !column_total;
                cell.diagonal = !row_fn && !column_fp && !column_total && !row_total
                    && cell.row_class_id >= 0 && cell.row_class_id == cell.column_class_id;
                cell.error = !cell.diagonal && !row_total && !column_total;
                cell.cell_kind = row_fn ? QStringLiteral("false_negative")
                    : (column_fp ? QStringLiteral("false_positive")
                       : (row_total && column_total ? QStringLiteral("all")
                          : (row_total ? QStringLiteral("gt_total")
                             : (column_total ? QStringLiteral("pred_total")
                                : (cell.row_class_id == cell.column_class_id
                                   ? QStringLiteral("match") : QStringLiteral("class_mismatch"))))));
                cells.push_back(cell);
            }
        }
    }
    confusion_matrix_->setRecords(std::move(cells));
    QList<QVariantMap> charts;
    for (const QVariant &value : root.value(QStringLiteral("charts")).toList())
        charts.push_back(value.toMap());
    chart_descriptors_ = charts;
    charts_->setRecords(std::move(charts));
}

void ModelEvaluationViewModel::loadInstances(const QString &path)
{
    const QFileInfo file(path);
    if (!file.exists() || !file.isFile())
        return;
    try
    {
        const YAML::Node root = common::yaml::loadFile(file);
        const YAML::Node records = root.IsSequence() ? root : root["records"];
        if (!records || !records.IsSequence())
            return;
        if (!root.IsSequence() && root["record_count"] && root["record_count"].IsScalar()
            && root["record_count"].as<int>() != static_cast<int>(records.size()))
            throw std::runtime_error("评估实例 record_count 与 records 数量不一致");
        QVariantList values;
        values.reserve(static_cast<int>(records.size()));
        for (const YAML::Node &entry : records)
            values.push_back(common::yaml::nodeVariant(entry).toMap());
        loadInstanceRecords(values);
    }
    catch (const std::exception &)
    {
        // 报告本身仍然可显示；实例文件缺失时只显示空实例区。
    }
}

void ModelEvaluationViewModel::loadInstanceRecords(const QVariantList &records)
{
    QSet<QString> event_ids;
    std::vector<EvaluationInstanceRecord> values;
    values.reserve(static_cast<size_t>(records.size()));
    for (const QVariant &entry : records)
    {
        EvaluationInstanceRecord value = instanceFromMap(entry.toMap());
        if (value.event_uuid.isEmpty() || event_ids.contains(value.event_uuid))
            continue;
        event_ids.insert(value.event_uuid);
        if (value.gt_class_color.isEmpty())
            value.gt_class_color = class_colors_.value(value.gt_class_id);
        if (value.pred_class_color.isEmpty())
            value.pred_class_color = class_colors_.value(value.pred_class_id);
        value.thumbnail_url = thumbnailUrl(value);
        values.push_back(std::move(value));
    }
    instances_->setRecords(std::move(values));
}

void ModelEvaluationViewModel::rebuildFilteredAggregates()
{
    const int revision = ++aggregation_revision_;
    EvaluationAggregateInput input;
    input.class_catalog = class_catalog_;
    input.chart_descriptors = chart_descriptors_;
    input.class_ids = global_filtered_instances_->classIds();
    input.confidence_threshold = confidence_threshold_;
    input.iou_threshold = iou_threshold_;
    input.matching_strategy = matching_strategy_.isEmpty() ? QStringLiteral("greedy_iou") : matching_strategy_;
    input.has_instance_metrics = has_instance_metrics_;
    input.has_image_metrics = has_image_metrics_;
    input.has_confusion_matrix = has_confusion_matrix_;

    // QSortFilterProxyModel remains the single GUI-thread filter boundary.
    // The worker receives only detached value records and never touches a
    // proxy, QModelIndex, QObject or QML object.
    for (const EvaluationInstanceRecord &record : instances_->records())
    {
        if (global_filtered_instances_->acceptsRecord(record))
            input.instances.push_back(record);
    }
    const QVariantList dataset_ids = global_filtered_instances_->datasetIds();
    const QVariantList class_ids = global_filtered_instances_->classIds();

    // The image proxy decides whether an image has at least one class that
    // passes the external GlobalFilter, but the image still contains all of
    // its classes.  Detach a class-filtered value record before handing it to
    // the worker so image metrics and PR charts cannot count unrelated
    // classes.  This keeps all QObject/proxy access on the GUI thread.
    bool external_class_filter_enabled = false;
    bool external_class_filter_available = false;
    if (global_filter_ != nullptr)
    {
        external_class_filter_available = QMetaObject::invokeMethod(
            global_filter_, "isLabelClassFilterEnabled", Qt::DirectConnection,
            Q_RETURN_ARG(bool, external_class_filter_enabled));
    }
    const bool class_filter_active = !class_ids.isEmpty()
        || (external_class_filter_available && external_class_filter_enabled);
    const auto classAllowed = [this, &class_ids, external_class_filter_available,
                               external_class_filter_enabled](const int class_id)
    {
        if (class_id < 0)
            return false;
        if (!class_ids.isEmpty())
        {
            bool selected = false;
            for (const QVariant &value : class_ids)
                selected = selected || value.toInt() == class_id;
            if (!selected)
                return false;
        }
        if (external_class_filter_available && external_class_filter_enabled && global_filter_ != nullptr)
        {
            bool accepted = true;
            if (QMetaObject::invokeMethod(global_filter_, "acceptsLabelClassId", Qt::DirectConnection,
                                          Q_RETURN_ARG(bool, accepted), Q_ARG(qint64, qint64(class_id))))
                return accepted;
        }
        return true;
    };
    for (const EvaluationImageRecord &record : images_->records())
    {
        if (!filtered_images_->acceptsRecord(record))
            continue;
        if (!dataset_ids.isEmpty())
        {
            bool match = false;
            for (const QVariant &value : dataset_ids)
                match = match || value.toLongLong() == record.dataset_id;
            if (!match)
                continue;
        }
        if (!class_ids.isEmpty())
        {
            bool match = false;
            const QList<int> &relevant_classes
                = (!record.gt_class_ids.isEmpty() || record.has_gt) ? record.gt_class_ids : record.pred_class_ids;
            for (const QVariant &value : class_ids)
                match = match || relevant_classes.contains(value.toInt());
            if (!match)
                continue;
        }
        if (!class_filter_active)
        {
            input.images.push_back(record);
            continue;
        }

        // Keep only selected GT/PRED classes and their corresponding detail
        // records.  A selected class filter is GT-preferred for image
        // visibility (handled by filtered_images_), while unrelated
        // predictions are excluded from the aggregate once the image is in.
        EvaluationImageRecord filtered = record;
        QList<int> filtered_gt_classes;
        for (const int class_id : record.gt_class_ids)
            if (classAllowed(class_id))
                filtered_gt_classes.push_back(class_id);
        QList<int> filtered_pred_classes;
        for (const int class_id : record.pred_class_ids)
            if (classAllowed(class_id))
                filtered_pred_classes.push_back(class_id);
        QList<EvaluationGroundTruthRecord> filtered_gt_instances;
        for (const EvaluationGroundTruthRecord &ground_truth : record.gt_instances)
            if (classAllowed(ground_truth.class_id))
                filtered_gt_instances.push_back(ground_truth);
        QList<EvaluationPredictionRecord> filtered_predictions;
        for (const EvaluationPredictionRecord &prediction : record.predictions)
            if (classAllowed(prediction.class_id))
                filtered_predictions.push_back(prediction);

        filtered.gt_class_ids = std::move(filtered_gt_classes);
        filtered.pred_class_ids = std::move(filtered_pred_classes);
        filtered.gt_instances = std::move(filtered_gt_instances);
        filtered.predictions = std::move(filtered_predictions);
        filtered.score = 0.0;
        for (const EvaluationPredictionRecord &prediction : filtered.predictions)
            filtered.score = std::max(filtered.score, prediction.score);
        filtered.has_gt = !filtered.gt_class_ids.isEmpty() || !filtered.gt_instances.isEmpty();
        filtered.has_pred = record.has_pred && (!filtered.pred_class_ids.isEmpty() || !filtered.predictions.isEmpty());
        if (filtered.gt_class_ids.isEmpty() && filtered.pred_class_ids.isEmpty()
            && !filtered.has_gt && !filtered.has_pred)
            continue;
        input.images.push_back(std::move(filtered));
    }

    const QPointer<ModelEvaluationViewModel> guard(this);
    QThreadPool::globalInstance()->start([guard, revision, input = std::move(input)]() mutable
    {
        if (guard.isNull())
            return;
        const EvaluationAggregateOutput output = aggregateEvaluation(input);
        QMetaObject::invokeMethod(guard, [guard, revision, output]() mutable
        {
            if (guard.isNull() || guard->aggregation_revision_ != revision)
                return;
            guard->instance_metrics_->setRecords(output.instance_metrics);
            guard->image_metrics_->setRecords(output.image_metrics);
            guard->per_class_metrics_->setRecords(output.per_class_metrics);
            guard->confusion_matrix_->setRecords(output.confusion);
            guard->charts_->setRecords(output.charts);
        });
    });
    return;
}

QVariantMap ModelEvaluationViewModel::instanceToMap(const EvaluationInstanceRecord &record) const
{
    return {{QStringLiteral("eventUuid"), record.event_uuid}, {QStringLiteral("imageId"), record.image_id},
            {QStringLiteral("datasetId"), record.dataset_id}, {QStringLiteral("imageName"), record.image_name},
            {QStringLiteral("imagePath"), record.image_path}, {QStringLiteral("imageWidth"), record.image_width},
            {QStringLiteral("imageHeight"), record.image_height}, {QStringLiteral("status"), record.status},
            {QStringLiteral("statusText"), statusDisplayText(record.status)},
            {QStringLiteral("gtClass"), record.gt_class}, {QStringLiteral("gtClassName"), record.gt_class},
            {QStringLiteral("predClass"), record.pred_class}, {QStringLiteral("predClassName"), record.pred_class},
            {QStringLiteral("gtClassId"), record.gt_class_id}, {QStringLiteral("predClassId"), record.pred_class_id},
            {QStringLiteral("gtLabelId"), record.gt_label_id}, {QStringLiteral("gtInstanceId"), record.gt_instance_id},
            {QStringLiteral("predInstanceId"), record.pred_instance_id},
            {QStringLiteral("gtClassColor"), record.gt_class_color}, {QStringLiteral("predClassColor"), record.pred_class_color},
            {QStringLiteral("thumbnailUrl"), record.thumbnail_url},
            {QStringLiteral("score"), record.score}, {QStringLiteral("predScore"), record.score},
            {QStringLiteral("iou"), record.iou}, {QStringLiteral("selected"), record.selected},
            {QStringLiteral("gtGeometry"), record.gt_geometry},
            {QStringLiteral("predGeometry"), record.pred_geometry},
            {QStringLiteral("gtBounds"), record.gt_bounds}, {QStringLiteral("predBounds"), record.pred_bounds},
            {QStringLiteral("cropBounds"), record.crop_bounds},
            {QStringLiteral("gtOverlayBounds"), record.gt_overlay_bounds},
            {QStringLiteral("predOverlayBounds"), record.pred_overlay_bounds},
            {QStringLiteral("gtOverlayPoints"), record.gt_overlay_points},
            {QStringLiteral("predOverlayPoints"), record.pred_overlay_points},
            {QStringLiteral("gtMaskUrl"), record.gt_mask_url},
            {QStringLiteral("predMaskUrl"), record.pred_mask_url}};
}

QString ModelEvaluationViewModel::thumbnailUrl(const EvaluationInstanceRecord &record) const
{
    if (record.image_path.trimmed().isEmpty())
        return {};

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("event"), record.event_uuid);
    query.addQueryItem(QStringLiteral("revision"), result_revision_);
    query.addQueryItem(QStringLiteral("path"), record.image_path);
    const auto addBounds = [&query, &record](const QString &name, const QString &key)
    {
        const QVariant value = record.crop_bounds.value(key);
        if (value.isValid())
            query.addQueryItem(name, QString::number(value.toDouble(), 'f', 6));
    };
    addBounds(QStringLiteral("x"), QStringLiteral("x"));
    addBounds(QStringLiteral("y"), QStringLiteral("y"));
    addBounds(QStringLiteral("width"), QStringLiteral("width"));
    addBounds(QStringLiteral("height"), QStringLiteral("height"));

    const QString encoded_event = QString::fromLatin1(QUrl::toPercentEncoding(record.event_uuid));
    return QString("image://evaluationthumbnail/%1?%2")
        .arg(encoded_event, query.toString(QUrl::FullyEncoded));
}

void ModelEvaluationViewModel::selectInstance(const int proxyRow)
{
    if (proxyRow < 0 || proxyRow >= filtered_instances_->rowCount())
    {
        selected_proxy_row_ = -1;
        selected_instance_.clear();
        instances_->setSelectedEvent({});
    }
    else
    {
        selected_proxy_row_ = proxyRow;
        const QModelIndex proxy_index = filtered_instances_->index(proxyRow, 0);
        const QModelIndex global_index = filtered_instances_->mapToSource(proxy_index);
        const QModelIndex source_index = global_filtered_instances_->mapToSource(global_index);
        const EvaluationInstanceRecord *record = instances_->recordAt(source_index.row());
        selected_instance_ = record != nullptr ? instanceToMap(*record) : QVariantMap{};
        instances_->setSelectedEvent(record != nullptr ? record->event_uuid : QString());
    }
    emit selectedInstanceChanged();
}

bool ModelEvaluationViewModel::selectInstance(const QString &eventUuid)
{
    const QString value = eventUuid.trimmed();
    if (value.isEmpty())
    {
        selectInstance(-1);
        return false;
    }
    for (int row = 0; row < filtered_instances_->rowCount(); ++row)
    {
        const QModelIndex index = filtered_instances_->index(row, 0);
        if (filtered_instances_->data(index, EvaluationInstanceModel::EventUuidRole).toString() == value)
        {
            selectInstance(row);
            return true;
        }
    }
    selectInstance(-1);
    return false;
}

void ModelEvaluationViewModel::selectMatrixCell(const QString &rowKey, const QString &columnKey)
{
    filtered_instances_->setMatrixRow(rowKey);
    filtered_instances_->setMatrixColumn(columnKey);
    selectInstance(-1);
}

bool ModelEvaluationViewModel::selectConfusionCell(const int row, const int column)
{
    if (confusion_matrix_ == nullptr || row < 0 || column < 0 || row >= confusion_matrix_->rowCount()
        || column >= confusion_matrix_->columnCount())
        return false;
    const QModelIndex index = confusion_matrix_->index(row, column);
    if (!index.isValid() || !index.data(EvaluationConfusionModel::SelectableRole).toBool())
        return false;
    selectMatrixCell(index.data(EvaluationConfusionModel::RowKeyRole).toString(),
                     index.data(EvaluationConfusionModel::ColumnKeyRole).toString());
    return true;
}

void ModelEvaluationViewModel::clearMatrixSelection()
{
    filtered_instances_->setMatrixRow({});
    filtered_instances_->setMatrixColumn({});
}

void ModelEvaluationViewModel::clearConfusionCellFilter()
{
    clearMatrixSelection();
}

void ModelEvaluationViewModel::setDatasetFilter(const QVariantList &datasetIds)
{
    global_filtered_instances_->setDatasetIds(datasetIds);
}

void ModelEvaluationViewModel::setClassFilter(const QVariantList &classIds)
{
    global_filtered_instances_->setClassIds(classIds);
}

void ModelEvaluationViewModel::setPredClassFilter(const qint64 classId)
{
    filtered_instances_->setPredClassIds(classId >= 0 ? QVariantList{classId} : QVariantList{});
}

void ModelEvaluationViewModel::clearPredClassFilter()
{
    filtered_instances_->setPredClassIds({});
}

void ModelEvaluationViewModel::setStatusFilter(const QString &status)
{
    filtered_instances_->setStatus(status);
}

void ModelEvaluationViewModel::clearFilters()
{
    global_filtered_instances_->setDatasetIds({});
    global_filtered_instances_->setClassIds({});
    filtered_instances_->setStatus({});
    filtered_instances_->setPredClassIds({});
    clearMatrixSelection();
}

void ModelEvaluationViewModel::setGlobalFilter(QObject *filter)
{
    if (global_filter_ != nullptr)
        disconnect(global_filter_, nullptr, this, nullptr);
    global_filter_ = filter;
    filtered_images_->setGlobalFilter(filter);
    global_filtered_instances_->setGlobalFilter(filter);
    if (global_filter_ != nullptr)
        connect(global_filter_, SIGNAL(filterChanged()), this, SIGNAL(filterStateChanged()));
    emit filterStateChanged();
}

} // namespace dltool::model
