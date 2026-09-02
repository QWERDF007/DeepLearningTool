#include "model/EvaluationDataset.h"

#include "data/DatasetIO.h"
#include "data/LabelData.h"
#include "database/DataBase.h"
#include "database/ModelTaskDataBase.h"
#include "model/EvaluationGeometry.h"
#include "model/ModelDatasetSelection.h"

#include <opencv2/imgcodecs.hpp>

#include <QCache>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>
#include <QMetaType>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <exception>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace dltool::model {

namespace {

/// 图像文件列表的文档大小上限。
constexpr qint64      kMaxEvaluationFileBytes = 256LL * 1024LL * 1024LL;
/// 预测/文件列表的记录数量上限。
constexpr std::size_t kMaxEvaluationRecords = 5'000'000;

/**
 * @brief 解析 CSV 行（支持引号转义）。
 * @param line 输入行。
 * @param valid 输出解析有效性（引号未闭合视为无效），可为 nullptr。
 * @return 字段列表。
 */
QList<QString> parseCsvLine(const QString &line, bool *valid = nullptr)
{
    QList<QString> fields;
    QString        field;
    bool           quoted = false;
    for (int i = 0; i < line.size(); ++i)
    {
        const QChar c = line.at(i);
        if (c == QChar('"'))
        {
            if (quoted && i + 1 < line.size() && line.at(i + 1) == QChar('"'))
            {
                field += QChar('"');
                ++i;
            }
            else
            {
                quoted = !quoted;
            }
        }
        else if (c == QChar(',') && !quoted)
        {
            fields.push_back(field);
            field.clear();
        }
        else
        {
            field += c;
        }
    }
    fields.push_back(field);
    if (valid != nullptr)
        *valid = !quoted;
    return fields;
}

/**
 * @brief 判断协作取消令牌是否已被置位。
 * @param cancel 协作取消令牌，可为空。
 * @return 已置位返回 true。
 */
bool isCancelled(const std::shared_ptr<std::atomic_bool> &cancel)
{
    return cancel != nullptr && cancel->load(std::memory_order_relaxed);
}

bool decodeEvaluationScoreMap(const QString &path, cv::Mat &decoded, QString *err_msg);
bool decodedScoreMapMaximum(const cv::Mat &decoded, double *maximum);

struct EvaluationScoreMaximumRequest
{
    qint64  image_id{-1};
    QString path;
};

struct EvaluationScoreMaximumResult
{
    qint64                                image_id{-1};
    bool                                  has_score{false};
    bool                                  maximum_cache_hit{false};
    double                                maximum{0.0};
    std::shared_ptr<const EvaluationScoreMap> score_map;
    QString                               error;
};

struct EvaluationScoreMaximumCacheEntry
{
    qint64 file_size{0};
    qint64 last_modified_ms{0};
    bool   has_score{false};
    double maximum{0.0};
};

QMutex &evaluationScoreMaximumCacheMutex()
{
    static QMutex mutex;
    return mutex;
}

QCache<QString, EvaluationScoreMaximumCacheEntry> &evaluationScoreMaximumCache()
{
    static QCache<QString, EvaluationScoreMaximumCacheEntry> cache;
    static const bool initialized = []
    {
        cache.setMaxCost(100'000);
        return true;
    }();
    Q_UNUSED(initialized);
    return cache;
}

bool cachedEvaluationScoreMapMaximum(const QString &path, double *maximum, bool *has_score)
{
    if (maximum == nullptr || has_score == nullptr)
        return false;

    const QFileInfo file_info(path);
    if (!file_info.isFile())
        return false;
    const QString cache_key       = file_info.absoluteFilePath();
    const qint64  file_size       = file_info.size();
    const qint64  last_modified_ms = file_info.lastModified().toMSecsSinceEpoch();
    QMutexLocker locker(&evaluationScoreMaximumCacheMutex());
    const EvaluationScoreMaximumCacheEntry *cached = evaluationScoreMaximumCache().object(cache_key);
    if (cached == nullptr || cached->file_size != file_size || cached->last_modified_ms != last_modified_ms)
        return false;
    *maximum  = cached->maximum;
    *has_score = cached->has_score;
    return true;
}

void cacheEvaluationScoreMapMaximum(const QString &path, const bool has_score, const double maximum)
{
    const QFileInfo file_info(path);
    if (!file_info.isFile())
        return;
    const QString cache_key = file_info.absoluteFilePath();
    QMutexLocker  locker(&evaluationScoreMaximumCacheMutex());
    evaluationScoreMaximumCache().insert(
        cache_key, new EvaluationScoreMaximumCacheEntry{
                       file_info.size(), file_info.lastModified().toMSecsSinceEpoch(), has_score, maximum});
}

std::shared_ptr<const EvaluationScoreMap> materializedScoreMap(const cv::Mat &decoded)
{
    auto score_map = std::make_shared<EvaluationScoreMap>();
    score_map->width  = decoded.cols;
    score_map->height = decoded.rows;
    score_map->values.resize(decoded.cols * decoded.rows);
    double maximum     = 0.0;
    bool   has_maximum = false;
    if (decoded.type() == CV_32FC1)
    {
        for (int y = 0; y < decoded.rows; ++y)
        {
            const float *source = decoded.ptr<float>(y);
            double       *target = score_map->values.data() + y * decoded.cols;
            for (int x = 0; x < decoded.cols; ++x)
            {
                const double value = static_cast<double>(source[x]);
                target[x]           = value;
                if (std::isfinite(value) && (!has_maximum || value > maximum))
                {
                    maximum     = value;
                    has_maximum = true;
                }
            }
        }
    }
    else
    {
        for (int y = 0; y < decoded.rows; ++y)
        {
            const double *source = decoded.ptr<double>(y);
            double        *target = score_map->values.data() + y * decoded.cols;
            for (int x = 0; x < decoded.cols; ++x)
            {
                const double value = source[x];
                target[x]           = value;
                if (std::isfinite(value) && (!has_maximum || value > maximum))
                {
                    maximum     = value;
                    has_maximum = true;
                }
            }
        }
    }
    score_map->maximum_score     = maximum;
    score_map->has_maximum_score = has_maximum;
    return score_map;
}

EvaluationScoreMaximumResult readScoreMapForEvaluation(const EvaluationScoreMaximumRequest &request,
                                                        const double retain_threshold)
{
    EvaluationScoreMaximumResult result;
    result.image_id = request.image_id;
    if (cachedEvaluationScoreMapMaximum(request.path, &result.maximum, &result.has_score))
    {
        result.maximum_cache_hit = true;
        if (!result.has_score || !std::isfinite(retain_threshold) || result.maximum < retain_threshold)
            return result;

        cv::Mat decoded;
        if (!decodeEvaluationScoreMap(request.path, decoded, &result.error))
            return result;
        result.score_map = materializedScoreMap(decoded);
        return result;
    }

    cv::Mat decoded;
    if (!decodeEvaluationScoreMap(request.path, decoded, &result.error))
        return result;

    if (!decodedScoreMapMaximum(decoded, &result.maximum))
    {
        cacheEvaluationScoreMapMaximum(request.path, false, 0.0);
        result.error.clear();
        return result;
    }
    result.has_score = true;
    cacheEvaluationScoreMapMaximum(request.path, true, result.maximum);
    if (std::isfinite(retain_threshold) && result.maximum >= retain_threshold)
        result.score_map = materializedScoreMap(decoded);
    return result;
}

std::vector<EvaluationScoreMaximumResult> readScoreMapMaximums(
    const std::vector<EvaluationScoreMaximumRequest> &requests,
    const std::shared_ptr<std::atomic_bool>            &cancel_token, const double retain_threshold)
{
    std::vector<EvaluationScoreMaximumResult> results(requests.size());
    if (requests.empty())
        return results;

    constexpr std::size_t kMaximumReaderThreads = 8;
    const std::size_t worker_count
        = std::min(requests.size(), std::max<std::size_t>(1, std::min<std::size_t>(kMaximumReaderThreads,
                                                                                     std::thread::hardware_concurrency())));
    std::atomic_size_t next_index{0};
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    const auto worker = [&]()
    {
        while (!isCancelled(cancel_token))
        {
            const std::size_t index = next_index.fetch_add(1, std::memory_order_relaxed);
            if (index >= requests.size())
                return;

            results[index] = readScoreMapForEvaluation(requests[index], retain_threshold);
        }
    };
    for (std::size_t index = 0; index < worker_count; ++index)
        workers.emplace_back(worker);
    for (std::thread &thread : workers)
        thread.join();
    return results;
}

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
    if (normalized == QString("good") || normalized == QString("良好") || normalized == QString("正常")
        || normalized == QString("ok"))
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

bool decodeEvaluationScoreMap(const QString &path, cv::Mat &decoded, QString *err_msg)
{
    const auto fail = [err_msg](const QString &message)
    {
        if (err_msg != nullptr)
            *err_msg = message;
        return false;
    };

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(QString("打开异常分数图失败: %1").arg(path));
    const QByteArray bytes = file.readAll();
    if (bytes.isEmpty())
        return fail(QString("异常分数图为空: %1").arg(path));

    cv::Mat encoded(1, bytes.size(), CV_8UC1, const_cast<char *>(bytes.constData()));
    decoded = cv::imdecode(encoded, cv::IMREAD_UNCHANGED);
    if (decoded.empty() || decoded.dims != 2 || decoded.channels() != 1
        || (decoded.type() != CV_32FC1 && decoded.type() != CV_64FC1))
        return fail(QString("异常分数图必须是单通道 CV_32FC1/CV_64FC1 TIFF: %1").arg(path));
    return true;
}

bool decodedScoreMapMaximum(const cv::Mat &decoded, double *maximum)
{
    if (maximum == nullptr)
        return false;
    bool   found = false;
    double value = 0.0;
    if (decoded.type() == CV_32FC1)
    {
        for (int y = 0; y < decoded.rows; ++y)
        {
            const float *row = decoded.ptr<float>(y);
            for (int x = 0; x < decoded.cols; ++x)
            {
                const double score = static_cast<double>(row[x]);
                if (std::isfinite(score) && (!found || score > value))
                {
                    value = score;
                    found = true;
                }
            }
        }
    }
    else
    {
        for (int y = 0; y < decoded.rows; ++y)
        {
            const double *row = decoded.ptr<double>(y);
            for (int x = 0; x < decoded.cols; ++x)
            {
                const double score = row[x];
                if (std::isfinite(score) && (!found || score > value))
                {
                    value = score;
                    found = true;
                }
            }
        }
    }
    if (!found)
        return false;
    *maximum = value;
    return true;
}

} // namespace

bool readEvaluationScoreMap(const QString &path, EvaluationScoreMap &score_map, QString *err_msg)
{
    score_map = {};
    cv::Mat decoded;
    if (!decodeEvaluationScoreMap(path, decoded, err_msg))
        return false;

    const std::shared_ptr<const EvaluationScoreMap> materialized = materializedScoreMap(decoded);
    score_map = *materialized;
    return true;
}

bool readEvaluationScoreMapMaximum(const QString &path, double *maximum, QString *err_msg)
{
    cv::Mat decoded;
    return decodeEvaluationScoreMap(path, decoded, err_msg) && decodedScoreMapMaximum(decoded, maximum);
}

bool evaluationScoreMapMaximum(const EvaluationScoreMap &score_map, double *maximum)
{
    if (maximum == nullptr || !score_map.isValid())
        return false;
    if (score_map.has_maximum_score)
    {
        *maximum = score_map.maximum_score;
        return true;
    }
    bool   found = false;
    double value = 0.0;
    for (const double score : score_map.values)
    {
        if (!std::isfinite(score))
            continue;
        if (!found || score > value)
        {
            value = score;
            found = true;
        }
    }
    if (!found)
        return false;
    *maximum = value;
    return true;
}

bool evaluationAnomalyImageScore(const EvaluationImageData &image, double *score)
{
    if (score == nullptr)
        return false;
    if (image.has_anomaly_image_score && std::isfinite(image.anomaly_image_score))
    {
        *score = image.anomaly_image_score;
        return true;
    }
    if (image.anomaly_score_map != nullptr)
        return evaluationScoreMapMaximum(*image.anomaly_score_map, score);

    bool   found = false;
    double value = 0.0;
    for (const EvaluationPredictionData &prediction : image.predictions)
    {
        if (prediction.class_id != 1 || !std::isfinite(prediction.score))
            continue;
        if (!found || prediction.score > value)
        {
            value = prediction.score;
            found = true;
        }
    }
    if (!found)
        return false;
    *score = value;
    return true;
}

bool readEvaluationImageList(const QString &path, QList<QPair<qint64, QString>> &rows,
                             const std::shared_ptr<std::atomic_bool> &cancel_token, QString *err_msg)
{
    rows.clear();
    const QFileInfo file_info(path);
    if (!file_info.exists() || !file_info.isFile())
    {
        if (err_msg)
            *err_msg = QString("图像文件列表不存在: %1").arg(path);
        return false;
    }
    if (file_info.size() > kMaxEvaluationFileBytes)
    {
        if (err_msg)
            *err_msg = QString("图像文件列表超过大小限制: %1").arg(path);
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (err_msg)
            *err_msg = QString("打开图像文件列表失败: %1").arg(file.errorString());
        return false;
    }
    QTextStream  stream(&file);
    QSet<qint64> ids;
    bool         first = true;
    while (!stream.atEnd())
    {
        if (isCancelled(cancel_token))
        {
            if (err_msg)
                *err_msg = QString("评估已取消");
            return false;
        }
        QString line = stream.readLine();
        if (line.trimmed().isEmpty())
            continue;
        if (first && !line.isEmpty() && line.at(0) == QChar(0xfeff))
            line.remove(0, 1);
        bool                 csv_valid = false;
        const QList<QString> fields    = parseCsvLine(line, &csv_valid);
        if (first && csv_valid && fields.size() == 2
            && fields.at(0).trimmed().compare(QString("image_id"), Qt::CaseInsensitive) == 0)
        {
            first = false;
            continue;
        }
        first = false;
        if (!csv_valid || fields.size() != 2)
        {
            if (err_msg)
                *err_msg = QString("图像文件列表行格式无效: %1").arg(line);
            return false;
        }
        bool          ok         = false;
        const qint64  image_id   = fields.at(0).trimmed().toLongLong(&ok);
        const QString image_path = fields.at(1).trimmed();
        if (!ok || image_id < 0 || image_path.isEmpty())
            continue;
        if (ids.contains(image_id))
            continue;
        ids.insert(image_id);
        rows.push_back({image_id, image_path});
        if (rows.size() > static_cast<int>(kMaxEvaluationRecords))
        {
            if (err_msg)
                *err_msg = QString("图像文件列表记录数量超过限制");
            return false;
        }
    }
    if (rows.isEmpty())
    {
        if (err_msg)
            *err_msg = QString("图像文件列表没有有效图像: %1").arg(path);
        return false;
    }
    return true;
}

bool loadEvaluationImages(const QString &file_list_path, const QString &project_database_path,
                          const QString &task_database_path, const evaluation::Method method,
                          QMap<qint64, EvaluationImageData>       &images,
                          const std::shared_ptr<std::atomic_bool> &cancel_token, QString *err_msg,
                          int *missing_database_images, int *ignored_selection_images,
                          const std::function<bool(qint64 image_id, int *width, int *height)> &dimensions_provider,
                          QMap<int, QString> *class_catalog, QMap<int, QString> *class_colors_out)
{
    images.clear();
    if (class_catalog != nullptr)
        class_catalog->clear();
    if (class_colors_out != nullptr)
        class_colors_out->clear();
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
        if (class_catalog != nullptr)
        {
            const int     class_id = static_cast<int>(class_ids[index]);
            const QString name
                = class_names[index].trimmed().isEmpty() ? QString::number(class_id) : class_names[index];
            class_catalog->insert(class_id, name);
        }
        if (class_colors_out != nullptr)
        {
            const int     class_id = static_cast<int>(class_ids[index]);
            const QString color    = index < class_colors.size() ? class_colors[index] : QString();
            if (!color.isEmpty())
                class_colors_out->insert(class_id, color);
        }
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
        // 异常检测只使用图像级真值和预测 TIFF 的分数。原图尺寸仅在
        // 生成异常区域时按需读取，避免打开评估时为全部图像读取文件头。
        if (!evaluation::isAnomaly(method)
            && (!dimensions_provider || !dimensions_provider(image.id, &image.width, &image.height)))
        {
            data::DatasetIO::getImageDimensions(image.path, image.width, image.height);
        }

        const qint64 image_class_id = imageLabelClassIdFromExtraData(source_image.extra_data);
        const auto   image_class    = classes.find(image_class_id);
        if (evaluation::isAnomaly(method) && image_class != classes.cend())
        {
            image.gt.push_back(EvaluationGroundTruthData{-1,
                                                         static_cast<int>(image_class_id),
                                                         image_class.value().name,
                                                         {},
                                                         {},
                                                         {},
                                                         image_class.value().group == QString("anomaly")});
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
            ground_truth.class_id   = static_cast<int>(source_label.class_id);
            ground_truth.class_name = class_it.value().name;
            ground_truth.anomaly    = class_it.value().group == QString("anomaly");
            ground_truth.geometry   = label_geometry;
            ground_truth.bounds     = label_geometry;
            if (!readBox(ground_truth.geometry, ground_truth.box))
                ground_truth.geometry.clear();
            ground_truth.geometry = canonicalGeometry(ground_truth.geometry, ground_truth.box);
            if (ground_truth.box.valid())
                ground_truth.bounds = evaluationBoxMap(ground_truth.box);
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

bool loadEvaluationPredictions(const QString &task_database_path, const QString &prediction_dir,
                               QMap<qint64, EvaluationImageData> &images, const bool anomaly_method, int *count,
                               const std::shared_ptr<std::atomic_bool> &cancel_token, QString *err_msg,
                               int *ignored_count, const bool load_anomaly_score_maps,
                               const double retain_anomaly_score_map_threshold)
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

    // 异常检测的唯一评估分数来源是预测目录中的原始像素分数图。
    // task.db 中的 image_score 只属于 Python 任务记录，不能替代 TIFF，
    // 否则图像级分类、区域分割和热力图会落在不同的分数域。
    if (anomaly_method)
    {
        const auto fail = [err_msg](const QString &message)
        {
            if (err_msg != nullptr)
                *err_msg = message;
            return false;
        };
        int total = 0;
        std::vector<EvaluationScoreMaximumRequest> requests;
        requests.reserve(static_cast<std::size_t>(images.size()));
        for (auto image_it = images.begin(); image_it != images.end(); ++image_it)
        {
            if (isCancelled(cancel_token))
                return fail(QStringLiteral("评估已取消"));

            const QString score_path = QDir(prediction_dir).filePath(QStringLiteral("%1.tiff").arg(image_it.key()));
            if (!QFileInfo(score_path).isFile())
                continue;

            if (!load_anomaly_score_maps)
            {
                requests.push_back({image_it.key(), score_path});
                continue;
            }

            double maximum = 0.0;
            QString score_error;
            EvaluationScoreMap score_map;
            if (!readEvaluationScoreMap(score_path, score_map, &score_error))
            {
                return fail(score_error.isEmpty() ? QString("读取异常分数图失败: %1").arg(score_path) : score_error);
            }
            maximum = score_map.maximum_score;
            if (!score_map.has_maximum_score)
                continue;
            image_it->anomaly_image_score     = maximum;
            image_it->has_anomaly_image_score = true;
            image_it->anomaly_score_map = std::make_shared<const EvaluationScoreMap>(std::move(score_map));

            EvaluationPredictionData prediction;
            prediction.prediction_id = QString("image-%1").arg(image_it.key());
            prediction.image_id      = image_it.key();
            prediction.class_id      = 1;
            prediction.class_name    = evaluation::displayText(evaluation::DisplayText::Anomaly);
            prediction.score         = maximum;
            image_it->predictions.push_back(std::move(prediction));
            ++total;
        }

        if (!load_anomaly_score_maps)
        {
            const std::vector<EvaluationScoreMaximumResult> results
                = readScoreMapMaximums(requests, cancel_token, retain_anomaly_score_map_threshold);
            if (isCancelled(cancel_token))
                return fail(QStringLiteral("评估已取消"));
            for (std::size_t index = 0; index < requests.size(); ++index)
            {
                const EvaluationScoreMaximumResult &result = results[index];
                if (!result.has_score)
                {
                    if (result.error.isEmpty())
                        continue;
                    return fail(result.error);
                }

                const qint64 image_id = result.image_id;
                auto          image_it = images.find(image_id);
                if (image_it == images.end())
                    continue;
                EvaluationPredictionData prediction;
                prediction.prediction_id = QString("image-%1").arg(image_id);
                prediction.image_id      = image_id;
                prediction.class_id      = 1;
                prediction.class_name    = evaluation::displayText(evaluation::DisplayText::Anomaly);
                prediction.score         = result.maximum;
                image_it->anomaly_image_score     = result.maximum;
                image_it->has_anomaly_image_score = true;
                if (result.score_map != nullptr)
                    image_it->anomaly_score_map = result.score_map;
                image_it->predictions.push_back(std::move(prediction));
                ++total;
            }
        }
        if (count != nullptr)
            *count = total;
        return true;
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
            if (!finiteNumber(value_map.value(evaluation::fieldName(evaluation::Field::Score)), &prediction.score))
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

} // namespace dltool::model
