#include "model/AnomalyEvaluationEngine.h"

#include "model/AnomalyPreprocessingTransform.h"
#include "model/EvaluationAnomalyConfusion.h"
#include "model/EvaluationCharts.h"
#include "model/EvaluationCommon.h"
#include "model/EvaluationDataset.h"

#include "data/DatasetIO.h"

#include <opencv2/imgproc.hpp>

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QCache>
#include <QMutex>
#include <QMutexLocker>
#include <QVariantList>

#include <cmath>
#include <limits>

namespace dltool::model {

namespace {

QVariantMap polygonPoint(const double x, const double y)
{
    return {{QStringLiteral("x"), x}, {QStringLiteral("y"), y}};
}

QVariantList polygonPoints(const std::vector<cv::Point> &contour)
{
    QVariantList points;
    points.reserve(static_cast<qsizetype>(contour.size()));
    for (const cv::Point &point : contour)
        points.push_back(polygonPoint(point.x, point.y));
    return points;
}

struct AnomalyRegionSnapshot
{
    QVariantList model_polygons;
    QVariantList image_polygons;
};

QMutex &anomalyRegionCacheMutex()
{
    static QMutex mutex;
    return mutex;
}

QCache<QString, AnomalyRegionSnapshot> &anomalyRegionCache()
{
    static QCache<QString, AnomalyRegionSnapshot> cache;
    static const bool initialized = []
    {
        cache.setMaxCost(64 * 1024);
        return true;
    }();
    Q_UNUSED(initialized);
    return cache;
}

QString anomalyRegionCacheKey(const QString &score_path, const EvaluationImageData &image,
                              const EvaluationScoreMap &score_map, const double threshold,
                              const QVariantMap &preprocessing)
{
    const QFileInfo file_info(score_path);
    if (!file_info.isFile())
        return {};
    const QByteArray preprocessing_bytes = QJsonDocument::fromVariant(preprocessing).toJson(QJsonDocument::Compact);
    return QStringLiteral("%1|size=%2|mtime=%3|image=%4x%5|map=%6x%7|threshold=%8|preprocessing=%9")
        .arg(file_info.absoluteFilePath())
        .arg(file_info.size())
        .arg(file_info.lastModified().toMSecsSinceEpoch())
        .arg(image.width)
        .arg(image.height)
        .arg(score_map.width)
        .arg(score_map.height)
        .arg(QString::number(threshold, 'g', 17))
        .arg(QString::fromUtf8(preprocessing_bytes));
}

bool cachedAnomalyRegions(const QString &key, AnomalyRegionSnapshot *snapshot)
{
    if (key.isEmpty() || snapshot == nullptr)
        return false;
    QMutexLocker locker(&anomalyRegionCacheMutex());
    const AnomalyRegionSnapshot *cached = anomalyRegionCache().object(key);
    if (cached == nullptr)
        return false;
    *snapshot = *cached;
    return true;
}

void cacheAnomalyRegions(const QString &key, const AnomalyRegionSnapshot &snapshot)
{
    if (key.isEmpty())
        return;
    qsizetype point_count = 0;
    for (const QVariant &polygon : snapshot.model_polygons) point_count += polygon.toList().size();
    for (const QVariant &polygon : snapshot.image_polygons) point_count += polygon.toList().size();
    const int cost_kib = std::max(1, static_cast<int>((point_count * 64 + 1023) / 1024));
    QMutexLocker locker(&anomalyRegionCacheMutex());
    anomalyRegionCache().insert(key, new AnomalyRegionSnapshot(snapshot), cost_kib);
}

AnomalyRegionSnapshot anomalyRegions(const EvaluationImageData &image, const EvaluationScoreMap &score_map,
                                     const QString &score_path, const double threshold,
                                     const QVariantMap &preprocessing, bool *cache_hit)
{
    AnomalyRegionSnapshot snapshot;
    if (cache_hit != nullptr)
        *cache_hit = false;
    if (!score_map.isValid())
        return snapshot;

    const QString cache_key = anomalyRegionCacheKey(score_path, image, score_map, threshold, preprocessing);
    if (cachedAnomalyRegions(cache_key, &snapshot))
    {
        if (cache_hit != nullptr)
            *cache_hit = true;
        return snapshot;
    }

    cv::Mat binary(score_map.height, score_map.width, CV_8UC1, cv::Scalar(0));
    for (int y = 0; y < score_map.height; ++y)
    {
        uchar *target = binary.ptr<uchar>(y);
        for (int x = 0; x < score_map.width; ++x)
        {
            const double value = score_map.values.constData()[y * score_map.width + x];
            target[x]          = std::isfinite(value) && value >= threshold ? 255 : 0;
        }
    }

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    const QSize image_size(image.width, image.height);
    const AnomalyPreprocessingTransform transform = AnomalyPreprocessingTransform::fromConfig(
        image_size, QSize(score_map.width, score_map.height), preprocessing);
    for (const std::vector<cv::Point> &contour : contours)
    {
        if (contour.size() < 3)
            continue;
        const QVariantList model_points = polygonPoints(contour);
        snapshot.model_polygons.push_back(model_points);
        if (transform.isValid())
        {
            QVariantList image_points;
            image_points.reserve(model_points.size());
            for (const cv::Point &point : contour)
            {
                const QPointF mapped = transform.modelToImage(QPointF(point.x, point.y));
                image_points.push_back(polygonPoint(mapped.x(), mapped.y()));
            }
            snapshot.image_polygons.push_back(image_points);
        }
    }

    cacheAnomalyRegions(cache_key, snapshot);
    return snapshot;
}

} // namespace

AnomalyEvaluationEngine::AnomalyEvaluationEngine()
    : IEvaluationEngine()
{
}

evaluation::Method AnomalyEvaluationEngine::method() const
{
    return evaluation::Method::AnomalyDetection;
}

void AnomalyEvaluationEngine::buildClasses(const QMap<qint64, EvaluationImageData> &, QMap<int, QString> &) {}

bool AnomalyEvaluationEngine::supportsThresholdSearch() const
{
    return true;
}

bool AnomalyEvaluationEngine::computeInstanceCounts(const QMap<qint64, EvaluationImageData> &,
                                                    const QMap<int, QString> &, QMap<int, EvaluationCounts> &,
                                                    EvaluationCounts &, QString *)
{
    // 异常检测无实例级指标，保持 per_class/overall 为空。
    return true;
}

bool AnomalyEvaluationEngine::runAnomalyLoop(const QMap<qint64, EvaluationImageData> &images, QString *err_msg)
{
    const auto fail = [err_msg](const QString &message)
    {
        if (err_msg)
            *err_msg = message;
        return false;
    };

    for (auto image_it = images.begin(); image_it != images.end(); ++image_it)
    {
        if (cancelled(scratch_.cancel_token))
            return fail(QString("评估已取消"));
        const EvaluationImageData &image = image_it.value();

        // 异常检测按图像生成事件供 UI 统一消费。没有原始实例事件的真负样本也会生成一条记录。
        const EvaluationGroundTruthData *category_gt          = nullptr;
        bool                             ground_truth_anomaly = false;
        for (const EvaluationGroundTruthData &gt : image.gt)
        {
            ground_truth_anomaly = ground_truth_anomaly || gt.anomaly;
            if (category_gt == nullptr || gt.label_id < 0)
                category_gt = &gt;
        }
        const EvaluationPredictionData *anomaly_prediction = nullptr;
        for (const EvaluationPredictionData &prediction : image.predictions)
        {
            if (prediction.class_id == 1
                && (anomaly_prediction == nullptr || prediction.score > anomaly_prediction->score))
                anomaly_prediction = &prediction;
        }
        const auto  cached_score = scratch_.anomaly_image_scores.constFind(image.id);
        double      image_score = cached_score != scratch_.anomaly_image_scores.cend() ? cached_score.value() : 0.0;
        if (cached_score == scratch_.anomaly_image_scores.cend())
        {
            double score = 0.0;
            if (evaluationAnomalyImageScore(image, &score))
            {
                image_score = score;
                scratch_.anomaly_image_scores.insert(image.id, score);
            }
        }
        const bool               predicted_anomaly = image_score >= scratch_.confidence;
        AnomalyRegionSnapshot regions;
        const QString score_path
            = QDir(scratch_.prediction_root).filePath(QStringLiteral("%1.tiff").arg(image.id));
        EvaluationImageData region_image = image;
        if (scratch_.collect_events && predicted_anomaly)
        {
            if (region_image.width <= 0 || region_image.height <= 0)
            {
                bool dimensions_loaded = scratch_.image_dimensions_provider
                    && scratch_.image_dimensions_provider(region_image.id, &region_image.width, &region_image.height);
                if (!dimensions_loaded)
                    data::DatasetIO::getImageDimensions(region_image.path, region_image.width, region_image.height);
            }
            EvaluationScoreMap region_score_map;
            const EvaluationScoreMap *score_map = image.anomaly_score_map.get();
            if (score_map == nullptr)
            {
                QString            score_error;
                if (!readEvaluationScoreMap(score_path, region_score_map, &score_error))
                    return fail(score_error.isEmpty() ? QString("读取异常分数图失败: %1").arg(score_path) : score_error);
                score_map = &region_score_map;
            }
            regions = anomalyRegions(region_image, *score_map, score_path, scratch_.confidence, scratch_.preprocessing_config,
                                     nullptr);
        }
        const evaluation::Status status
            = ground_truth_anomaly && predicted_anomaly
                ? evaluation::Status::TruePositive
                : (!ground_truth_anomaly && !predicted_anomaly
                       ? evaluation::Status::TrueNegative
                       : (predicted_anomaly ? evaluation::Status::FalsePositive : evaluation::Status::FalseNegative));

        if (status == evaluation::Status::TruePositive)
            ++scratch_.image_counts.tp;
        else if (status == evaluation::Status::FalsePositive)
            ++scratch_.image_counts.fp;
        else if (status == evaluation::Status::FalseNegative)
            ++scratch_.image_counts.fn;

        EvaluationGroundTruthData display_gt = category_gt != nullptr ? *category_gt : EvaluationGroundTruthData{};
        if (category_gt == nullptr)
        {
            display_gt.class_id   = -1;
            display_gt.class_name = QString{};
            display_gt.anomaly    = false;
        }
        EvaluationPredictionData display_prediction
            = anomaly_prediction != nullptr ? *anomaly_prediction : EvaluationPredictionData{};
        display_prediction.class_id   = predicted_anomaly ? 1 : 0;
        display_prediction.class_name = evaluation::displayText(predicted_anomaly ? evaluation::DisplayText::Anomaly
                                                                                  : evaluation::DisplayText::Good);
        display_prediction.score      = image_score;

        if (scratch_.collect_events)
        {
            const EvaluationImageData &display_image
                = predicted_anomaly && (image.width <= 0 || image.height <= 0) ? region_image : image;
            EvaluationInstanceRecord event
                = buildInstanceRecord(display_image, status, &display_gt, &display_prediction, 0.0,
                                       scratch_.dataset_root,
                                       scratch_.prediction_root, static_cast<qint64>(scratch_.events.size() + 1));
            event.anomaly_score_map_path
                = score_path;
            event.anomaly_model_polygons = regions.model_polygons;
            event.anomaly_image_polygons = regions.image_polygons;
            scratch_.events.push_back(std::move(event));
        }
    }
    return true;
}

bool AnomalyEvaluationEngine::computeImageCounts(const QMap<qint64, EvaluationImageData> &images,
                                                 EvaluationCounts &image_counts, QString *err_msg)
{
    // 单次异常循环同时产出图像级计数与事件，后续 buildEvents 从共享暂存区读取。
    if (!runAnomalyLoop(images, err_msg))
        return false;
    image_counts = scratch_.image_counts;
    return true;
}

bool AnomalyEvaluationEngine::buildEvents(const QMap<qint64, EvaluationImageData> &,
                                          QList<EvaluationInstanceRecord> &events, QString *)
{
    // 每图像一条事件（含真负样本）已在 runAnomalyLoop 中生成。
    events = scratch_.events;
    return true;
}

QList<QVariantMap> AnomalyEvaluationEngine::buildCharts(const QMap<qint64, EvaluationImageData> &images,
                                                        const QMap<int, QString> &, const EvaluationCounts &,
                                                        const EvaluationCounts &image_counts,
                                                        const QMap<int, EvaluationCounts> &,
                                                        const QMap<QString, qint64> &,
                                                        const QList<EvaluationInstanceRecord> &, QString *)
{
    // 构造统一诊断结构；异常分支只消费 Image 指标。
    const QVariantMap diagnostic = {
        {evaluation::fieldName(evaluation::Field::Instance),
         QVariantMap{{evaluation::fieldName(evaluation::Field::Overall), evaluationMetricMap(0, 0, 0)},
                     {evaluation::fieldName(evaluation::Field::PerClass), QVariantList{}}}},
        {evaluation::fieldName(evaluation::Field::Image),
         evaluationMetricMap(image_counts.tp, image_counts.fp, image_counts.fn)}
    };
    const EvaluationChartOutput official
        = buildAnomalyEvaluationCharts(images, diagnostic, scratch_.confidence, &scratch_.threshold_search);
    scratch_.official_metrics        = official.metrics;
    scratch_.image_metric_definition = official.image_definition;
    QList<QVariantMap>          charts;
    charts.reserve(official.charts.size());
    for (const QVariant &value : official.charts) charts.push_back(value.toMap());
    return charts;
}

QVector<EvaluationConfusionCell> AnomalyEvaluationEngine::buildConfusionMatrix(const QMap<int, QString> &classes,
                                                                               const QMap<QString, qint64> &)
{
    QList<AnomalyConfusionSample> samples;
    samples.reserve(scratch_.events.size());
    for (const EvaluationInstanceRecord &event : scratch_.events)
    {
        const bool predicted_anomaly
            = event.status == evaluation::Status::TruePositive || event.status == evaluation::Status::FalsePositive;
        const bool category_anomaly
            = event.status == evaluation::Status::TruePositive || event.status == evaluation::Status::FalseNegative;
        samples.push_back({event.gt_class_id, event.gt_class, category_anomaly, predicted_anomaly});
    }

    const std::vector<EvaluationConfusionCell> cells = buildAnomalyConfusionCells(samples, classes);
    return QVector<EvaluationConfusionCell>(cells.cbegin(), cells.cend());
}

bool AnomalyEvaluationEngine::hasConfusionMatrix() const
{
    return evaluation::hasConfusionMatrix(evaluation::Method::AnomalyDetection);
}

QStringList AnomalyEvaluationEngine::chartKinds() const
{
    return {evaluation::chartKindKey(evaluation::ChartKind::Line),
            evaluation::chartKindKey(evaluation::ChartKind::Line)};
}

} // namespace dltool::model
