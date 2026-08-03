#include "model/ModelEvaluationViewModel.h"
#include "model/ModelEvaluationService.h"
#include "model/ModelEvaluationProtocol.h"
#include "model/TaskCommunication.h"

#include "common/YamlUtils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QVariantList>
#include <QMap>
#include <QMetaMethod>
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

QString statusDisplayText(const evaluation::Status status)
{
    return evaluation::statusDisplayName(status);
}

QString classColor(const int class_id)
{
    static const QStringList palette = {QStringLiteral("#ef5350"), QStringLiteral("#42a5f5"),
                                        QStringLiteral("#66bb6a"), QStringLiteral("#ffa726"),
                                        QStringLiteral("#ab47bc"), QStringLiteral("#26c6da"),
                                        QStringLiteral("#8d6e63"), QStringLiteral("#78909c")};
    const int index = class_id >= 0 ? class_id % palette.size() : 0;
    return palette.at(index);
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

QString textValue(const QVariantMap &map, const evaluation::Field field, const QString &fallback = {})
{
    return textValue(map, evaluation::fieldName(field), fallback);
}

int intValue(const QVariantMap &map, const evaluation::Field field, const int fallback = -1)
{
    return intValue(map, evaluation::fieldName(field), fallback);
}

qint64 longValue(const QVariantMap &map, const evaluation::Field field, const qint64 fallback = 0)
{
    return longValue(map, evaluation::fieldName(field), fallback);
}

double realValue(const QVariantMap &map, const evaluation::Field field, const double fallback = 0.0)
{
    return realValue(map, evaluation::fieldName(field), fallback);
}

bool hasInvokable(QObject *object, const char *method, const int parameter_count)
{
    if (object == nullptr)
        return false;
    const QMetaObject *meta_object = object->metaObject();
    for (int index = 0; index < meta_object->methodCount(); ++index)
    {
        const QMetaMethod meta_method = meta_object->method(index);
        if (meta_method.name() == method && meta_method.parameterCount() == parameter_count)
            return true;
    }
    return false;
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

void requireReportField(const QVariantMap &root, const evaluation::Field field)
{
    const QString name = evaluation::fieldName(field);
    if (!root.contains(name))
        throw std::runtime_error(QString("评估报告缺少字段: %1").arg(name).toUtf8().constData());
}

void validateReportProtocol(const QVariantMap &root, const QFileInfo &report_file)
{
    if (report_file.size() > kMaxEvaluationYamlBytes)
        throw std::runtime_error("评估报告超过大小限制");
    const QVariant version = root.value(evaluation::fieldName(evaluation::Field::SchemaVersion));
    bool version_ok = false;
    const int schema_version = version.toInt(&version_ok);
    if (!version_ok || schema_version != evaluation::kReportSchemaVersion)
        throw std::runtime_error("评估报告 schema_version 无效");
    for (const evaluation::Field field : {evaluation::Field::ModelUuid, evaluation::Field::TestTaskUuid,
                                           evaluation::Field::Method, evaluation::Field::PrimaryMetricSet,
                                           evaluation::Field::EvaluatedAt, evaluation::Field::EvaluationConfig,
                                           evaluation::Field::Capabilities, evaluation::Field::DiagnosticMetrics,
                                           evaluation::Field::ConfusionMatrix, evaluation::Field::Charts,
                                           evaluation::Field::ImageRecords, evaluation::Field::InstanceRecords,
                                           evaluation::Field::DatasetManifest, evaluation::Field::PredictionManifest,
                                           evaluation::Field::ImageList, evaluation::Field::ImageCount,
                                           evaluation::Field::PredictionCount, evaluation::Field::EventCount})
        requireReportField(root, field);
    if (root.value(evaluation::fieldName(evaluation::Field::ModelUuid)).toString().trimmed().isEmpty()
        || root.value(evaluation::fieldName(evaluation::Field::TestTaskUuid)).toString().trimmed().isEmpty()
        || root.value(evaluation::fieldName(evaluation::Field::Method)).toString().trimmed().isEmpty())
        throw std::runtime_error("评估报告 model_uuid/test_task_uuid/method 无效");
    if (root.value(evaluation::fieldName(evaluation::Field::EvaluationConfig)).toMap().isEmpty()
        || root.value(evaluation::fieldName(evaluation::Field::Capabilities)).toMap().isEmpty())
        throw std::runtime_error("评估报告 evaluation_config/capabilities 无效");
    const QString method_key = root.value(evaluation::fieldName(evaluation::Field::Method)).toString().trimmed().toLower();
    const evaluation::Method method = evaluation::methodFromKey(method_key);
    if (method == evaluation::Method::Unknown || evaluation::methodKey(method) != method_key)
        throw std::runtime_error("评估报告 method 不是规范值");
    const QString metric_set_key = root.value(evaluation::fieldName(evaluation::Field::PrimaryMetricSet)).toString().trimmed().toLower();
    const evaluation::MetricSet metric_set = evaluation::metricSetFromKey(metric_set_key);
    if (evaluation::metricSetKey(metric_set) != metric_set_key)
        throw std::runtime_error("评估报告 primary_metric_set 不是规范值");
    const QVariantMap evaluation_config = root.value(evaluation::fieldName(evaluation::Field::EvaluationConfig)).toMap();
    const QString matching_strategy_key
        = evaluation_config.value(evaluation::fieldName(evaluation::Field::MatchingStrategy)).toString().trimmed().toLower();
    if (!evaluation_config.contains(evaluation::fieldName(evaluation::Field::ConfidenceThreshold))
        || !evaluation_config.contains(evaluation::fieldName(evaluation::Field::IouThreshold))
        || matching_strategy_key.isEmpty()
        || evaluation::matchingStrategyKey(evaluation::matchingStrategyFromKey(matching_strategy_key))
               != matching_strategy_key)
        throw std::runtime_error("评估报告 evaluation_config 不是规范值");
    const QVariant image_records_value = root.value(evaluation::fieldName(evaluation::Field::ImageRecords));
    if (!image_records_value.canConvert<QVariantList>())
        throw std::runtime_error("评估报告 image_records 必须为序列");
    const QVariantList image_records = image_records_value.toList();
    if (image_records.size() > static_cast<int>(kMaxEvaluationRecords))
        throw std::runtime_error("评估报告 image_records 数量超过限制");

    const auto rejectLegacyFields = [](const QVariantMap &map, const QStringList &fields)
    {
        for (const QString &field : fields)
            if (map.contains(field))
                throw std::runtime_error(QString("评估报告包含已删除字段: %1").arg(field).toUtf8().constData());
    };
    const auto requireList = [](const QVariantMap &map, const QString &field)
    {
        if (!map.value(field).canConvert<QVariantList>())
            throw std::runtime_error(QString("评估报告字段 %1 必须为序列").arg(field).toUtf8().constData());
    };
    for (const QVariant &entry : image_records)
    {
        const QVariantMap image = entry.toMap();
        for (const evaluation::Field field : {evaluation::Field::ImageId, evaluation::Field::DatasetId,
                                              evaluation::Field::ImageName, evaluation::Field::ImagePath,
                                              evaluation::Field::ImageWidth, evaluation::Field::ImageHeight})
            if (!image.contains(evaluation::fieldName(field)))
                throw std::runtime_error(QString("评估报告图像记录缺少字段: %1")
                                             .arg(evaluation::fieldName(field))
                                             .toUtf8()
                                             .constData());
        requireList(image, evaluation::fieldName(evaluation::Field::GtInstances));
        requireList(image, evaluation::fieldName(evaluation::Field::Predictions));
        rejectLegacyFields(image, {QStringLiteral("gt_label_ids"), QStringLiteral("gt_class_ids"),
                                   QStringLiteral("pred_class_ids"), QStringLiteral("score"),
                                   QStringLiteral("has_gt"), QStringLiteral("has_pred")});
        for (const QVariant &value : image.value(evaluation::fieldName(evaluation::Field::GtInstances)).toList())
        {
            const QVariantMap ground_truth = value.toMap();
            for (const evaluation::Field field : {evaluation::Field::LabelId, evaluation::Field::ClassId,
                                                  evaluation::Field::ClassName, evaluation::Field::Geometry})
                if (!ground_truth.contains(evaluation::fieldName(field)))
                    throw std::runtime_error(QString("评估报告 GT 记录缺少字段: %1")
                                                 .arg(evaluation::fieldName(field))
                                                 .toUtf8()
                                                 .constData());
            rejectLegacyFields(ground_truth, {evaluation::fieldName(evaluation::Field::Bounds)});
        }
        for (const QVariant &value : image.value(evaluation::fieldName(evaluation::Field::Predictions)).toList())
        {
            const QVariantMap prediction = value.toMap();
            for (const evaluation::Field field : {evaluation::Field::PredictionId, evaluation::Field::ClassId,
                                                  evaluation::Field::ClassName, evaluation::Field::Score,
                                                  evaluation::Field::Geometry})
                if (!prediction.contains(evaluation::fieldName(field)))
                    throw std::runtime_error(QString("评估报告预测记录缺少字段: %1")
                                                 .arg(evaluation::fieldName(field))
                                                 .toUtf8()
                                                 .constData());
            rejectLegacyFields(prediction, {evaluation::fieldName(evaluation::Field::ImageId),
                                            evaluation::fieldName(evaluation::Field::DatasetId),
                                            evaluation::fieldName(evaluation::Field::Bounds)});
        }
    }

    const QVariantMap confusion_matrix = root.value(evaluation::fieldName(evaluation::Field::ConfusionMatrix)).toMap();
    requireList(confusion_matrix, evaluation::fieldName(evaluation::Field::Cells));
    for (const QVariant &entry : confusion_matrix.value(evaluation::fieldName(evaluation::Field::Cells)).toList())
    {
        const QVariantMap cell = entry.toMap();
        for (const evaluation::Field field : {evaluation::Field::RowKey, evaluation::Field::ColumnKey,
                                              evaluation::Field::RowLabel, evaluation::Field::ColumnLabel,
                                              evaluation::Field::RowClassId, evaluation::Field::ColumnClassId,
                                              evaluation::Field::Count, evaluation::Field::CellKind,
                                              evaluation::Field::Selectable, evaluation::Field::IsDiagonal,
                                              evaluation::Field::IsError})
            if (!cell.contains(evaluation::fieldName(field)))
                throw std::runtime_error(QString("评估报告矩阵单元缺少字段: %1")
                                             .arg(evaluation::fieldName(field))
                                             .toUtf8()
                                             .constData());
    }
    if (!root.value(evaluation::fieldName(evaluation::Field::Charts)).canConvert<QVariantList>())
        throw std::runtime_error("评估报告 charts 必须为序列");

    if (!root.value(evaluation::fieldName(evaluation::Field::InstanceRecords)).canConvert<QVariantList>())
        throw std::runtime_error("评估报告缺少 instance_records");
    const QString task_root = QFileInfo(report_file.absolutePath()).absoluteDir().absolutePath();

    for (const auto &reference : {std::pair<QString, QString>(evaluation::fieldName(evaluation::Field::PredictionManifest),
                                                               QDir(task_root).filePath(QStringLiteral("pred/manifest.yaml"))),
                                  std::pair<QString, QString>(evaluation::fieldName(evaluation::Field::ImageList),
                                                               QDir(task_root).filePath(QStringLiteral("pred/images.txt"))),
                                  std::pair<QString, QString>(evaluation::fieldName(evaluation::Field::DatasetManifest), QString())})
    {
        const QString configured = root.value(reference.first).toString();
        if (configured.trimmed().isEmpty())
            throw std::runtime_error(QString("评估报告缺少 %1").arg(reference.first).toUtf8().constData());
        const QString target = resolveReportReference(report_file.absolutePath(), configured);
        if (target.isEmpty() || !pathWithin(task_root, target))
            throw std::runtime_error(QString("评估报告 %1 路径越界").arg(reference.first).toUtf8().constData());
        if (reference.first != evaluation::fieldName(evaluation::Field::DatasetManifest)
            && QFileInfo(target).absoluteFilePath().compare(QFileInfo(reference.second).absoluteFilePath(),
                                                            Qt::CaseInsensitive) != 0)
            throw std::runtime_error(QString("评估报告 %1 未指向当前测试产物").arg(reference.first).toUtf8().constData());
    }

}

QString resolveTaskReference(const QFileInfo &result_file, const QString &value)
{
    const QString candidate = value.trimmed();
    if (candidate.isEmpty())
        return {};
    const QFileInfo info(candidate);
    return info.isAbsolute() ? info.absoluteFilePath() : result_file.absoluteDir().filePath(candidate);
}

bool sourceImageExists(const QString &path, const QString &dataset_manifest)
{
    QFileInfo image(path);
    if (!image.isAbsolute() && !image.exists() && !dataset_manifest.isEmpty())
        image = QFileInfo(QDir(QFileInfo(dataset_manifest).absolutePath()), path);
    return image.exists() && image.isFile();
}

void filterUnavailableReportDetails(QVariantMap &report, const QString &dataset_manifest)
{
    QVariantList visible_images;
    QSet<qint64> visible_image_ids;
    qint64 visible_predictions = 0;
    {
        for (const QVariant &entry : report.value(evaluation::fieldName(evaluation::Field::ImageRecords)).toList())
        {
            const QVariantMap image = entry.toMap();
            if (!sourceImageExists(image.value(evaluation::fieldName(evaluation::Field::ImagePath)).toString(),
                                   dataset_manifest))
                continue;
            const qint64 image_id = image.value(evaluation::fieldName(evaluation::Field::ImageId)).toLongLong();
            if (image_id < 0)
                continue;
            visible_image_ids.insert(image_id);
            visible_predictions += image.value(evaluation::fieldName(evaluation::Field::Predictions)).toList().size();
            visible_images.push_back(image);
        }
    }

    QVariantList visible_events;
    {
        for (const QVariant &entry : report.value(evaluation::fieldName(evaluation::Field::InstanceRecords)).toList())
        {
            if (visible_image_ids.contains(
                    entry.toMap().value(evaluation::fieldName(evaluation::Field::ImageId)).toLongLong()))
                visible_events.push_back(entry);
        }
    }
    report.insert(evaluation::fieldName(evaluation::Field::ImageRecords), visible_images);
    report.insert(evaluation::fieldName(evaluation::Field::InstanceRecords), visible_events);
    report.insert(evaluation::fieldName(evaluation::Field::ImageCount), visible_images.size());
    report.insert(evaluation::fieldName(evaluation::Field::PredictionCount), visible_predictions);
    report.insert(evaluation::fieldName(evaluation::Field::EventCount), visible_events.size());
}

void validateResultProtocol(const QVariantMap &result, const QFileInfo &result_file, const QFileInfo &report_file)
{
    for (const evaluation::Field field : {evaluation::Field::SchemaVersion, evaluation::Field::ModelUuid,
                                          evaluation::Field::TestTaskUuid, evaluation::Field::Method,
                                          evaluation::Field::Status, evaluation::Field::EvaluatedAt,
                                          evaluation::Field::EvaluationReport})
    {
        const QString name = evaluation::fieldName(field);
        if (!result.contains(name))
            throw std::runtime_error(QString("测试结果摘要缺少字段: %1").arg(name).toUtf8().constData());
    }
    if (result.value(evaluation::fieldName(evaluation::Field::SchemaVersion)).toInt()
            != evaluation::kResultSchemaVersion
        || result.value(evaluation::fieldName(evaluation::Field::Status)).toString()
               != taskProtocolStatusName(TaskProtocolStatus::Finished)
        || result.value(evaluation::fieldName(evaluation::Field::ModelUuid)).toString().trimmed().isEmpty()
        || result.value(evaluation::fieldName(evaluation::Field::TestTaskUuid)).toString().trimmed().isEmpty()
        || result.value(evaluation::fieldName(evaluation::Field::Method)).toString().trimmed().isEmpty())
        throw std::runtime_error("测试结果摘要无效");

    const QString task_root = result_file.absoluteDir().absolutePath();
    const QString configured_report = resolveTaskReference(
        result_file, result.value(evaluation::fieldName(evaluation::Field::EvaluationReport)).toString());
    if (configured_report.isEmpty() || !pathWithin(task_root, configured_report)
        || QFileInfo(configured_report).absoluteFilePath().compare(report_file.absoluteFilePath(), Qt::CaseInsensitive) != 0)
    {
        throw std::runtime_error("测试结果 evaluation_report 引用无效");
    }
}

EvaluationMetricRecord metricFromMap(const QString &key, const QVariantMap &map, const QString &fallback_label = {})
{
    EvaluationMetricRecord metric;
    metric.key = key;
    metric.label = fallback_label.isEmpty() ? key : fallback_label;
    metric.class_name = textValue(map, evaluation::Field::ClassName);
    metric.class_id = intValue(map, evaluation::Field::ClassId);
    metric.precision = realValue(map, evaluation::Field::Precision);
    metric.recall = realValue(map, evaluation::Field::Recall);
    metric.f1 = realValue(map, evaluation::Field::F1);
    metric.tp = longValue(map, evaluation::Field::Tp);
    metric.fp = longValue(map, evaluation::Field::Fp);
    metric.fn = longValue(map, evaluation::Field::Fn);
    metric.precision_defined = map.contains(evaluation::fieldName(evaluation::Field::PrecisionDefined))
        ? map.value(evaluation::fieldName(evaluation::Field::PrecisionDefined)).toBool() : metric.tp + metric.fp > 0;
    metric.recall_defined = map.contains(evaluation::fieldName(evaluation::Field::RecallDefined))
        ? map.value(evaluation::fieldName(evaluation::Field::RecallDefined)).toBool() : metric.tp + metric.fn > 0;
    metric.f1_defined = map.contains(evaluation::fieldName(evaluation::Field::F1Defined))
        ? map.value(evaluation::fieldName(evaluation::Field::F1Defined)).toBool() : metric.precision_defined && metric.recall_defined;
    return metric;
}

EvaluationInstanceRecord instanceFromMap(const QVariantMap &map)
{
    EvaluationInstanceRecord record;
    record.event_uuid = textValue(map, evaluation::Field::EventUuid);
    record.image_id = longValue(map, evaluation::Field::ImageId, -1);
    record.dataset_id = longValue(map, evaluation::Field::DatasetId, -1);
    record.image_name = textValue(map, evaluation::Field::ImageName);
    record.image_path = textValue(map, evaluation::Field::ImagePath);
    record.image_width = intValue(map, evaluation::Field::ImageWidth, 0);
    record.image_height = intValue(map, evaluation::Field::ImageHeight, 0);
    record.status = evaluation::statusFromKey(textValue(map, evaluation::Field::Status));
    record.score = realValue(map, evaluation::Field::Score);
    record.iou = realValue(map, evaluation::Field::Iou);
    record.gt_label_id = longValue(map, evaluation::Field::GtLabelId, -1);
    record.gt_instance_id = record.gt_label_id >= 0 ? QString::number(record.gt_label_id) : QString();
    record.pred_instance_id = textValue(map, evaluation::Field::PredInstanceId);
    record.gt_class_id = intValue(map, evaluation::Field::GtClassId);
    record.pred_class_id = intValue(map, evaluation::Field::PredClassId);
    record.gt_class = textValue(map, evaluation::Field::GtClassName);
    record.pred_class = textValue(map, evaluation::Field::PredClassName);
    record.gt_geometry = map.value(evaluation::fieldName(evaluation::Field::GtGeometry)).toMap();
    record.pred_geometry = map.value(evaluation::fieldName(evaluation::Field::PredGeometry)).toMap();
    record.gt_bounds = record.gt_geometry.value(evaluation::fieldName(evaluation::Field::Bounds)).toMap();
    record.pred_bounds = record.pred_geometry.value(evaluation::fieldName(evaluation::Field::Bounds)).toMap();
    record.crop_bounds = map.value(evaluation::fieldName(evaluation::Field::CropBounds)).toMap();
    record.gt_overlay_bounds = map.value(evaluation::fieldName(evaluation::Field::GtOverlayBounds)).toMap();
    record.pred_overlay_bounds = map.value(evaluation::fieldName(evaluation::Field::PredOverlayBounds)).toMap();
    record.gt_overlay_points = map.value(evaluation::fieldName(evaluation::Field::GtOverlayPoints)).toList();
    record.pred_overlay_points = map.value(evaluation::fieldName(evaluation::Field::PredOverlayPoints)).toList();
    record.gt_mask_url = textValue(map, evaluation::Field::GtMaskUrl);
    record.pred_mask_url = textValue(map, evaluation::Field::PredMaskUrl);
    if (record.image_name.isEmpty())
        record.image_name = QFileInfo(record.image_path).fileName();
    return record;
}

EvaluationImageRecord imageFromMap(const QVariantMap &map)
{
    EvaluationImageRecord record;
    record.image_id = longValue(map, evaluation::Field::ImageId, -1);
    record.dataset_id = longValue(map, evaluation::Field::DatasetId, -1);
    record.image_name = textValue(map, evaluation::Field::ImageName);
    record.image_path = textValue(map, evaluation::Field::ImagePath);
    record.image_width = intValue(map, evaluation::Field::ImageWidth, 0);
    record.image_height = intValue(map, evaluation::Field::ImageHeight, 0);
    for (const QVariant &value : map.value(evaluation::fieldName(evaluation::Field::GtInstances)).toList())
    {
        const QVariantMap item = value.toMap();
        EvaluationGroundTruthRecord gt;
        gt.label_id = longValue(item, evaluation::Field::LabelId, -1);
        gt.class_id = intValue(item, evaluation::Field::ClassId, -1);
        gt.class_name = textValue(item, evaluation::Field::ClassName);
        gt.geometry = item.value(evaluation::fieldName(evaluation::Field::Geometry)).toMap();
        record.gt_instances.push_back(std::move(gt));
    }
    for (const QVariant &value : map.value(evaluation::fieldName(evaluation::Field::Predictions)).toList())
    {
        const QVariantMap item = value.toMap();
        EvaluationPredictionRecord prediction;
        prediction.prediction_id = textValue(item, evaluation::Field::PredictionId);
        prediction.class_id = intValue(item, evaluation::Field::ClassId, -1);
        prediction.class_name = textValue(item, evaluation::Field::ClassName);
        prediction.score = realValue(item, evaluation::Field::Score);
        prediction.geometry = item.value(evaluation::fieldName(evaluation::Field::Geometry)).toMap();
        record.predictions.push_back(std::move(prediction));
    }
    if (record.image_name.isEmpty())
        record.image_name = QFileInfo(record.image_path).fileName();
    return record;
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

bool isAnomalyImage(const EvaluationImageRecord &record, const double threshold, const bool predicted)
{
    const QList<int> classes = predicted ? predClassIds(record, threshold) : gtClassIds(record);
    return classes.contains(1);
}

double imageScore(const EvaluationImageRecord &record)
{
    return record.max_prediction_score;
}

struct ScoreHistogramData
{
    QVariantList labels;
    QVariantList good_points;
    QVariantList anomaly_points;
    std::vector<double> centers;
    int max_count{0};
    double min_score{0.0};
    double max_score{1.0};
};

ScoreHistogramData scoreHistogram(const QVariantList &good_scores, const QVariantList &anomaly_scores)
{
    ScoreHistogramData histogram;
    std::vector<double> good_values;
    std::vector<double> anomaly_values;
    const auto collect = [](const QVariantList &source, std::vector<double> &target)
    {
        for (const QVariant &value : source)
        {
            bool ok = false;
            const double score = value.toDouble(&ok);
            if (ok && std::isfinite(score))
                target.push_back(score);
        }
    };
    collect(good_scores, good_values);
    collect(anomaly_scores, anomaly_values);

    std::vector<double> all_values;
    all_values.reserve(good_values.size() + anomaly_values.size());
    all_values.insert(all_values.end(), good_values.cbegin(), good_values.cend());
    all_values.insert(all_values.end(), anomaly_values.cbegin(), anomaly_values.cend());
    if (all_values.empty())
        return histogram;

    const auto minmax = std::minmax_element(all_values.cbegin(), all_values.cend());
    histogram.min_score = *minmax.first;
    histogram.max_score = *minmax.second;
    constexpr int bin_count = 24;
    int actual_bin_count = bin_count;
    if (std::abs(histogram.max_score - histogram.min_score) <= 1e-12)
    {
        const double padding = std::max(0.01, std::abs(histogram.min_score) * 0.05);
        histogram.min_score -= padding;
        histogram.max_score += padding;
        actual_bin_count = 1;
    }
    const double bin_width = (histogram.max_score - histogram.min_score) / actual_bin_count;
    std::vector<int> good_counts(static_cast<std::size_t>(actual_bin_count), 0);
    std::vector<int> anomaly_counts(static_cast<std::size_t>(actual_bin_count), 0);
    histogram.centers.reserve(actual_bin_count);
    histogram.labels.reserve(actual_bin_count);
    for (int index = 0; index < actual_bin_count; ++index)
    {
        const double center = histogram.min_score + (static_cast<double>(index) + 0.5) * bin_width;
        histogram.centers.push_back(center);
        histogram.labels.push_back(QString::number(center, 'f', 4));
    }

    const auto addToBins = [&](const std::vector<double> &values, std::vector<int> &counts)
    {
        for (const double score : values)
        {
            const int index = std::clamp(
                static_cast<int>(std::floor((score - histogram.min_score) / bin_width)), 0, actual_bin_count - 1);
            ++counts.at(static_cast<std::size_t>(index));
        }
    };
    addToBins(good_values, good_counts);
    addToBins(anomaly_values, anomaly_counts);

    const auto makePoints = [&histogram](const std::vector<int> &counts)
    {
        QVariantList points;
        points.reserve(static_cast<int>(counts.size()));
        for (std::size_t index = 0; index < counts.size(); ++index)
            points.push_back(QVariantMap{{QStringLiteral("x"), histogram.centers.at(index)},
                                         {QStringLiteral("y"), counts.at(index)}});
        return points;
    };
    histogram.good_points = makePoints(good_counts);
    histogram.anomaly_points = makePoints(anomaly_counts);
    histogram.max_count = std::max(*std::max_element(good_counts.cbegin(), good_counts.cend()),
                                   *std::max_element(anomaly_counts.cbegin(), anomaly_counts.cend()));
    return histogram;
}

QVariantMap anomalyScoreChart(const QVariantList &good_scores, const QVariantList &anomaly_scores,
                              const bool has_good, const double good_max, const bool has_anomaly,
                              const double anomaly_min)
{
    constexpr const char *good_color = "#43A047";
    constexpr const char *good_fill = "rgba(67, 160, 71, 0.24)";
    constexpr const char *anomaly_color = "#E53935";
    constexpr const char *anomaly_fill = "rgba(229, 57, 53, 0.24)";
    const ScoreHistogramData histogram = scoreHistogram(good_scores, anomaly_scores);
    const auto distributionDataset = [](const QString &label, const QString &line_color,
                                        const QString &fill_color, const QVariantList &points)
    {
        return QVariantMap{{QStringLiteral("label"), label},
                           {QStringLiteral("data"), points},
                           {QStringLiteral("backgroundColor"), fill_color},
                           {QStringLiteral("borderColor"), line_color},
                           {QStringLiteral("pointBackgroundColor"), line_color},
                           {QStringLiteral("pointBorderColor"), line_color},
                           {QStringLiteral("pointRadius"), 0},
                           {QStringLiteral("pointHoverRadius"), 4},
                           {QStringLiteral("borderWidth"), 2},
                           {QStringLiteral("lineTension"), 0},
                           {QStringLiteral("showLine"), true},
                           {QStringLiteral("fill"), true}};
    };
    const auto referenceDataset = [](const QString &label, const QString &color, const double value,
                                     const int max_count)
    {
        return QVariantMap{{QStringLiteral("label"), label},
                           {QStringLiteral("data"), QVariantList{
                               QVariantMap{{QStringLiteral("x"), value}, {QStringLiteral("y"), 0}},
                               QVariantMap{{QStringLiteral("x"), value}, {QStringLiteral("y"), max_count}}}},
                           {QStringLiteral("backgroundColor"), color},
                           {QStringLiteral("borderColor"), color},
                           {QStringLiteral("borderWidth"), 2},
                           {QStringLiteral("borderDash"), QVariantList{6, 4}},
                           {QStringLiteral("pointRadius"), 0},
                           {QStringLiteral("pointHoverRadius"), 0},
                           {QStringLiteral("lineTension"), 0},
                           {QStringLiteral("showLine"), true},
                           {QStringLiteral("fill"), false}};
    };

    QVariantList datasets;
    if (has_good)
        datasets.push_back(distributionDataset(QStringLiteral("GOOD"), QString::fromLatin1(good_color),
                                                QString::fromLatin1(good_fill), histogram.good_points));
    if (has_anomaly)
        datasets.push_back(distributionDataset(QStringLiteral("Anomaly"), QString::fromLatin1(anomaly_color),
                                                QString::fromLatin1(anomaly_fill), histogram.anomaly_points));
    if (has_good)
        datasets.push_back(referenceDataset(
            QStringLiteral("GOOD 最大分数：%1").arg(QString::number(good_max, 'f', 4)),
            QString::fromLatin1(good_color), good_max, histogram.max_count));
    if (has_anomaly)
        datasets.push_back(referenceDataset(
            QStringLiteral("Anomaly 最小分数：%1").arg(QString::number(anomaly_min, 'f', 4)),
            QString::fromLatin1(anomaly_color), anomaly_min, histogram.max_count));

    const double suggested_count = histogram.max_count > 0 ? histogram.max_count * 1.1 : 1.0;
    const QVariantMap options{
        {QStringLiteral("maintainAspectRatio"), false},
        {QStringLiteral("responsive"), true},
        {QStringLiteral("legend"), QVariantMap{{QStringLiteral("display"), true},
                                                 {QStringLiteral("position"), QStringLiteral("top")}}},
        {QStringLiteral("tooltips"), QVariantMap{{QStringLiteral("mode"), QStringLiteral("nearest")},
                                                  {QStringLiteral("intersect"), false}}},
        {QStringLiteral("scales"), QVariantMap{
            {QStringLiteral("xAxes"), QVariantList{QVariantMap{
                {QStringLiteral("type"), QStringLiteral("linear")},
                {QStringLiteral("display"), true},
                {QStringLiteral("ticks"), QVariantMap{{QStringLiteral("min"), histogram.min_score},
                                                       {QStringLiteral("max"), histogram.max_score},
                                                       {QStringLiteral("maxTicksLimit"), 12},
                                                       {QStringLiteral("maxRotation"), 0},
                                                       {QStringLiteral("minRotation"), 0}}},
                {QStringLiteral("scaleLabel"), QVariantMap{{QStringLiteral("display"), true},
                                                             {QStringLiteral("labelString"), QStringLiteral("分数")}}}}}},
            {QStringLiteral("yAxes"), QVariantList{QVariantMap{
                {QStringLiteral("type"), QStringLiteral("linear")},
                {QStringLiteral("display"), true},
                {QStringLiteral("ticks"), QVariantMap{{QStringLiteral("beginAtZero"), true},
                                                        {QStringLiteral("suggestedMax"), suggested_count}}},
                {QStringLiteral("scaleLabel"), QVariantMap{{QStringLiteral("display"), true},
                                                             {QStringLiteral("labelString"), QStringLiteral("数量")}}}}}}}}};

    return QVariantMap{{evaluation::fieldName(evaluation::Field::Kind), QStringLiteral("line")},
                       {evaluation::fieldName(evaluation::Field::ChartId),
                        QStringLiteral("anomaly_score_distribution")},
                       {evaluation::fieldName(evaluation::Field::FilterKind), QStringLiteral("image_score")},
                       {evaluation::fieldName(evaluation::Field::Title), QStringLiteral("异常分数分布（图像级 pred_score）")},
                       {evaluation::fieldName(evaluation::Field::Data),
                        QVariantMap{{evaluation::fieldName(evaluation::Field::Labels), histogram.labels},
                                    {evaluation::fieldName(evaluation::Field::Datasets), datasets}}},
                       {evaluation::fieldName(evaluation::Field::Options), options}};
}

struct EvaluationAggregateInput
{
    struct InstanceEvent
    {
        evaluation::Status status{evaluation::Status::Unknown};
        QString gt_class;
        QString pred_class;
        int gt_class_id{-1};
        int pred_class_id{-1};
    };

    QList<InstanceEvent> instances;
    QList<EvaluationImageRecord> images;
    QMap<int, QString> class_catalog;
    QList<QVariantMap> chart_descriptors;
    QVariantList class_ids;
    double confidence_threshold{0.5};
    double iou_threshold{0.5};
    evaluation::MatchingStrategy matching_strategy{evaluation::MatchingStrategy::GreedyIoU};
    bool has_instance_metrics{false};
    bool has_image_metrics{false};
    bool has_confusion_matrix{false};
    bool anomaly_detection{false};
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
    QVariantMap source = value;
    if (source.contains(QStringLiteral("bounds")))
        source = source.value(QStringLiteral("bounds")).toMap();
    if (source.contains(QStringLiteral("values")))
    {
        const QVariantList values = source.value(QStringLiteral("values")).toList();
        if (values.size() >= 4)
        {
            bool ok[4] = {false, false, false, false};
            const double x = values.at(0).toDouble(&ok[0]);
            const double y = values.at(1).toDouble(&ok[1]);
            const double width = values.at(2).toDouble(&ok[2]);
            const double height = values.at(3).toDouble(&ok[3]);
            if (ok[0] && ok[1] && ok[2] && ok[3])
                return {x, y, width, height, width > 0.0 && height > 0.0};
        }
    }
    bool x_ok = false;
    bool y_ok = false;
    bool width_ok = false;
    bool height_ok = false;
    AggregateBox box;
    box.x = source.value(QStringLiteral("x")).toDouble(&x_ok);
    box.y = source.value(QStringLiteral("y")).toDouble(&y_ok);
    box.width = source.value(QStringLiteral("width")).toDouble(&width_ok);
    box.height = source.value(QStringLiteral("height")).toDouble(&height_ok);
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
                                        const double threshold, const evaluation::MatchingStrategy strategy)
{
    const bool use_hungarian = strategy == evaluation::MatchingStrategy::HungarianIoU;
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
                const double overlap = aggregateIou(predictions.at(prediction).geometry,
                                                    ground_truth.at(gt).geometry);
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
                const double overlap = aggregateIou(predictions.at(prediction).geometry,
                                                    ground_truth.at(gt).geometry);
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
                                          const evaluation::MatchingStrategy matching_strategy)
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
    const QString matrix_fn = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::FalseNegative);
    const QString matrix_fp = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::FalsePositive);
    const QString matrix_total = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::Total);
    QMap<int, AggregateCounts> classes;
    QMap<int, QString> class_names = input.class_catalog;
    QMap<QString, qint64> matrix;
    AggregateCounts overall;
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

    // Anomaly projects are evaluated at image level.  A GOOD image has no
    // ground-truth label in the dataset manifest, so the instance-event
    // matrix above cannot represent true negatives (and anomaly projects do
    // not produce instance events).  Build the binary image matrix explicitly
    // while retaining the same FP/FN/total row and column layout as detection.
    if (input.anomaly_detection)
    {
        class_names.clear();
        class_names.insert(0, QStringLiteral("GOOD"));
        class_names.insert(1, QStringLiteral("Anomaly"));
        matrix.clear();
        for (const EvaluationImageRecord &image : input.images)
        {
            const bool ground_truth_anomaly = isAnomalyImage(image, input.confidence_threshold, false);
            const bool predicted_anomaly = isAnomalyImage(image, input.confidence_threshold, true);
            const QString row = predicted_anomaly ? QStringLiteral("1") : QStringLiteral("0");
            const QString column = ground_truth_anomaly ? QStringLiteral("1") : QStringLiteral("0");
            ++matrix[row + QLatin1Char('\x1f') + column];
        }
    }

    AggregateCounts image_counts;
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
            const bool has_gt = hasGroundTruth(image) && input.class_ids.isEmpty();
            const bool has_pred = hasPredictions(image, input.confidence_threshold) && input.class_ids.isEmpty();
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
        if (parts.at(0) == matrix_fn)
            unmatched_fn += it.value();
        else
            pred_totals[parts.at(0).toInt()] += it.value();
        if (parts.at(1) == matrix_fp)
            unmatched_fp += it.value();
        else
        {
            gt_totals[parts.at(1).toInt()] += it.value();
            if (parts.at(0) != matrix_fn && parts.at(1) != matrix_fp)
                matched_pairs += it.value();
        }
    }
    const auto append_cell = [&output, &class_names, &matrix_fn, &matrix_total, &matrix_fp](
        const QString &row, const QString &column, const qint64 count, const evaluation::CellKind kind,
        const bool selectable, const bool diagonal, const bool error)
    {
        const bool row_fn = row == matrix_fn;
        const bool row_total = row == matrix_total;
        const bool column_fp = column == matrix_fp;
        const bool column_total = column == matrix_total;
        const int row_id = row_fn || row_total ? -1 : row.toInt();
        const int column_id = column_fp || column_total ? -1 : column.toInt();
        const QString total = QString("合计");
        output.confusion.push_back({row, column,
                                    row_fn ? matrix_fn : (row_total ? total : class_names.value(row_id)),
                                    column_fp ? matrix_fp
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
                            row_it.key() == column_it.key() ? evaluation::CellKind::Match
                                                            : evaluation::CellKind::ClassMismatch,
                            true, row_it.key() == column_it.key(), row_it.key() != column_it.key());
            }
            append_cell(row, matrix_fp, matrix.value(row + QLatin1Char('\x1f') + matrix_fp),
                        evaluation::CellKind::FalsePositive, true, false, true);
            append_cell(row, matrix_total, pred_totals.value(row_it.key()), evaluation::CellKind::PredTotal,
                        true, false, false);
        }
        for (auto column_it = class_names.cbegin(); column_it != class_names.cend(); ++column_it)
        {
            const QString column = QString::number(column_it.key());
            append_cell(matrix_fn, column,
                        matrix.value(matrix_fn + QLatin1Char('\x1f') + column),
                        evaluation::CellKind::FalseNegative, true, false, true);
        }
        append_cell(matrix_fn, matrix_fp, 0, evaluation::CellKind::NotApplicable, false, false, false);
        append_cell(matrix_fn, matrix_total, unmatched_fn,
                    evaluation::CellKind::FalseNegativeTotal, true, false, true);
        for (auto column_it = class_names.cbegin(); column_it != class_names.cend(); ++column_it)
        {
            const QString column = QString::number(column_it.key());
            append_cell(matrix_total, column, gt_totals.value(column_it.key()), evaluation::CellKind::GtTotal,
                        true, false, false);
        }
        append_cell(matrix_total, matrix_fp, unmatched_fp,
                    evaluation::CellKind::FalsePositiveTotal, true, false, true);
        append_cell(matrix_total, matrix_total, matched_pairs + unmatched_fp + unmatched_fn,
                    evaluation::CellKind::All, true, false, false);
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
        output.charts.push_back({{evaluation::fieldName(evaluation::Field::Kind), QStringLiteral("bar")},
                                 {evaluation::fieldName(evaluation::Field::ChartId),
                                  QStringLiteral("per_class_metrics")},
                                 {evaluation::fieldName(evaluation::Field::FilterKind),
                                  QStringLiteral("per_class_metrics")},
                                 {evaluation::fieldName(evaluation::Field::Title), QString("按类别指标")},
                                 {evaluation::fieldName(evaluation::Field::Data), QVariantMap{
                                     {evaluation::fieldName(evaluation::Field::Labels), labels},
                                     {evaluation::fieldName(evaluation::Field::Datasets), QVariantList{
                                         QVariantMap{{evaluation::fieldName(evaluation::Field::Label),
                                                      QStringLiteral("Precision")},
                                                     {evaluation::fieldName(evaluation::Field::Data), precision}},
                                         QVariantMap{{evaluation::fieldName(evaluation::Field::Label),
                                                      QStringLiteral("Recall")},
                                                     {evaluation::fieldName(evaluation::Field::Data), recall}},
                                         QVariantMap{{evaluation::fieldName(evaluation::Field::Label),
                                                      QStringLiteral("F1")},
                                                     {evaluation::fieldName(evaluation::Field::Data), f1}}}}}},
                                  {evaluation::fieldName(evaluation::Field::Options),
                                   QVariantMap{{QStringLiteral("maintainAspectRatio"), false}}}});
    }

    if (input.anomaly_detection)
    {
        QVariantList good_scores;
        QVariantList anomaly_scores;
        bool has_good = false;
        bool has_anomaly = false;
        double good_max = 0.0;
        double anomaly_min = std::numeric_limits<double>::max();
        for (const EvaluationImageRecord &image : input.images)
        {
            const double score = imageScore(image);
            if (gtClassIds(image).contains(1))
            {
                good_scores.push_back(QVariant());
                anomaly_scores.push_back(score);
                has_anomaly = true;
                anomaly_min = std::min(anomaly_min, score);
            }
            else
            {
                good_scores.push_back(score);
                anomaly_scores.push_back(QVariant());
                has_good = true;
                good_max = std::max(good_max, score);
            }
        }
        output.charts.push_back(anomalyScoreChart(good_scores, anomaly_scores, has_good, good_max,
                                                  has_anomaly, anomaly_min));
    }

    for (const QVariantMap &descriptor : input.chart_descriptors)
    {
        const QString chart_id = descriptor.value(evaluation::fieldName(evaluation::Field::ChartId)).toString();
        const QString filter_kind = descriptor.value(evaluation::fieldName(evaluation::Field::FilterKind)).toString();
        if (chart_id == QStringLiteral("anomaly_score_distribution"))
            continue;
        if (descriptor.value(evaluation::fieldName(evaluation::Field::Kind)).toString() == QStringLiteral("bar")
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
            filtered.insert(evaluation::fieldName(evaluation::Field::Data), QVariantMap{
                {evaluation::fieldName(evaluation::Field::Labels), labels},
                {evaluation::fieldName(evaluation::Field::Datasets), QVariantList{
                    QVariantMap{{evaluation::fieldName(evaluation::Field::Label), QStringLiteral("Precision")},
                                {evaluation::fieldName(evaluation::Field::Data), precision}},
                    QVariantMap{{evaluation::fieldName(evaluation::Field::Label), QStringLiteral("Recall")},
                                {evaluation::fieldName(evaluation::Field::Data), recall}}}}});
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
            filtered.insert(evaluation::fieldName(evaluation::Field::Data), QVariantMap{
                {evaluation::fieldName(evaluation::Field::Labels), labels},
                {evaluation::fieldName(evaluation::Field::Datasets), QVariantList{
                    QVariantMap{{evaluation::fieldName(evaluation::Field::Label), QStringLiteral("score")},
                                {evaluation::fieldName(evaluation::Field::Data), scores}}}}});
        }
        output.charts.push_back(std::move(filtered));
    }
    return output;
}

} // namespace

ModelEvaluationViewModel::ModelEvaluationViewModel(QObject *parent)
    : QObject(parent)
    , state_(evaluation::viewStateKey(evaluation::ViewState::NotRun))
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
QString ModelEvaluationViewModel::reportPath() const { return report_path_; }

void ModelEvaluationViewModel::setReportPath(const QString &path)
{
    setPaths(path, result_path_);
}

QString ModelEvaluationViewModel::resultPath() const { return result_path_; }

void ModelEvaluationViewModel::setResultPath(const QString &path)
{
    setPaths(report_path_, path);
}

void ModelEvaluationViewModel::setPaths(const QString &reportPath, const QString &resultPath)
{
    const QString report = QDir::fromNativeSeparators(reportPath.trimmed());
    const QString result = QDir::fromNativeSeparators(resultPath.trimmed());
    const bool report_changed = report_path_ != report;
    const bool result_changed = result_path_ != result;
    if (!report_changed && !result_changed)
        return;
    report_path_ = report;
    result_path_ = result;
    if (report_changed || result_changed)
    {
        loaded_report_size_ = -1;
        loaded_report_mtime_ = -1;
        loaded_result_size_ = -1;
        loaded_result_mtime_ = -1;
    }
    if (report_changed)
        emit reportPathChanged();
    if (result_changed)
        emit resultPathChanged();
    reload();
}

QString ModelEvaluationViewModel::primaryMetricSet() const { return primary_metric_set_; }
bool ModelEvaluationViewModel::globalFilterActive() const
{
    if (global_filter_ == nullptr)
        return false;
    bool active = false;
    if (hasInvokable(global_filter_, "isActive", 0))
        QMetaObject::invokeMethod(global_filter_, "isActive", Qt::DirectConnection, Q_RETURN_ARG(bool, active));
    return active;
}

QString ModelEvaluationViewModel::globalFilterDescription() const
{
    if (global_filter_ != nullptr)
    {
        QString description;
        if (hasInvokable(global_filter_, "description", 0)
            && QMetaObject::invokeMethod(global_filter_, "description", Qt::DirectConnection,
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
        state_ = evaluation::viewStateKey(evaluation::ViewState::Loading);
    else if (state_ == evaluation::viewStateKey(evaluation::ViewState::Loading))
        state_ = available_ ? evaluation::viewStateKey(evaluation::ViewState::Ready)
                            : (error_.isEmpty() ? evaluation::viewStateKey(evaluation::ViewState::NotRun)
                                                : evaluation::viewStateKey(evaluation::ViewState::Error));
    emit loadingChanged();
}

void ModelEvaluationViewModel::clearReport(const QString &error, const QString &state)
{
    ++aggregation_revision_;
    available_ = false;
    error_ = error;
    state_ = state.isEmpty()
        ? (error.isEmpty() ? evaluation::viewStateKey(evaluation::ViewState::NotRun)
                           : evaluation::viewStateKey(evaluation::ViewState::Error))
        : state;
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
    anomaly_detection_ = false;
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
    filtered_instances_->setMinScore(-std::numeric_limits<double>::infinity());
    filtered_instances_->setMaxScore(std::numeric_limits<double>::infinity());
    charts_->setRecords({});
    selected_instance_.clear();
    selected_proxy_row_ = -1;
    instances_->setSelectedEvent({});
}

void ModelEvaluationViewModel::reload()
{
    const int revision = ++reload_revision_;
    if (report_path_.isEmpty() || result_path_.isEmpty())
    {
        setLoading(true);
        clearReport();
        state_ = report_path_.isEmpty() ? evaluation::viewStateKey(evaluation::ViewState::MissingReport)
                                        : evaluation::viewStateKey(evaluation::ViewState::MissingResult);
        setLoading(false);
        emit reportChanged();
        emit selectedInstanceChanged();
        return;
    }
    const QString report_path = report_path_;
    const QString result_path = result_path_;
    const QFileInfo report_file(report_path);
    const QFileInfo result_file(result_path);
    const qint64 report_size = report_file.exists() && report_file.isFile() ? report_file.size() : -1;
    const qint64 report_mtime = report_file.exists() && report_file.isFile()
        ? report_file.lastModified().toMSecsSinceEpoch() : -1;
    const qint64 result_size = result_file.exists() && result_file.isFile() ? result_file.size() : -1;
    const qint64 result_mtime = result_file.exists() && result_file.isFile()
        ? result_file.lastModified().toMSecsSinceEpoch() : -1;
    if (available_ && !loading_ && report_size == loaded_report_size_ && report_mtime == loaded_report_mtime_
        && result_size == loaded_result_size_ && result_mtime == loaded_result_mtime_)
        return;
    setLoading(true);
    clearReport();
    const QPointer<ModelEvaluationViewModel> guard(this);
    QThreadPool::globalInstance()->start([guard, revision, report_path, result_path, report_size, report_mtime,
                                          result_size, result_mtime]()
    {
        if (guard.isNull())
            return;
        QVariantMap report;
        QVariantList instance_records;
        QString error;
        QString failure_state;
        try
        {
            const QFileInfo file(report_path);
            if (!file.exists() || !file.isFile())
            {
                failure_state = evaluation::viewStateKey(evaluation::ViewState::MissingReport);
                throw std::runtime_error("评估报告不存在");
            }
            const QFileInfo result_file(result_path);
            if (!result_file.exists() || !result_file.isFile())
            {
                failure_state = evaluation::viewStateKey(evaluation::ViewState::MissingResult);
                throw std::runtime_error("测试结果摘要不存在");
            }
            if (result_file.size() > kMaxEvaluationYamlBytes)
                throw std::runtime_error("测试结果摘要超过大小限制");
            const YAML::Node result_root = common::yaml::loadFile(result_file);
            if (!result_root || !result_root.IsMap())
                throw std::runtime_error("测试结果摘要不是 YAML map");
            const QVariantMap result_map = common::yaml::nodeVariant(result_root).toMap();
            validateResultProtocol(result_map, result_file, file);
            const YAML::Node root = common::yaml::loadFile(file);
            if (!root || !root.IsMap())
                throw std::runtime_error("评估报告不是 YAML map");
            report = common::yaml::nodeVariant(root).toMap();
            validateReportProtocol(report, file);
            filterUnavailableReportDetails(
                report,
                resolveReportReference(file.absolutePath(),
                                       report.value(evaluation::fieldName(evaluation::Field::DatasetManifest)).toString()));
            const auto validateRecords = [&report](const QVariantList &records)
            {
                if (records.size() > static_cast<int>(kMaxEvaluationRecords))
                    throw std::runtime_error("评估实例 records 数量超过限制");
                QSet<QString> event_ids;
                QSet<qint64> image_ids;
                for (const QVariant &entry : report.value(evaluation::fieldName(evaluation::Field::ImageRecords)).toList())
                    image_ids.insert(entry.toMap().value(evaluation::fieldName(evaluation::Field::ImageId)).toLongLong());
                for (const QVariant &entry : records)
                {
                    const QVariantMap value = entry.toMap();
                    const QString event_uuid = value.value(evaluation::fieldName(evaluation::Field::EventUuid))
                                                   .toString()
                                                   .trimmed();
                    const QString status = value.value(evaluation::fieldName(evaluation::Field::Status))
                                               .toString()
                                               .trimmed();
                    bool score_ok = false;
                    bool iou_ok = false;
                    const double score = value.value(evaluation::fieldName(evaluation::Field::Score)).toDouble(&score_ok);
                    const double iou = value.value(evaluation::fieldName(evaluation::Field::Iou)).toDouble(&iou_ok);
                    for (const evaluation::Field field : {evaluation::Field::EventUuid, evaluation::Field::ImageId,
                                                          evaluation::Field::Status, evaluation::Field::Score,
                                                          evaluation::Field::Iou, evaluation::Field::GtLabelId,
                                                          evaluation::Field::GtClassId, evaluation::Field::GtClassName,
                                                          evaluation::Field::GtGeometry, evaluation::Field::PredInstanceId,
                                                          evaluation::Field::PredClassId, evaluation::Field::PredClassName,
                                                          evaluation::Field::PredGeometry, evaluation::Field::CropBounds,
                                                          evaluation::Field::GtOverlayBounds,
                                                          evaluation::Field::PredOverlayBounds,
                                                          evaluation::Field::GtOverlayPoints,
                                                          evaluation::Field::PredOverlayPoints,
                                                          evaluation::Field::GtMaskUrl, evaluation::Field::PredMaskUrl})
                    {
                        const QString name = evaluation::fieldName(field);
                        if (!value.contains(name))
                            throw std::runtime_error(QString("评估实例缺少字段: %1").arg(name).toUtf8().constData());
                    }
                    for (const QString &field : {QStringLiteral("gt"), QStringLiteral("pred"),
                                                 evaluation::fieldName(evaluation::Field::DatasetId),
                                                 evaluation::fieldName(evaluation::Field::ImageName),
                                                 evaluation::fieldName(evaluation::Field::ImagePath),
                                                 evaluation::fieldName(evaluation::Field::ImageWidth),
                                                 evaluation::fieldName(evaluation::Field::ImageHeight),
                                                 QStringLiteral("gt_instance_id"),
                                                 QStringLiteral("gt_class_color"),
                                                 QStringLiteral("pred_class_color"),
                                                 QStringLiteral("gt_bounds"),
                                                 QStringLiteral("pred_bounds")})
                        if (value.contains(field))
                            throw std::runtime_error(QString("评估实例包含已删除字段: %1").arg(field).toUtf8().constData());
                    if (value.isEmpty() || event_uuid.isEmpty() || event_ids.contains(event_uuid)
                        || value.value(evaluation::fieldName(evaluation::Field::ImageId)).toLongLong() < 0
                        || !image_ids.contains(value.value(evaluation::fieldName(evaluation::Field::ImageId)).toLongLong())
                        || !score_ok || !std::isfinite(score) || !iou_ok || !std::isfinite(iou)
                        || iou < 0.0 || iou > 1.0
                        || evaluation::statusFromKey(status) == evaluation::Status::Unknown)
                        throw std::runtime_error("评估实例记录字段无效或 event_uuid 重复");
                    event_ids.insert(event_uuid);
                }
                const QVariant event_count = report.value(evaluation::fieldName(evaluation::Field::EventCount));
                if (event_count.isValid() && event_count.toLongLong() != records.size())
                    throw std::runtime_error("评估报告 event_count 与实例记录不一致");
            };

            instance_records = report.value(evaluation::fieldName(evaluation::Field::InstanceRecords)).toList();
            validateRecords(instance_records);
        }
        catch (const std::exception &exception)
        {
            error = QString("%1").arg(QString(exception.what()));
            if (failure_state.isEmpty())
                failure_state = evaluation::viewStateKey(evaluation::ViewState::InvalidReport);
        }
        QMetaObject::invokeMethod(guard.data(), [guard, revision, report = std::move(report),
                                                  instance_records = std::move(instance_records), error, failure_state,
                                                  report_size, report_mtime,
                                                  result_size, result_mtime]() mutable
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
            guard->loadReport(report);
            guard->loadInstanceRecords(instance_records);
            // Rebuild even when details were filtered to an empty set.  A
            // report whose source data changed must not keep presenting the
            // old aggregate values above an empty detail view.
            guard->rebuildFilteredAggregates();
            guard->available_ = true;
            guard->state_ = evaluation::viewStateKey(evaluation::ViewState::Ready);
            guard->error_.clear();
            guard->loaded_report_size_ = report_size;
            guard->loaded_report_mtime_ = report_mtime;
            guard->loaded_result_size_ = result_size;
            guard->loaded_result_mtime_ = result_mtime;
            guard->setLoading(false);
            emit guard->reportChanged();
            emit guard->selectedInstanceChanged();
        }, Qt::QueuedConnection);
    });
}

void ModelEvaluationViewModel::refresh()
{
    loaded_report_size_ = -1;
    loaded_report_mtime_ = -1;
    loaded_result_size_ = -1;
    loaded_result_mtime_ = -1;
    reload();
}

void ModelEvaluationViewModel::setRuntimeState(const QString &state)
{
    const QString value = state.trimmed();
    if (value.isEmpty())
        return;
    if (state_ == value && !available_ && error_.isEmpty())
        return;
    if (value == evaluation::viewStateKey(evaluation::ViewState::Running)
        || value == evaluation::viewStateKey(evaluation::ViewState::Failed)
        || value == evaluation::viewStateKey(evaluation::ViewState::NotRun))
    {
        clearReport({}, value);
        setLoading(false);
        emit reportChanged();
        emit selectedInstanceChanged();
    }
}

void ModelEvaluationViewModel::loadReport(const QVariantMap &root)
{
    const evaluation::MetricSet metric_set
        = evaluation::metricSetFromKey(root.value(evaluation::fieldName(evaluation::Field::PrimaryMetricSet)).toString());
    primary_metric_set_ = evaluation::metricSetKey(metric_set);
    result_revision_ = root.value(evaluation::fieldName(evaluation::Field::EvaluatedAt)).toString();
    metric_scope_description_ = metric_set == evaluation::MetricSet::Official
        ? QString("官方指标")
        : QString("诊断匹配指标");
    image_metric_definition_ = root.value(evaluation::fieldName(evaluation::Field::ImageMetricDefinition)).toMap();
    const QVariantMap evaluation_config = root.value(evaluation::fieldName(evaluation::Field::EvaluationConfig)).toMap();
    anomaly_detection_ = evaluation::isAnomaly(
        evaluation::methodFromKey(root.value(evaluation::fieldName(evaluation::Field::Method)).toString()));
    confidence_threshold_ = realValue(evaluation_config, evaluation::Field::ConfidenceThreshold);
    iou_threshold_ = realValue(evaluation_config, evaluation::Field::IouThreshold);
    matching_strategy_ = evaluation::matchingStrategyKey(
        evaluation::matchingStrategyFromKey(textValue(evaluation_config, evaluation::Field::MatchingStrategy)));
    const QVariantMap capabilities = root.value(evaluation::fieldName(evaluation::Field::Capabilities)).toMap();
    has_instance_metrics_ = capabilities.value(evaluation::fieldName(evaluation::Field::HasInstanceMetrics)).toBool();
    has_image_metrics_ = capabilities.value(evaluation::fieldName(evaluation::Field::HasImageMetrics)).toBool();
    has_confusion_matrix_ = capabilities.value(evaluation::fieldName(evaluation::Field::HasConfusionMatrix)).toBool()
        || anomaly_detection_;
    // Anomaly reports are image-level, but the instance grid still needs one
    // synthetic record per image so matrix selections can show GOOD/Anomaly
    // samples, including true negatives with no original event.
    has_instance_events_ = capabilities.value(evaluation::fieldName(evaluation::Field::HasInstanceEvents)).toBool()
        || anomaly_detection_;
    const QVariantMap diagnostic = root.value(evaluation::fieldName(evaluation::Field::DiagnosticMetrics)).toMap();
    const QVariantMap instance = diagnostic.value(evaluation::fieldName(evaluation::Field::Instance)).toMap();
    const QVariantMap overall = instance.value(evaluation::fieldName(evaluation::Field::Overall)).toMap();
    if (!overall.isEmpty())
        instance_metrics_->setRecords({metricFromMap(QStringLiteral("overall"), overall, QString("整体"))});
    const QVariantMap image = diagnostic.value(evaluation::fieldName(evaluation::Field::Image)).toMap();
    if (!image.isEmpty())
        image_metrics_->setRecords({metricFromMap(QStringLiteral("image"), image, QString("图像"))});

    // The report keeps official and diagnostic metrics separate.  The
    // primary set only changes which values are shown in the overall panel;
    // matrix/events always remain diagnostic records.
    const QVariantMap official = root.value(evaluation::fieldName(evaluation::Field::OfficialMetrics)).toMap();
    if (primary_metric_set_ == evaluation::metricSetKey(evaluation::MetricSet::Official)
        && official.value(evaluation::fieldName(evaluation::Field::Available)).toBool())
    {
        const QVariantMap official_instance = official.value(evaluation::fieldName(evaluation::Field::Instance)).toMap();
        if (!official_instance.isEmpty())
            instance_metrics_->setRecords({metricFromMap(QStringLiteral("overall"), official_instance,
                                                          QString("整体"))});
        const QVariantMap official_image = official.value(evaluation::fieldName(evaluation::Field::Image)).toMap();
        if (!official_image.isEmpty())
            image_metrics_->setRecords({metricFromMap(QStringLiteral("image"), official_image,
                                                       QString("图像"))});
    }

    std::vector<EvaluationImageRecord> image_records;
    for (const QVariant &value : root.value(evaluation::fieldName(evaluation::Field::ImageRecords)).toList())
        image_records.push_back(imageFromMap(value.toMap()));
    images_->setRecords(std::move(image_records));

    std::vector<EvaluationMetricRecord> per_class;
    for (const QVariant &value : instance.value(evaluation::fieldName(evaluation::Field::PerClass)).toList())
    {
        const QVariantMap map = value.toMap();
        const QString key = map.value(evaluation::fieldName(evaluation::Field::ClassId)).toString();
        per_class.push_back(metricFromMap(key, map,
                                          map.value(evaluation::fieldName(evaluation::Field::ClassName)).toString()));
    }
    per_class_metrics_->setRecords(std::move(per_class));

    std::vector<EvaluationConfusionCell> cells;
    const QVariantMap matrix = root.value(evaluation::fieldName(evaluation::Field::ConfusionMatrix)).toMap();
    const QVariantList matrix_cells = matrix.value(evaluation::fieldName(evaluation::Field::Cells)).toList();
    for (const QVariant &value : matrix_cells)
    {
        const QVariantMap map = value.toMap();
        EvaluationConfusionCell cell;
        cell.row_key = textValue(map, evaluation::Field::RowKey);
        cell.column_key = textValue(map, evaluation::Field::ColumnKey);
        cell.row_label = textValue(map, evaluation::Field::RowLabel);
        cell.column_label = textValue(map, evaluation::Field::ColumnLabel);
        cell.count = longValue(map, evaluation::Field::Count);
        cell.row_class_id = intValue(map, evaluation::Field::RowClassId);
        cell.column_class_id = intValue(map, evaluation::Field::ColumnClassId);
        cell.cell_kind = evaluation::cellKindFromKey(textValue(map, evaluation::Field::CellKind));
        cell.selectable = map.value(evaluation::fieldName(evaluation::Field::Selectable)).toBool();
        cell.diagonal = map.value(evaluation::fieldName(evaluation::Field::IsDiagonal)).toBool();
        cell.error = map.value(evaluation::fieldName(evaluation::Field::IsError)).toBool();
        cells.push_back(cell);
    }
    confusion_matrix_->setRecords(std::move(cells));
    QList<QVariantMap> charts;
    for (const QVariant &value : root.value(evaluation::fieldName(evaluation::Field::Charts)).toList())
        charts.push_back(value.toMap());
    charts_->setRecords(std::move(charts));
}

void ModelEvaluationViewModel::loadInstanceRecords(const QVariantList &records)
{
    QSet<QString> event_ids;
    QHash<qint64, const EvaluationImageRecord *> image_index;
    for (const EvaluationImageRecord &image : images_->records())
        image_index.insert(image.image_id, &image);
    std::vector<EvaluationInstanceRecord> values;
    values.reserve(static_cast<size_t>(records.size()));

    if (anomaly_detection_ && images_ != nullptr && images_->rowCount() > 0)
    {
        values.reserve(static_cast<size_t>(images_->rowCount()));
        for (const EvaluationImageRecord &image : images_->records())
        {
            const bool ground_truth_anomaly = isAnomalyImage(image, confidence_threshold_, false);
            const bool predicted_anomaly = isAnomalyImage(image, confidence_threshold_, true);

            const int gt_class_id = ground_truth_anomaly ? 1 : 0;
            const int pred_class_id = predicted_anomaly ? 1 : 0;
            EvaluationInstanceRecord value;
            value.event_uuid = QStringLiteral("image-%1").arg(image.image_id);
            value.image_id = image.image_id;
            value.dataset_id = image.dataset_id;
            value.image_name = image.image_name;
            value.image_path = image.image_path;
            value.image_width = image.image_width;
            value.image_height = image.image_height;
            value.status = gt_class_id == 1 && pred_class_id == 1
                ? evaluation::Status::TruePositive
                : (gt_class_id == 0 && pred_class_id == 0
                       ? evaluation::Status::TrueNegative
                       : (gt_class_id == 0 ? evaluation::Status::FalsePositive
                                           : evaluation::Status::FalseNegative));
            value.gt_class_id = gt_class_id;
            value.pred_class_id = pred_class_id;
            value.gt_class = gt_class_id == 1 ? QStringLiteral("Anomaly") : QStringLiteral("GOOD");
            value.pred_class = pred_class_id == 1 ? QStringLiteral("Anomaly") : QStringLiteral("GOOD");
            value.score = imageScore(image);
            value.gt_class_color = classColor(gt_class_id);
            value.pred_class_color = classColor(pred_class_id);
            if (image.image_width > 0 && image.image_height > 0)
                value.crop_bounds = QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0},
                                                {QStringLiteral("width"), image.image_width},
                                                {QStringLiteral("height"), image.image_height}};
            value.thumbnail_url = thumbnailUrl(value);
            values.push_back(std::move(value));
        }
        instances_->setRecords(std::move(values));
        return;
    }

    for (const QVariant &entry : records)
    {
        EvaluationInstanceRecord value = instanceFromMap(entry.toMap());
        const auto image = image_index.constFind(value.image_id);
        if (image != image_index.cend())
        {
            value.dataset_id = image.value()->dataset_id;
            value.image_name = image.value()->image_name;
            value.image_path = image.value()->image_path;
            value.image_width = image.value()->image_width;
            value.image_height = image.value()->image_height;
        }
        if (value.event_uuid.isEmpty() || event_ids.contains(value.event_uuid))
            continue;
        event_ids.insert(value.event_uuid);
        if (value.gt_class_color.isEmpty())
            value.gt_class_color = classColor(value.gt_class_id);
        if (value.pred_class_color.isEmpty())
            value.pred_class_color = classColor(value.pred_class_id);
        value.thumbnail_url = thumbnailUrl(value);
        values.push_back(std::move(value));
    }
    instances_->setRecords(std::move(values));
}

void ModelEvaluationViewModel::rebuildFilteredAggregates()
{
    const int revision = ++aggregation_revision_;
    EvaluationAggregateInput input;
    for (const EvaluationMetricRecord &metric : per_class_metrics_->records())
    {
        if (metric.class_id >= 0)
            input.class_catalog.insert(metric.class_id,
                                       metric.class_name.isEmpty() ? metric.label : metric.class_name);
    }
    for (const EvaluationConfusionCell &cell : confusion_matrix_->records())
    {
        if (cell.row_class_id >= 0 && !cell.row_label.isEmpty() && cell.row_label != QStringLiteral("合计"))
            input.class_catalog.insert(cell.row_class_id, cell.row_label);
        if (cell.column_class_id >= 0 && !cell.column_label.isEmpty() && cell.column_label != QStringLiteral("合计"))
            input.class_catalog.insert(cell.column_class_id, cell.column_label);
    }
    if (anomaly_detection_)
    {
        input.class_catalog.insert(0, QStringLiteral("GOOD"));
        input.class_catalog.insert(1, QStringLiteral("Anomaly"));
    }
    input.chart_descriptors = charts_->records();
    input.class_ids = global_filtered_instances_->classIds();
    input.confidence_threshold = confidence_threshold_;
    input.iou_threshold = iou_threshold_;
    input.matching_strategy = evaluation::matchingStrategyFromKey(matching_strategy_);
    input.has_instance_metrics = has_instance_metrics_;
    input.has_image_metrics = has_image_metrics_;
    input.has_confusion_matrix = has_confusion_matrix_;
    input.anomaly_detection = anomaly_detection_;

    // QSortFilterProxyModel remains the single GUI-thread filter boundary.
    // The worker receives only detached value records and never touches a
    // proxy, QModelIndex, QObject or QML object.
    for (const EvaluationInstanceRecord &record : instances_->records())
    {
        if (global_filtered_instances_->acceptsRecord(record))
            input.instances.push_back({record.status, record.gt_class, record.pred_class,
                                      record.gt_class_id, record.pred_class_id});
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
    if (global_filter_ != nullptr && hasInvokable(global_filter_, "isLabelClassFilterEnabled", 0))
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
            if (hasInvokable(global_filter_, "acceptsLabelClassId", 1)
                && QMetaObject::invokeMethod(global_filter_, "acceptsLabelClassId", Qt::DirectConnection,
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
            const QList<int> ground_truth_classes = gtClassIds(record);
            const QList<int> predicted_classes = predClassIds(record, confidence_threshold_);
            const QList<int> relevant_classes
                = ground_truth_classes.isEmpty() ? predicted_classes : ground_truth_classes;
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
        QList<EvaluationGroundTruthRecord> filtered_gt_instances;
        for (const EvaluationGroundTruthRecord &ground_truth : record.gt_instances)
            if (classAllowed(ground_truth.class_id))
                filtered_gt_instances.push_back(ground_truth);
        QList<EvaluationPredictionRecord> filtered_predictions;
        for (const EvaluationPredictionRecord &prediction : record.predictions)
            if (classAllowed(prediction.class_id))
                filtered_predictions.push_back(prediction);

        filtered.gt_instances = std::move(filtered_gt_instances);
        filtered.predictions = std::move(filtered_predictions);
        if (!hasGroundTruth(filtered) && !hasPredictions(filtered, confidence_threshold_))
            continue;
        input.images.push_back(std::move(filtered));
    }

    const QPointer<ModelEvaluationViewModel> guard(this);
    QThreadPool::globalInstance()->start([guard, revision, input = std::move(input)]() mutable
    {
        if (guard.isNull())
            return;
        EvaluationAggregateOutput output = aggregateEvaluation(input);
        QMetaObject::invokeMethod(guard, [guard, revision, output = std::move(output)]() mutable
        {
            if (guard.isNull() || guard->aggregation_revision_ != revision)
                return;
            guard->instance_metrics_->setRecords(std::move(output.instance_metrics));
            guard->image_metrics_->setRecords(std::move(output.image_metrics));
            guard->per_class_metrics_->setRecords(std::move(output.per_class_metrics));
            guard->confusion_matrix_->setRecords(std::move(output.confusion));
            guard->charts_->setRecords(std::move(output.charts));
        });
    });
    return;
}

QVariantMap ModelEvaluationViewModel::instanceToMap(const EvaluationInstanceRecord &record) const
{
    return {{QStringLiteral("eventUuid"), record.event_uuid}, {QStringLiteral("imageId"), record.image_id},
            {QStringLiteral("datasetId"), record.dataset_id}, {QStringLiteral("imageName"), record.image_name},
            {QStringLiteral("imagePath"), record.image_path}, {QStringLiteral("imageWidth"), record.image_width},
            {QStringLiteral("imageHeight"), record.image_height},
            {QStringLiteral("status"), evaluation::statusKey(record.status)},
            {QStringLiteral("statusKind"), static_cast<int>(record.status)},
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
    const auto normalizeKey = [this](QString value, const bool row)
    {
        value = value.trimmed();
        if (value.isEmpty()
            || value == evaluation::matrixAxisKey(evaluation::MatrixAxisKey::Total)
            || value == evaluation::matrixAxisKey(evaluation::MatrixAxisKey::FalseNegative)
            || value == evaluation::matrixAxisKey(evaluation::MatrixAxisKey::FalsePositive))
            return value;

        bool numeric = false;
        value.toInt(&numeric);
        if (numeric)
            return value;

        for (const EvaluationConfusionCell &cell : confusion_matrix_->records())
        {
            const QString label = row ? cell.row_label : cell.column_label;
            const QString key = row ? cell.row_key : cell.column_key;
            if (!key.isEmpty() && label.compare(value, Qt::CaseInsensitive) == 0)
                return key;
        }
        return value;
    };

    filtered_instances_->setMatrixRow(normalizeKey(rowKey, true));
    filtered_instances_->setMatrixColumn(normalizeKey(columnKey, false));
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
    filtered_instances_->setMinScore(-std::numeric_limits<double>::infinity());
    filtered_instances_->setMaxScore(std::numeric_limits<double>::infinity());
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
