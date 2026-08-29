#include "model/AnomalyEvaluationEngine.h"

#include "model/AnomalyPreprocessingTransform.h"
#include "model/EvaluationAnomalyConfusion.h"
#include "model/EvaluationCharts.h"
#include "model/EvaluationCommon.h"
#include "model/EvaluationDataset.h"

#include <opencv2/imgproc.hpp>

#include <QDir>
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
    double        max_score{0.0};
    bool          has_score{false};
};

AnomalyRegionSnapshot anomalyRegions(const EvaluationImageData &image, const double threshold,
                                     const QVariantMap &preprocessing)
{
    AnomalyRegionSnapshot snapshot;
    if (image.anomaly_score_map == nullptr || !image.anomaly_score_map->isValid())
        return snapshot;
    const EvaluationScoreMap &score_map = *image.anomaly_score_map;
    if (!evaluationScoreMapMaximum(score_map, &snapshot.max_score))
        return snapshot;
    snapshot.has_score = true;

    cv::Mat binary(score_map.height, score_map.width, CV_8UC1, cv::Scalar(0));
    for (int y = 0; y < score_map.height; ++y)
    {
        uchar *target = binary.ptr<uchar>(y);
        for (int x = 0; x < score_map.width; ++x)
        {
            const double value = score_map.values.at(y * score_map.width + x);
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

bool AnomalyEvaluationEngine::collectThresholdSearchData(const QMap<qint64, EvaluationImageData> &images,
                                                         QVector<double> &scores,
                                                         qint64 &positive_ground_truth_count, QString *err_msg)
{
    scores.clear();
    positive_ground_truth_count = 0;
    for (const EvaluationImageData &image : images)
    {
        if (cancelled(scratch_.cancel_token))
        {
            if (err_msg != nullptr)
                *err_msg = QStringLiteral("评估已取消");
            return false;
        }

        bool ground_truth_anomaly = false;
        for (const EvaluationGroundTruthData &ground_truth : image.gt)
            ground_truth_anomaly = ground_truth_anomaly || ground_truth.anomaly;
        if (ground_truth_anomaly)
            ++positive_ground_truth_count;

        double maximum = 0.0;
        if (image.anomaly_score_map != nullptr
            && evaluationScoreMapMaximum(*image.anomaly_score_map, &maximum))
        {
            scratch_.anomaly_image_scores.insert(image.id, maximum);
            scores.push_back(maximum);
        }
    }
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
        AnomalyRegionSnapshot regions;
        if (scratch_.collect_events)
            regions = anomalyRegions(image, scratch_.confidence, scratch_.preprocessing_config);
        const auto  cached_score = scratch_.anomaly_image_scores.constFind(image.id);
        const double image_score = cached_score != scratch_.anomaly_image_scores.cend()
                                      ? cached_score.value()
                                      : (regions.has_score ? regions.max_score : 0.0);
        if (regions.has_score)
            scratch_.anomaly_image_scores.insert(image.id, regions.max_score);
        const bool               predicted_anomaly = image_score >= scratch_.confidence;
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
            const QVariantMap event_map
                = buildInstanceEvent(image, status, &display_gt, &display_prediction, 0.0, scratch_.dataset_root,
                                     scratch_.prediction_root, static_cast<qint64>(scratch_.events.size() + 1));
            EvaluationInstanceRecord event = instanceFromMap(event_map);
            event.anomaly_score_map_path
                = QDir(scratch_.prediction_root).filePath(QStringLiteral("%1.tiff").arg(image.id));
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
    // Keep the anomaly-score chart in the same score domain as classification
    // and polygon extraction when a score TIFF is available.
    QMap<qint64, EvaluationImageData> chart_images = images;
    for (auto image_it = chart_images.begin(); image_it != chart_images.end(); ++image_it)
    {
        const auto score_it = scratch_.anomaly_image_scores.constFind(image_it.key());
        if (score_it == scratch_.anomaly_image_scores.cend())
            continue;
        for (EvaluationPredictionData &prediction : image_it->predictions)
            prediction.score = score_it.value();
        rebuildImageDerivedValues(image_it.value());
    }
    const EvaluationChartOutput official
        = buildAnomalyEvaluationCharts(chart_images, diagnostic, scratch_.confidence, &scratch_.threshold_search);
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
