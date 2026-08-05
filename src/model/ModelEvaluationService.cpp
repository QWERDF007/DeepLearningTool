#include "model/ModelEvaluationService.h"

#include "data/DatasetIO.h"
#include "data/LabelData.h"
#include "database/DataBase.h"
#include "database/ModelTaskDataBase.h"
#include "model/EvaluationCharts.h"
#include "model/EvaluationCommon.h"
#include "model/EvaluationDataset.h"
#include "model/EvaluationGeometry.h"
#include "model/EvaluationMatching.h"
#include "model/ModelDatasetSelection.h"

#include <spdlog/spdlog.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>
#include <QMetaType>
#include <QSet>
#include <QTextStream>
#include <QUrl>
#include <QVariantList>
#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>

namespace dltool::model {

namespace {

// Prediction artifacts are user-/framework-produced input.  Bound both the
// document size and sequence cardinality before JSON is converted to
// QVariant values, so malformed prediction data cannot exhaust the GUI process.
constexpr std::size_t kMaxEvaluationRecords = 5'000'000;

QString mapString(const QVariantMap &map, const QString &key, const QString &fallback = {})
{
    const QVariant value = map.value(key);
    return value.isValid() ? value.toString() : fallback;
}

int mapInt(const QVariantMap &map, const QString &key, int fallback = -1)
{
    bool      ok    = false;
    const int value = map.value(key).toInt(&ok);
    return ok ? value : fallback;
}

bool sourceImageExists(const QString &path, const QString &dataset_root)
{
    QFileInfo image(path);
    if (!image.isAbsolute() && !image.exists() && !dataset_root.isEmpty())
        image = QFileInfo(QDir(dataset_root), path);
    return image.exists() && image.isFile();
}

bool finiteNumber(const QVariant &value, double *output = nullptr)
{
    bool         ok     = false;
    const double number = value.toDouble(&ok);
    if (!ok || !std::isfinite(number))
        return false;
    if (output != nullptr)
        *output = number;
    return true;
}

bool isCancelled(const std::shared_ptr<std::atomic_bool> &cancel_token)
{
    return cancel_token != nullptr && cancel_token->load(std::memory_order_relaxed);
}

struct SourceImage
{
    qint64               id{-1};
    qint64               dataset_id{-1};
    QString              path;
    std::vector<uint8_t> extra_data;
};

struct SourceLabel
{
    qint64               id{-1};
    qint64               image_id{-1};
    qint64               class_id{-1};
    std::vector<uint8_t> data;
};

struct SourceClass
{
    QString name;
    QString group;
};

QString normalizedLabelClassGroup(const QString &group)
{
    const QString normalized = group.trimmed().toLower();
    if (normalized == QString("good") || normalized == QString("良好"))
        return QString("good");
    if (normalized == QString("unlabeled") || normalized == QString("unlabelled") || normalized == QString("未标注"))
        return QString("unlabeled");
    return QString("anomaly");
}

qint64 imageLabelClassIdFromExtraData(const std::vector<uint8_t> &extra_data)
{
    if (extra_data.empty())
        return -1;
    const QByteArray    encoded(reinterpret_cast<const char *>(extra_data.data()),
                                static_cast<qsizetype>(extra_data.size()));
    QJsonParseError     parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(encoded, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        return -1;
    return document.object().value(QString("image_label_class_id")).toInteger(-1);
}

QString labelClassGroupFromExtraData(const std::vector<uint8_t> &extra_data)
{
    if (extra_data.empty())
        return QString("anomaly");
    const QByteArray    encoded(reinterpret_cast<const char *>(extra_data.data()),
                                static_cast<qsizetype>(extra_data.size()));
    QJsonParseError     parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(encoded, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        return QString("anomaly");
    return normalizedLabelClassGroup(document.object().value(QString("group")).toString());
}

bool selectionIncludesImage(const ModelDatasetSelection &selection, const SourceImage &image,
                            const QList<SourceLabel> &labels)
{
    if (selection.dataset_ids.find(image.dataset_id) != selection.dataset_ids.cend())
        return true;

    const qint64 image_class_id = imageLabelClassIdFromExtraData(image.extra_data);
    if (selection.containsLabelClass(image.dataset_id, image_class_id))
        return true;

    return std::any_of(labels.cbegin(), labels.cend(), [&selection, &image](const SourceLabel &label)
                       { return selection.containsLabelClass(image.dataset_id, label.class_id); });
}

bool selectedLabel(const ModelDatasetSelection &selection, const SourceImage &image, const SourceLabel &label)
{
    return selection.dataset_ids.find(image.dataset_id) != selection.dataset_ids.cend()
        || selection.containsLabelClass(image.dataset_id, label.class_id);
}

/**
 * @brief 从项目数据库加载测试图像与真值。
 * @param file_list_path 测试任务文件列表路径。
 * @param project_database_path 项目数据库路径。
 * @param task_database_path 测试任务数据库路径（数据集/类别选择）。
 * @param method 评估方法。
 * @param images 输出图像记录。
 * @param cancel_token 协作取消令牌。
 * @param err_msg 错误信息输出。
 * @param missing_database_images 输出：不在项目数据库中的图像数。
 * @param ignored_selection_images 输出：不满足数据集/类别选择的图像数。
 * @return 加载成功返回 true。
 */
bool loadImages(const QString &file_list_path, const QString &project_database_path, const QString &task_database_path,
                const evaluation::Method method, QMap<qint64, EvaluationImageData> &images,
                const std::shared_ptr<std::atomic_bool> &cancel_token, QString *err_msg,
                int *missing_database_images = nullptr, int *ignored_selection_images = nullptr)
{
    images.clear();
    if (missing_database_images != nullptr)
        *missing_database_images = 0;
    if (ignored_selection_images != nullptr)
        *ignored_selection_images = 0;
    if (isCancelled(cancel_token))
    {
        if (err_msg)
            *err_msg = QString("评估已取消");
        return false;
    }

    QList<QPair<qint64, QString>> rows;
    if (!readEvaluationImageList(file_list_path, rows, cancel_token, err_msg))
        return false;

    if (task_database_path.trimmed().isEmpty() || !QFileInfo(task_database_path).isFile())
    {
        if (err_msg)
            *err_msg = QString("测试任务数据库不存在: %1").arg(task_database_path);
        return false;
    }
    database::ModelTaskDataBase             task_database(task_database_path);
    QList<database::DatasetSelectionRecord> selection_records;
    if (!task_database.readDatasets(selection_records, err_msg))
        return false;
    const ModelDatasetSelection selection = modelDatasetSelectionsFromDatabase(selection_records).test;
    if (selection.isEmpty())
    {
        if (err_msg)
            *err_msg = QString("测试任务没有保存测试数据集或类别选择");
        return false;
    }

    if (project_database_path.trimmed().isEmpty() || !QFileInfo(project_database_path).isFile())
    {
        if (err_msg)
            *err_msg = QString("项目数据库不存在: %1").arg(project_database_path);
        return false;
    }

    database::ProjectDataBase         project_database(project_database_path);
    QString                           database_error;
    std::vector<int64_t>              image_dataset_ids;
    std::vector<int64_t>              image_ids;
    std::vector<QString>              image_paths;
    std::vector<std::vector<uint8_t>> image_extra_data;
    if (!project_database.getAllImages(image_dataset_ids, image_ids, image_paths, image_extra_data, database_error))
    {
        if (err_msg)
            *err_msg = QString("读取项目图像失败: %1").arg(database_error);
        return false;
    }
    if (image_dataset_ids.size() != image_ids.size() || image_ids.size() != image_paths.size()
        || image_ids.size() != image_extra_data.size())
    {
        if (err_msg)
            *err_msg = QString("项目图像数据数量不一致");
        return false;
    }

    std::vector<int64_t>              label_ids;
    std::vector<int64_t>              label_image_ids;
    std::vector<int64_t>              label_class_ids;
    std::vector<int64_t>              label_types;
    std::vector<std::vector<uint8_t>> label_data;
    if (!project_database.getAllLabels(label_ids, label_image_ids, label_class_ids, label_types, label_data,
                                       database_error))
    {
        if (err_msg)
            *err_msg = QString("读取项目标注失败: %1").arg(database_error);
        return false;
    }
    if (label_ids.size() != label_image_ids.size() || label_ids.size() != label_class_ids.size()
        || label_ids.size() != label_types.size() || label_ids.size() != label_data.size())
    {
        if (err_msg)
            *err_msg = QString("项目标注数据数量不一致");
        return false;
    }

    std::vector<int64_t>              class_ids;
    std::vector<QString>              class_names;
    std::vector<QString>              class_colors;
    std::vector<QString>              class_shortcuts;
    std::vector<int64_t>              class_ordinals;
    std::vector<std::vector<uint8_t>> class_extra_data;
    if (!project_database.getAllLabelClasses(class_ids, class_names, class_colors, class_shortcuts, class_ordinals,
                                             class_extra_data, database_error))
    {
        if (err_msg)
            *err_msg = QString("读取项目标签类别失败: %1").arg(database_error);
        return false;
    }
    if (class_ids.size() != class_names.size() || class_ids.size() != class_extra_data.size())
    {
        if (err_msg)
            *err_msg = QString("项目标签类别数据数量不一致");
        return false;
    }

    QMap<qint64, SourceImage> source_images;
    for (size_t index = 0; index < image_ids.size(); ++index)
    {
        if (image_ids[index] < 0 || source_images.contains(image_ids[index]))
            continue;
        source_images.insert(image_ids[index], SourceImage{image_ids[index], image_dataset_ids[index],
                                                           image_paths[index], image_extra_data[index]});
    }

    QMap<qint64, QList<SourceLabel>> labels_by_image;
    for (size_t index = 0; index < label_ids.size(); ++index)
    {
        if (label_ids[index] < 0 || label_image_ids[index] < 0 || label_class_ids[index] < 0)
            continue;
        labels_by_image[label_image_ids[index]].push_back(
            SourceLabel{label_ids[index], label_image_ids[index], label_class_ids[index], label_data[index]});
    }

    QMap<qint64, SourceClass> classes;
    for (size_t index = 0; index < class_ids.size(); ++index)
    {
        classes.insert(class_ids[index],
                       SourceClass{class_names[index], labelClassGroupFromExtraData(class_extra_data[index])});
    }

    const std::unique_ptr<data::LabelDataHelper_t> label_helper = data::createLabelDataHelper(static_cast<int>(method));
    if (label_helper == nullptr)
    {
        if (err_msg)
            *err_msg = QString("无法创建评估标注数据解析器");
        return false;
    }

    for (const auto &[image_id, listed_path] : rows)
    {
        if (isCancelled(cancel_token))
        {
            if (err_msg)
                *err_msg = QString("评估已取消");
            return false;
        }
        const auto source_it = source_images.find(image_id);
        if (source_it == source_images.end())
        {
            if (missing_database_images != nullptr)
                ++(*missing_database_images);
            continue;
        }
        const SourceImage       &source_image  = source_it.value();
        const QList<SourceLabel> source_labels = labels_by_image.value(image_id);
        if (!selectionIncludesImage(selection, source_image, source_labels))
        {
            if (ignored_selection_images != nullptr)
                ++(*ignored_selection_images);
            continue;
        }

        EvaluationImageData image;
        image.id         = source_image.id;
        image.dataset_id = source_image.dataset_id;
        image.path       = source_image.path.trimmed().isEmpty() ? listed_path : source_image.path;
        image.name       = QFileInfo(image.path).fileName();
        data::DatasetIO::getImageDimensions(image.path, image.width, image.height);

        const qint64 image_class_id = imageLabelClassIdFromExtraData(source_image.extra_data);
        const auto   image_class    = classes.find(image_class_id);
        if (evaluation::isAnomaly(method) && image_class != classes.cend()
            && image_class.value().group == QString("anomaly"))
        {
            image.gt.push_back(EvaluationGroundTruthData{-1, 1, QString("Anomaly"), {}, {}});
        }
        else if (method == evaluation::Method::Classification && image_class != classes.cend()
                 && (selection.dataset_ids.find(image.dataset_id) != selection.dataset_ids.cend()
                     || selection.containsLabelClass(image.dataset_id, image_class_id)))
        {
            image.gt.push_back(
                EvaluationGroundTruthData{-1, static_cast<int>(image_class_id), image_class.value().name, {}, {}});
        }

        for (const SourceLabel &source_label : source_labels)
        {
            if (!selectedLabel(selection, source_image, source_label))
                continue;
            const auto class_it = classes.find(source_label.class_id);
            if (class_it == classes.cend())
                continue;

            QVariantMap label_geometry;
            if (!source_label.data.empty())
            {
                try
                {
                    const std::unique_ptr<data::LabelData_t> label = label_helper->createLabelData();
                    if (label == nullptr)
                    {
                        if (err_msg)
                            *err_msg = QString("无法创建图像 %1 的标注数据").arg(source_label.id);
                        return false;
                    }
                    label->fromBlob(source_label.data);
                    label_geometry = label->dataMap();
                }
                catch (const std::exception &exception)
                {
                    if (err_msg)
                        *err_msg = QString("读取标注 %1 失败: %2")
                                       .arg(source_label.id)
                                       .arg(QString::fromUtf8(exception.what()));
                    return false;
                }
            }

            EvaluationGroundTruthData ground_truth;
            ground_truth.label_id   = source_label.id;
            ground_truth.class_id   = evaluation::isAnomaly(method)
                                        ? (class_it.value().group == QString("anomaly") ? 1 : 0)
                                        : static_cast<int>(source_label.class_id);
            ground_truth.class_name = evaluation::isAnomaly(method)
                                        ? (ground_truth.class_id == 1 ? QString("Anomaly") : QString("GOOD"))
                                        : class_it.value().name;
            ground_truth.geometry   = label_geometry;
            ground_truth.bounds     = label_geometry;
            if (!readBox(ground_truth.geometry, ground_truth.box))
                ground_truth.geometry.clear();
            ground_truth.geometry = canonicalGeometry(ground_truth.geometry, ground_truth.box);
            if (ground_truth.box.valid())
                ground_truth.bounds = evaluationBoxMap(ground_truth.box);
            if (!evaluation::isAnomaly(method) || ground_truth.class_id == 1)
                image.gt.push_back(std::move(ground_truth));
        }
        images.insert(image.id, std::move(image));
    }
    if (images.isEmpty())
    {
        if (err_msg)
            *err_msg = QString("测试数据集没有有效图像");
        return false;
    }
    return true;
}

/**
 * @brief 从测试任务数据库与预测目录加载预测结果。
 *
 * 每条预测记录按协议校验 geometry，越界 bbox 裁剪到图像边界，并规范化
 * 几何记录；异常检测方法只读取图像级 image_score。
 * @param task_database_path 测试任务数据库路径。
 * @param prediction_dir 预测输出目录（mask artifact 根目录）。
 * @param images 已加载的图像记录（按 image_id 追加预测）。
 * @param anomaly_method 是否为异常检测方法。
 * @param count 输出预测总数，可为 nullptr。
 * @param cancel_token 协作取消令牌。
 * @param err_msg 错误信息输出。
 * @param ignored_count 输出：不属于当前可用图像的预测数。
 * @return 加载成功返回 true。
 */
bool loadPredictions(const QString &task_database_path, const QString &prediction_dir,
                     QMap<qint64, EvaluationImageData> &images, const bool anomaly_method, int *count,
                     const std::shared_ptr<std::atomic_bool> &cancel_token, QString *err_msg,
                     int *ignored_count = nullptr)
{
    if (count)
        *count = 0;
    if (ignored_count)
        *ignored_count = 0;
    if (isCancelled(cancel_token))
    {
        if (err_msg)
            *err_msg = QString("评估已取消");
        return false;
    }
    if (task_database_path.trimmed().isEmpty() || !QFileInfo(task_database_path).isFile())
        return true;

    database::ModelTaskDataBase database(task_database_path);
    QHash<qint64, QVariant>     records;
    if (!database.readPredictions(records, err_msg))
        return false;

    QSet<QString> prediction_ids;
    int           total = 0;
    const auto    fail  = [err_msg](const QString &message)
    {
        if (err_msg)
            *err_msg = message;
        return false;
    };
    for (auto record_it = records.cbegin(); record_it != records.cend(); ++record_it)
    {
        if (isCancelled(cancel_token))
            return fail(QString("评估已取消"));
        const qint64 image_id = record_it.key();
        if (!images.contains(image_id))
        {
            if (ignored_count)
                ++(*ignored_count);
            continue;
        }

        if (anomaly_method)
        {
            const QVariantMap value       = record_it.value().toMap();
            const QVariant    score_value = value.value(QStringLiteral("image_score"));
            double            score       = 0.0;
            if (!finiteNumber(score_value, &score) || score < 0.0 || score > 1.0)
                return fail(QString("图像 %1 的 image_score 无效").arg(image_id));
            EvaluationPredictionData prediction;
            prediction.prediction_id = QString("image-%1").arg(image_id);
            prediction.image_id      = image_id;
            prediction.class_id      = 1;
            prediction.class_name    = QString("Anomaly");
            prediction.score         = score;
            images[image_id].predictions.push_back(prediction);
            ++total;
            continue;
        }

        const QVariant value = record_it.value();
        QVariantList   prediction_values;
        if (value.metaType().id() == QMetaType::QVariantList)
            prediction_values = value.toList();
        else
        {
            const QVariantMap map = value.toMap();
            if (map.contains(evaluation::fieldName(evaluation::Field::Predictions)))
                prediction_values = map.value(evaluation::fieldName(evaluation::Field::Predictions)).toList();
            else if (map.contains(evaluation::fieldName(evaluation::Field::ClassId))
                     || map.contains(evaluation::fieldName(evaluation::Field::Score)))
                prediction_values.push_back(value);
            else if (!map.isEmpty())
                return fail(QString("图像 %1 的预测记录格式无效").arg(image_id));
        }
        if (prediction_values.size() > static_cast<int>(kMaxEvaluationRecords))
            return fail(QString("图像 %1 的预测数量超过限制").arg(image_id));

        for (int index = 0; index < prediction_values.size(); ++index)
        {
            const QVariantMap value_map = prediction_values.at(index).toMap();
            if (value_map.isEmpty())
                return fail(QString("图像 %1 的预测记录必须是对象").arg(image_id));
            EvaluationPredictionData prediction;
            prediction.prediction_id = mapString(value_map, evaluation::fieldName(evaluation::Field::PredictionId));
            if (prediction.prediction_id.isEmpty())
                prediction.prediction_id = QString("%1-%2").arg(image_id).arg(index + 1);
            prediction.image_id   = image_id;
            prediction.class_id   = mapInt(value_map, evaluation::fieldName(evaluation::Field::ClassId));
            prediction.class_name = mapString(value_map, evaluation::fieldName(evaluation::Field::ClassName));
            if (prediction.class_id < 0)
                return fail(QString("预测 %1 的 class_id 无效").arg(prediction.prediction_id));
            if (!finiteNumber(value_map.value(evaluation::fieldName(evaluation::Field::Score)), &prediction.score)
                || prediction.score < 0.0 || prediction.score > 1.0)
                return fail(QString("预测 %1 的 score 无效").arg(prediction.prediction_id));
            if (prediction_ids.contains(prediction.prediction_id))
                return fail(QString("预测 prediction_id 重复: %1").arg(prediction.prediction_id));
            prediction_ids.insert(prediction.prediction_id);

            prediction.geometry           = value_map.value(evaluation::fieldName(evaluation::Field::Geometry)).toMap();
            const QString x_key           = evaluation::fieldName(evaluation::Field::X);
            const QString y_key           = evaluation::fieldName(evaluation::Field::Y);
            const QString width_key       = evaluation::fieldName(evaluation::Field::Width);
            const QString height_key      = evaluation::fieldName(evaluation::Field::Height);
            const QString short_width_key = QString("w");
            const QString short_height_key  = QString("h");
            const bool    has_direct_x      = value_map.contains(x_key);
            const bool    has_direct_y      = value_map.contains(y_key);
            const bool    has_direct_width  = value_map.contains(width_key) || value_map.contains(short_width_key);
            const bool    has_direct_height = value_map.contains(height_key) || value_map.contains(short_height_key);
            if (prediction.geometry.isEmpty()
                && (has_direct_x || has_direct_y || has_direct_width || has_direct_height))
            {
                if (!has_direct_x || !has_direct_y || !has_direct_width || !has_direct_height)
                    return fail(
                        QString("预测 %1 的 bbox 必须同时包含 x、y、w/width、h/height").arg(prediction.prediction_id));
                prediction.geometry = {
                    {            evaluation::fieldName(evaluation::Field::Type),QStringLiteral("bbox")                                                                                },
                    {          evaluation::fieldName(evaluation::Field::Format),         QStringLiteral("xywh")},
                    {evaluation::fieldName(evaluation::Field::CoordinateSystem), QStringLiteral("image_pixels")},
                    {          evaluation::fieldName(evaluation::Field::Values),
                     QVariantList{
                     value_map.value(x_key), value_map.value(y_key),
                     value_map.contains(width_key) ? value_map.value(width_key) : value_map.value(short_width_key),
                     value_map.contains(height_key) ? value_map.value(height_key)
                     : value_map.value(short_height_key)}                                                      }
                };
            }
            prediction.bounds = prediction.geometry.value(evaluation::fieldName(evaluation::Field::Bounds)).toMap();
            if (!prediction.geometry.isEmpty())
            {
                QString geometry_error;
                if (!validateGeometryProtocol(prediction.geometry, images[image_id].width, images[image_id].height,
                                              prediction_dir, &geometry_error))
                    return fail(geometry_error);
            }
            if (readBox(prediction.geometry, prediction.box))
            {
                const EvaluationImageData &image = images[image_id];
                if (image.width > 0 && image.height > 0)
                {
                    const double right
                        = std::clamp(prediction.box.x + prediction.box.w, 0.0, static_cast<double>(image.width));
                    const double bottom
                        = std::clamp(prediction.box.y + prediction.box.h, 0.0, static_cast<double>(image.height));
                    prediction.box.x = std::clamp(prediction.box.x, 0.0, static_cast<double>(image.width));
                    prediction.box.y = std::clamp(prediction.box.y, 0.0, static_cast<double>(image.height));
                    prediction.box.w = std::max(0.0, right - prediction.box.x);
                    prediction.box.h = std::max(0.0, bottom - prediction.box.y);
                }
                prediction.bounds = evaluationBoxMap(prediction.box);
            }
            prediction.geometry = canonicalGeometry(prediction.geometry, prediction.box);
            images[image_id].predictions.push_back(prediction);
            ++total;
        }
    }
    if (count)
        *count = total;
    return true;
}

} // namespace

EvaluationCapabilities ModelEvaluationService::capabilitiesForMethod(const evaluation::Method method)
{
    EvaluationCapabilities capabilities;
    capabilities.has_instance_metrics = evaluation::hasInstanceMetrics(method);
    capabilities.has_image_metrics    = evaluation::hasImageMetrics(method);
    capabilities.has_confusion_matrix = evaluation::hasConfusionMatrix(method);
    capabilities.has_instance_events  = evaluation::hasInstanceEvents(method);
    if (evaluation::isAnomaly(method))
        capabilities.chart_kinds = {QStringLiteral("line")};
    else if (capabilities.has_instance_metrics)
        capabilities.chart_kinds = {QStringLiteral("bar"), QStringLiteral("line")};
    return capabilities;
}

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
    if (!loadImages(options.dataset_file_list_path, options.project_database_path, options.task_database_path,
                    options.method, images, options.cancel_token, err_msg, &missing_database_images,
                    &ignored_selection_images))
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
    if (!loadPredictions(options.task_database_path, options.prediction_dir, images,
                         evaluation::isAnomaly(options.method), &prediction_count, options.cancel_token, err_msg,
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
            const EvaluationGroundTruthData *anomaly_gt = nullptr;
            for (const EvaluationGroundTruthData &gt : image.gt)
            {
                if (gt.class_id == 1)
                {
                    anomaly_gt = &gt;
                    break;
                }
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
            const bool               ground_truth_anomaly = anomaly_gt != nullptr;
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

            EvaluationGroundTruthData display_gt = anomaly_gt != nullptr ? *anomaly_gt : EvaluationGroundTruthData{};
            display_gt.class_id                  = ground_truth_anomaly ? 1 : 0;
            display_gt.class_name = ground_truth_anomaly ? QStringLiteral("Anomaly") : QStringLiteral("GOOD");
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
