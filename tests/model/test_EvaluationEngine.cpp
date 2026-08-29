#include "../test_runner.h"

#include "TestFixture.h"

#include "model/AnomalyEvaluationEngine.h"
#include "model/AnomalyPreprocessingTransform.h"
#include "model/DetectionEvaluationEngine.h"
#include "model/EvaluationEngineRegistry.h"
#include "model/EvaluationThumbnailImageProvider.h"
#include "model/EvaluationResult.h"
#include "model/ModelEvaluationOptions.h"
#include "model/ModelEvaluationProtocol.h"
#include "model/SegmentationEvaluationEngine.h"

#include <QTest>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/core.hpp>

#include <QDir>
#include <QImage>
#include <QUrlQuery>

#include <atomic>
#include <cmath>
#include <memory>

using namespace dltool::model;
using namespace dltool::model::testsupport;

namespace {

ModelEvaluationOptions optionsFor(const EvaluationFixture &fixture, evaluation::Method method)
{
    ModelEvaluationOptions options;
    options.method                    = method;
    options.project_database_path     = fixture.projectDatabasePath();
    options.dataset_file_list_path    = fixture.fileListPath();
    options.task_database_path        = fixture.taskDatabasePath();
    options.prediction_dir            = fixture.predictionDirectory();
    options.confidence_threshold      = 0.5;
    options.iou_threshold              = 0.5;
    options.matching_strategy         = evaluation::MatchingStrategy::GreedyIoU;
    options.evaluation_config         = evaluation::normalizedEvaluationConfig({});
    return options;
}

const EvaluationConfusionCell *findCell(const EvaluationResult &result, const QString &row,
                                        const QString &column)
{
    for (const EvaluationConfusionCell &cell : result.matrix_cells)
        if (cell.row_key == row && cell.column_key == column)
            return &cell;
    return nullptr;
}

const EvaluationInstanceRecord *findEvent(const EvaluationResult &result, evaluation::Status status)
{
    for (const EvaluationInstanceRecord &event : result.instance_records)
        if (event.status == status)
            return &event;
    return nullptr;
}

class ThresholdCollectionProbe final : public DetectionEvaluationEngine
{
public:
    using InstanceMatchingEvaluationEngine::collectThresholdSearchData;

    void setCancelToken(const std::shared_ptr<std::atomic_bool> &token)
    {
        scratch_.cancel_token = token;
    }
};

class ThresholdCollectionFailureProbe final : public DetectionEvaluationEngine
{
protected:
    bool collectThresholdSearchData(const QMap<qint64, EvaluationImageData> &, QVector<double> &, qint64 &,
                                    QString *err_msg) override
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("阈值数据收集失败");
        return false;
    }
};

} // namespace

class EvaluationEngineTest : public QObject
{
    Q_OBJECT

private slots:
    void detectionEvaluateBuildsTypedResult()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 cat = fixture.addClass(QStringLiteral("Cat"), QStringLiteral("normal"));
        const qint64 image = fixture.addImage(QStringLiteral("cat"));
        QVERIFY(cat >= 0);
        QVERIFY(image >= 0);
        QVERIFY(fixture.addDetectionLabel(image, cat, 0, 0, 10, 10) >= 0);
        QVERIFY(fixture.writeImageList());
        QVERIFY(fixture.setTestSelection({cat}));
        QVERIFY(fixture.writePrediction(image, detectionPrediction(static_cast<int>(cat), QStringLiteral("Cat"),
                                                                    0.9, 0, 0, 10, 10)));

        DetectionEvaluationEngine engine;
        EvaluationResult result;
        QString error;
        QVERIFY2(engine.evaluate(optionsFor(fixture, evaluation::Method::Detection), &result, &error),
                 qPrintable(error));
        QCOMPARE(result.method, evaluation::Method::Detection);
        QCOMPARE(result.overall.tp, qint64(1));
        QCOMPARE(result.overall.fp, qint64(0));
        QCOMPARE(result.overall.fn, qint64(0));
        QCOMPARE(result.prediction_count, 1);
        QVERIFY(result.has_instance_metrics);
        QVERIFY(result.has_image_metrics);
        QVERIFY(result.has_confusion_matrix);
        QCOMPARE(result.instance_records.size(), 1);
        QCOMPARE(result.instance_records.front().status, evaluation::Status::TruePositive);
        const auto *match = findCell(result, QString::number(cat), QString::number(cat));
        QVERIFY(match != nullptr);
        QCOMPARE(match->count, qint64(1));
        QVERIFY(!result.charts.isEmpty());
        QVERIFY(result.official_metrics.value(evaluation::fieldName(evaluation::Field::Available)).toBool());
        QVERIFY(!result.image_metric_definition.isEmpty());
    }

    void detectionThresholdAndEmptyPredictionProduceFalseNegative()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 cat = fixture.addClass(QStringLiteral("Cat"), QStringLiteral("normal"));
        const qint64 image = fixture.addImage(QStringLiteral("cat"));
        QVERIFY(fixture.addDetectionLabel(image, cat, 0, 0, 10, 10) >= 0);
        QVERIFY(fixture.writeImageList());
        QVERIFY(fixture.setTestSelection({cat}));
        QVERIFY(fixture.writePrediction(image, detectionPrediction(static_cast<int>(cat), QStringLiteral("Cat"),
                                                                    0.4, 0, 0, 10, 10)));

        auto options = optionsFor(fixture, evaluation::Method::Detection);
        options.confidence_threshold = 0.5;
        DetectionEvaluationEngine engine;
        EvaluationResult result;
        QString error;
        QVERIFY2(engine.evaluate(options, &result, &error), qPrintable(error));
        QCOMPARE(result.overall.tp, qint64(0));
        QCOMPARE(result.overall.fp, qint64(0));
        QCOMPARE(result.overall.fn, qint64(1));
        const auto *event = findEvent(result, evaluation::Status::FalseNegative);
        QVERIFY(event != nullptr);
        const auto *cell = findCell(result, evaluation::matrixAxisKey(evaluation::MatrixAxisKey::FalseNegative),
                                     QString::number(cat));
        QVERIFY(cell != nullptr);
        QCOMPARE(cell->count, qint64(1));
    }

    void detectionMatchesPredictionsWithinTheirOwnClass()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 cat = fixture.addClass(QStringLiteral("Cat"), QStringLiteral("normal"));
        const qint64 dog = fixture.addClass(QStringLiteral("Dog"), QStringLiteral("normal"));
        const qint64 mismatch = fixture.addImage(QStringLiteral("mismatch"));
        const qint64 false_positive
            = fixture.addImage(QStringLiteral("false-positive"), {{QStringLiteral("image_label_class_id"), cat}});
        const qint64 false_negative = fixture.addImage(QStringLiteral("false-negative"));
        QVERIFY(fixture.addDetectionLabel(mismatch, cat, 0, 0, 10, 10) >= 0);
        QVERIFY(fixture.addDetectionLabel(false_negative, cat, 0, 0, 10, 10) >= 0);
        QVERIFY(fixture.writeImageList());
        QVERIFY(fixture.setTestSelection({cat, dog}));
        QVERIFY(fixture.writePrediction(mismatch,
                                         detectionPrediction(static_cast<int>(dog), QStringLiteral("Dog"), 0.9,
                                                             0, 0, 10, 10)));
        QVERIFY(fixture.writePrediction(false_positive,
                                        detectionPrediction(static_cast<int>(cat), QStringLiteral("Cat"), 0.8,
                                                            0, 0, 10, 10)));

        DetectionEvaluationEngine engine;
        EvaluationResult result;
        QString error;
        QVERIFY2(engine.evaluate(optionsFor(fixture, evaluation::Method::Detection), &result, &error),
                 qPrintable(error));
        QCOMPARE(result.overall.tp, qint64(0));
        QCOMPARE(result.overall.fp, qint64(2));
        QCOMPARE(result.overall.fn, qint64(2));
        QVERIFY(findEvent(result, evaluation::Status::ClassMismatch) == nullptr);
        QVERIFY(findEvent(result, evaluation::Status::FalsePositive) != nullptr);
        QVERIFY(findEvent(result, evaluation::Status::FalseNegative) != nullptr);
        QVERIFY(findCell(result, QStringLiteral("FN"), QString::number(cat)) != nullptr);
        QVERIFY(findCell(result, QString::number(cat), QStringLiteral("FP")) != nullptr);
    }

    void detectionThresholdSearchUsesGlobalMicroF1()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 cat = fixture.addClass(QStringLiteral("Cat"), QStringLiteral("normal"));
        const qint64 dog = fixture.addClass(QStringLiteral("Dog"), QStringLiteral("normal"));
        QVERIFY(cat >= 0);
        QVERIFY(dog >= 0);

        // At 0.9, ten Cat matches are kept: micro counts are TP=10/FN=1.
        for (int index = 0; index < 10; ++index)
        {
            const qint64 image = fixture.addImage(QStringLiteral("cat-%1").arg(index));
            QVERIFY(image >= 0);
            QVERIFY(fixture.addDetectionLabel(image, cat, 0, 0, 10, 10) >= 0);
            QVariantList predictions;
            predictions.push_back(detectionPrediction(static_cast<int>(cat), QStringLiteral("Cat"), 0.9,
                                                      0, 0, 10, 10));
            predictions.push_back(detectionPrediction(static_cast<int>(cat), QStringLiteral("Cat"), 0.8,
                                                      20, 20, 5, 5));
            QVERIFY(fixture.writePrediction(image, predictions));
        }

        // At 0.8, the minority Dog match and ten extra false positives are
        // added. This improves macro-F1 but lowers global micro-F1.
        const qint64 dog_image = fixture.addImage(QStringLiteral("dog"));
        QVERIFY(dog_image >= 0);
        QVERIFY(fixture.addDetectionLabel(dog_image, dog, 0, 0, 10, 10) >= 0);
        QVERIFY(fixture.writePrediction(dog_image,
                                        detectionPrediction(static_cast<int>(dog), QStringLiteral("Dog"), 0.8,
                                                            0, 0, 10, 10)));
        QVERIFY(fixture.writeImageList());
        QVERIFY(fixture.setTestSelection({cat, dog}));

        DetectionEvaluationEngine engine;
        EvaluationResult result;
        QString error;
        QVERIFY2(engine.evaluate(optionsFor(fixture, evaluation::Method::Detection), &result, &error),
                 qPrintable(error));
        QVERIFY(result.threshold_search.available);
        QCOMPARE(result.threshold_search.points.size(), 3);
        QCOMPARE(result.threshold_search.best_point.threshold, 0.9);
        QCOMPARE(result.threshold_search.best_point.counts.tp, qint64(10));
        QCOMPARE(result.threshold_search.best_point.counts.fp, qint64(0));
        QCOMPARE(result.threshold_search.best_point.counts.fn, qint64(1));
        QVERIFY(result.threshold_search.best_point.f1 > 0.95);
    }

    void segmentationUsesConcreteEngineAndPolygonLabels()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Segmentation));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 object = fixture.addClass(QStringLiteral("Object"), QStringLiteral("normal"));
        const qint64 image = fixture.addImage(QStringLiteral("polygon"));
        QVERIFY(fixture.addSegmentationLabel(image, object,
                                              {QPointF(2, 2), QPointF(14, 2), QPointF(14, 14), QPointF(2, 14)})
                >= 0);
        QVERIFY(fixture.writeImageList());
        QVERIFY(fixture.setTestSelection({object}));
        QVERIFY(fixture.writePrediction(image,
                                        detectionPrediction(static_cast<int>(object), QStringLiteral("Object"), 0.95,
                                                            2, 2, 12, 12)));

        SegmentationEvaluationEngine engine;
        EvaluationResult result;
        QString error;
        QVERIFY2(engine.evaluate(optionsFor(fixture, evaluation::Method::Segmentation), &result, &error),
                 qPrintable(error));
        QCOMPARE(result.method, evaluation::Method::Segmentation);
        QCOMPARE(result.overall.tp, qint64(1));
        QCOMPARE(result.overall.fp, qint64(0));
        QCOMPARE(result.overall.fn, qint64(0));
        QVERIFY(result.has_confusion_matrix);
    }

    void segmentationThresholdSearchUsesAllPredictionScores()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Segmentation));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 object = fixture.addClass(QStringLiteral("Object"), QStringLiteral("normal"));
        const qint64 image  = fixture.addImage(QStringLiteral("polygon-threshold"));
        QVERIFY(object >= 0);
        QVERIFY(image >= 0);
        QVERIFY(fixture.addSegmentationLabel(image, object,
                                             {QPointF(2, 2), QPointF(14, 2), QPointF(14, 14), QPointF(2, 14)})
                >= 0);
        QVERIFY(fixture.writeImageList());
        QVERIFY(fixture.setTestSelection({object}));
        QVERIFY(fixture.writePrediction(
            image,
            QVariantList{
                detectionPrediction(static_cast<int>(object), QStringLiteral("Object"), 0.8, 2, 2, 12, 12),
                detectionPrediction(static_cast<int>(object), QStringLiteral("Object"), 0.6, 20, 20, 5, 5)}));

        SegmentationEvaluationEngine engine;
        EvaluationResult             result;
        QString                      error;
        QVERIFY2(engine.evaluate(optionsFor(fixture, evaluation::Method::Segmentation), &result, &error),
                 qPrintable(error));
        QVERIFY(result.threshold_search.available);
        QCOMPARE(result.threshold_search.points.size(), 3);
        QCOMPARE(result.threshold_search.best_point.threshold, 0.8);
        QCOMPARE(result.threshold_search.best_point.counts.tp, qint64(1));
        QCOMPARE(result.threshold_search.best_point.counts.fp, qint64(0));
        QCOMPARE(result.threshold_search.best_point.counts.fn, qint64(0));
    }

    void instanceThresholdCollectionHonorsCancellation()
    {
        EvaluationImageData image;
        image.gt.push_back(EvaluationGroundTruthData{1, 1, QStringLiteral("Object"), {}, {}, {}, false});
        image.predictions.push_back(EvaluationPredictionData{QStringLiteral("prediction"), 1, 1,
                                                              QStringLiteral("Object"), 0.8, {}, {}, {}});

        auto                       cancel = std::make_shared<std::atomic_bool>(true);
        ThresholdCollectionProbe  probe;
        probe.setCancelToken(cancel);
        QVector<double>            scores;
        qint64                      positive_ground_truth_count = 0;
        QString                     error;
        QVERIFY(!probe.collectThresholdSearchData({{1, image}}, scores, positive_ground_truth_count, &error));
        QVERIFY(error.contains(QStringLiteral("取消")));
        QVERIFY(scores.isEmpty());
    }

    void thresholdCollectionFailureStopsEvaluation()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 cat   = fixture.addClass(QStringLiteral("Cat"), QStringLiteral("normal"));
        const qint64 image = fixture.addImage(QStringLiteral("cat"));
        QVERIFY(fixture.addDetectionLabel(image, cat, 0, 0, 10, 10) >= 0);
        QVERIFY(fixture.writeImageList());
        QVERIFY(fixture.setTestSelection({cat}));
        QVERIFY(fixture.writePrediction(
            image, detectionPrediction(static_cast<int>(cat), QStringLiteral("Cat"), 0.9, 0, 0, 10, 10)));

        ThresholdCollectionFailureProbe engine;
        EvaluationResult               result;
        QString                        error;
        QVERIFY(!engine.evaluate(optionsFor(fixture, evaluation::Method::Detection), &result, &error));
        QCOMPARE(error, QStringLiteral("阈值数据收集失败"));
        QVERIFY(result.images.isEmpty());
    }

    void detectionHonorsIoUBoundaryAndHungarianStrategy()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 cat = fixture.addClass(QStringLiteral("Cat"), QStringLiteral("normal"));
        const qint64 image = fixture.addImage(QStringLiteral("boundary"));
        QVERIFY(fixture.addDetectionLabel(image, cat, 0, 0, 10, 10) >= 0);
        QVERIFY(fixture.writeImageList());
        QVERIFY(fixture.setTestSelection({cat}));
        QVERIFY(fixture.writePrediction(image,
                                        detectionPrediction(static_cast<int>(cat), QStringLiteral("Cat"), 0.9,
                                                            0, 0, 5, 10)));

        auto options = optionsFor(fixture, evaluation::Method::Detection);
        options.iou_threshold = 0.5;
        options.matching_strategy = evaluation::MatchingStrategy::HungarianIoU;
        DetectionEvaluationEngine engine;
        EvaluationResult result;
        QString error;
        QVERIFY2(engine.evaluate(options, &result, &error), qPrintable(error));
        QCOMPARE(result.overall.tp, qint64(1));

        QVERIFY(fixture.writePrediction(image,
                                        detectionPrediction(static_cast<int>(cat), QStringLiteral("Cat"), 0.9,
                                                            0, 0, 4, 10)));
        result = {};
        QVERIFY2(engine.evaluate(options, &result, &error), qPrintable(error));
        QCOMPARE(result.overall.tp, qint64(0));
        QCOMPARE(result.overall.fn, qint64(1));
    }

    void anomalyEvaluateBuildsImageMetricsAndBinaryMatrix()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::AnomalyDetection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 good = fixture.addClass(QStringLiteral("Good"), QStringLiteral("good"));
        const qint64 anomaly = fixture.addClass(QStringLiteral("Scratch"), QStringLiteral("anomaly"));
        const qint64 normal_image
            = fixture.addImage(QStringLiteral("normal"), {{QStringLiteral("image_label_class_id"), good}});
        const qint64 bad_image
            = fixture.addImage(QStringLiteral("bad"), {{QStringLiteral("image_label_class_id"), anomaly}});
        QVERIFY(good >= 0);
        QVERIFY(anomaly >= 0);
        QVERIFY(normal_image >= 0);
        QVERIFY(bad_image >= 0);
        QVERIFY(fixture.writeImageList());
        QVERIFY(fixture.setTestSelection({good, anomaly}));
        QVERIFY(fixture.writePrediction(normal_image, anomalyPrediction(0.2)));
        QVERIFY(fixture.writePrediction(bad_image, anomalyPrediction(0.9)));

        AnomalyEvaluationEngine engine;
        EvaluationResult result;
        QString error;
        auto options = optionsFor(fixture, evaluation::Method::AnomalyDetection);
        options.confidence_threshold = 0.5;
        QVERIFY2(engine.evaluate(options, &result, &error), qPrintable(error));
        QCOMPARE(result.overall.tp, qint64(0));
        QCOMPARE(result.image_counts.tp, qint64(1));
        QCOMPARE(result.image_counts.fp, qint64(0));
        QCOMPARE(result.image_counts.fn, qint64(0));
        QCOMPARE(result.instance_records.size(), 2);
        QVERIFY(result.has_image_metrics);
        QVERIFY(!result.has_instance_metrics);
        QVERIFY(result.has_confusion_matrix);
        QVERIFY(!result.charts.isEmpty());
        QVERIFY(result.threshold_search.available);
        QCOMPARE(result.threshold_search.points.size(), 3);
        QVERIFY(std::abs(result.threshold_search.best_point.threshold - 0.9) < 1e-6);
        QCOMPARE(result.threshold_search.best_point.counts.tp, qint64(1));
        QCOMPARE(result.threshold_search.best_point.counts.fp, qint64(0));
        QCOMPARE(result.threshold_search.best_point.counts.fn, qint64(0));

        const QVariantMap *anomaly_distribution = nullptr;
        for (const QVariantMap &chart : result.charts)
        {
            if (chart.value(evaluation::fieldName(evaluation::Field::ChartId)).toString()
                == evaluation::chartIdKey(evaluation::ChartId::AnomalyScoreDistribution))
            {
                anomaly_distribution = &chart;
                break;
            }
        }
        QVERIFY(anomaly_distribution != nullptr);
        const QVariantList distribution_datasets
            = anomaly_distribution->value(evaluation::fieldName(evaluation::Field::Data))
                  .toMap()
                  .value(evaluation::fieldName(evaluation::Field::Datasets))
                  .toList();
        QCOMPARE(distribution_datasets.size(), 3);
        QCOMPARE(evaluation::seriesKindFromKey(
                     distribution_datasets.at(0).toMap()
                         .value(evaluation::fieldName(evaluation::Field::SeriesKind))
                         .toString()),
                 evaluation::SeriesKind::Good);
        QCOMPARE(evaluation::seriesKindFromKey(
                     distribution_datasets.at(1).toMap()
                         .value(evaluation::fieldName(evaluation::Field::SeriesKind))
                         .toString()),
                 evaluation::SeriesKind::Anomaly);
        QCOMPARE(evaluation::seriesKindFromKey(
                     distribution_datasets.at(2).toMap()
                         .value(evaluation::fieldName(evaluation::Field::SeriesKind))
                         .toString()),
                 evaluation::SeriesKind::BestThreshold);
        for (const QVariant &dataset_value : distribution_datasets)
            QVERIFY(evaluation::seriesKindFromKey(
                        dataset_value.toMap()
                            .value(evaluation::fieldName(evaluation::Field::SeriesKind))
                            .toString())
                    != evaluation::SeriesKind::Overall);

        const auto *good_cell = findCell(result, QStringLiteral("0"), QString::number(good));
        const auto *bad_cell = findCell(result, QStringLiteral("1"), QString::number(anomaly));
        QVERIFY(good_cell != nullptr);
        QVERIFY(bad_cell != nullptr);
        QCOMPARE(good_cell->count, qint64(1));
        QCOMPARE(bad_cell->count, qint64(1));
        QVERIFY(findCell(result, QStringLiteral("TOTAL"), QStringLiteral("TOTAL")) != nullptr);
    }

    void anomalyRegionsUseRawScoreMapAndHeatmapThresholdDoesNotChangeEvaluation()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::AnomalyDetection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 good = fixture.addClass(QStringLiteral("Good"), QStringLiteral("good"));
        const qint64 anomaly = fixture.addClass(QStringLiteral("Scratch"), QStringLiteral("anomaly"));
        const qint64 image = fixture.addImage(QStringLiteral("score-map"),
                                               {{QStringLiteral("image_label_class_id"), anomaly}});
        QVERIFY(good >= 0);
        QVERIFY(anomaly >= 0);
        QVERIFY(image >= 0);
        QVERIFY(fixture.writeImageList());
        QVERIFY(fixture.setTestSelection({good, anomaly}));
        QVERIFY(fixture.writePrediction(image, anomalyPrediction(0.9)));

        cv::Mat score_map = cv::Mat::zeros(10, 10, CV_32FC1);
        score_map(cv::Range(1, 3), cv::Range(1, 3)).setTo(0.8F);
        score_map(cv::Range(6, 9), cv::Range(6, 9)).setTo(1.2F);
        const QString score_path
            = QDir(fixture.predictionDirectory()).filePath(QStringLiteral("%1.tiff").arg(image));
        QVERIFY(cv::imwrite(score_path.toStdString(), score_map));

        AnomalyEvaluationEngine engine;
        auto options = optionsFor(fixture, evaluation::Method::AnomalyDetection);
        options.preprocessing_config = {
            {QStringLiteral("network"),
             QVariantMap{{QStringLiteral("image_size"), 20}, {QStringLiteral("center_crop_size"), 10}}}
        };
        options.evaluation_config
            = evaluation::normalizedEvaluationConfig({{QStringLiteral("classification_threshold"), 0.5},
                                                      {QStringLiteral("heatmap_threshold"), 1.0}});
        EvaluationResult first;
        QString          error;
        QVERIFY2(engine.evaluate(options, &first, &error), qPrintable(error));
        QCOMPARE(first.image_counts.tp, qint64(1));
        QCOMPARE(first.instance_records.size(), 1);
        const EvaluationInstanceRecord &first_event = first.instance_records.front();
        QCOMPARE(first_event.anomaly_model_polygons.size(), 2);
        QCOMPARE(first_event.anomaly_image_polygons.size(), 2);
        QCOMPARE(first_event.anomaly_score_map_path, score_path);

        // The provider first resizes the 32x32 source to 20x20, center-crops
        // 10x10, then scales the crop to the 10x10 score-map size. The image
        // polygons must use that exact inverse transform.
        QCOMPARE(first_event.anomaly_model_polygons.size(), first_event.anomaly_image_polygons.size());
        for (int polygon_index = 0; polygon_index < first_event.anomaly_model_polygons.size(); ++polygon_index)
        {
            const QVariantList model_polygon = first_event.anomaly_model_polygons.at(polygon_index).toList();
            const QVariantList image_polygon = first_event.anomaly_image_polygons.at(polygon_index).toList();
            QCOMPARE(model_polygon.size(), image_polygon.size());
            for (int point_index = 0; point_index < model_polygon.size(); ++point_index)
            {
                const QVariantMap model_point = model_polygon.at(point_index).toMap();
                const QVariantMap image_point = image_polygon.at(point_index).toMap();
                const double       model_x    = model_point.value(QStringLiteral("x")).toDouble();
                const double       model_y    = model_point.value(QStringLiteral("y")).toDouble();
                const double expected_x = ((model_x + 0.5) + 5.0) / 20.0 * 32.0 - 0.5;
                const double expected_y = ((model_y + 0.5) + 5.0) / 20.0 * 32.0 - 0.5;
                QVERIFY(std::abs(image_point.value(QStringLiteral("x")).toDouble() - expected_x) < 1e-9);
                QVERIFY(std::abs(image_point.value(QStringLiteral("y")).toDouble() - expected_y) < 1e-9);
            }
        }

        options.evaluation_config
            = evaluation::normalizedEvaluationConfig({{QStringLiteral("classification_threshold"), 0.5},
                                                      {QStringLiteral("heatmap_threshold"), 0.25}});
        EvaluationResult second;
        QVERIFY2(engine.evaluate(options, &second, &error), qPrintable(error));
        QCOMPARE(second.image_counts.tp, first.image_counts.tp);
        QCOMPARE(second.image_counts.fp, first.image_counts.fp);
        QCOMPARE(second.image_counts.fn, first.image_counts.fn);
        QCOMPARE(second.instance_records.front().anomaly_model_polygons.size(),
                 first_event.anomaly_model_polygons.size());
    }

    void anomalyClassificationUsesTheSameScoreMapAsRegions()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::AnomalyDetection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 good = fixture.addClass(QStringLiteral("Good"), QStringLiteral("good"));
        const qint64 anomaly = fixture.addClass(QStringLiteral("Scratch"), QStringLiteral("anomaly"));
        const qint64 image = fixture.addImage(QStringLiteral("score-map-consistency"),
                                               {{QStringLiteral("image_label_class_id"), anomaly}});
        QVERIFY(good >= 0);
        QVERIFY(anomaly >= 0);
        QVERIFY(image >= 0);
        QVERIFY(fixture.writeImageList());
        QVERIFY(fixture.setTestSelection({good, anomaly}));
        // Deliberately make the stored anomalib image score disagree with the
        // pixel map. Evaluation must use the shared pixel-score domain.
        QVERIFY(fixture.writePrediction(image, anomalyPrediction(0.95)));

        cv::Mat score_map = cv::Mat::zeros(10, 10, CV_32FC1);
        score_map(cv::Range(4, 6), cv::Range(4, 6)).setTo(0.75F);
        const QString score_path
            = QDir(fixture.predictionDirectory()).filePath(QStringLiteral("%1.tiff").arg(image));
        QVERIFY(cv::imwrite(score_path.toStdString(), score_map));

        AnomalyEvaluationEngine engine;
        auto options = optionsFor(fixture, evaluation::Method::AnomalyDetection);
        options.confidence_threshold = 0.5;
        EvaluationResult result;
        QString          error;
        QVERIFY2(engine.evaluate(options, &result, &error), qPrintable(error));
        QCOMPARE(result.image_counts.tp, qint64(1));
        QCOMPARE(result.instance_records.size(), 1);
        QCOMPARE(result.instance_records.front().pred_class_id, 1);
        QCOMPARE(result.instance_records.front().score, 0.75);
        QCOMPARE(result.instance_records.front().anomaly_model_polygons.size(), 1);

        score_map.setTo(0.25F);
        QVERIFY(cv::imwrite(score_path.toStdString(), score_map));
        result = {};
        QVERIFY2(engine.evaluate(options, &result, &error), qPrintable(error));
        QCOMPARE(result.image_counts.tp, qint64(0));
        QCOMPARE(result.image_counts.fn, qint64(1));
        QCOMPARE(result.instance_records.front().pred_class_id, 0);
        QVERIFY(result.instance_records.front().anomaly_model_polygons.isEmpty());
    }

    void anomalyPreprocessingTransformPreservesResizePaddingAndCropGeometry()
    {
        const QVariantMap preprocessing = {
            {QStringLiteral("network"),
             QVariantMap{{QStringLiteral("image_size"), QVariantList{15, 21}},
                         {QStringLiteral("padding"), QVariantList{1, 2, 2, 1}},
                         {QStringLiteral("center_crop_size"), QVariantList{11, 17}}}}
        };
        const AnomalyPreprocessingTransform transform
            = AnomalyPreprocessingTransform::fromConfig(QSize(101, 53), QSize(13, 9), preprocessing);
        QVERIFY(transform.isValid());
        QCOMPARE(transform.resizedSize(), QSize(21, 15));
        QCOMPARE(transform.padding(), QMargins(1, 2, 2, 1));
        QCOMPARE(transform.paddedSize(), QSize(24, 18));
        // 7 / 2 == 3.5; torchvision/torch rounds ties to the even integer 4.
        QCOMPARE(transform.cropRect(), QRect(4, 4, 17, 11));

        const QPointF model_point(3.25, 6.5);
        const double crop_edge_x = (model_point.x() + 0.5) * 17.0 / 13.0;
        const double crop_edge_y = (model_point.y() + 0.5) * 11.0 / 9.0;
        const QPointF expected((4.0 + crop_edge_x - 1.0) * 101.0 / 21.0 - 0.5,
                               (4.0 + crop_edge_y - 2.0) * 53.0 / 15.0 - 0.5);
        const QPointF image_point = transform.modelToImage(model_point);
        QVERIFY(std::abs(image_point.x() - expected.x()) < 1e-9);
        QVERIFY(std::abs(image_point.y() - expected.y()) < 1e-9);

        const QPointF round_trip = transform.imageToModel(image_point);
        QVERIFY(std::abs(round_trip.x() - model_point.x()) < 1e-9);
        QVERIFY(std::abs(round_trip.y() - model_point.y()) < 1e-9);

        QImage source(101, 53, QImage::Format_RGB888);
        source.fill(QColor(80, 120, 180));
        const QImage model_image = transform.applyToImage(source);
        QCOMPARE(model_image.size(), QSize(13, 9));
    }

    void anomalyPreprocessingTransformMatchesCenterCropAutomaticPadding()
    {
        const QVariantMap preprocessing = {
            {QStringLiteral("network"),
             QVariantMap{{QStringLiteral("image_size"), QVariantList{6, 8}},
                         {QStringLiteral("center_crop_size"), QVariantList{9, 11}}}}
        };
        const AnomalyPreprocessingTransform transform
            = AnomalyPreprocessingTransform::fromConfig(QSize(20, 10), QSize(11, 9), preprocessing);
        QVERIFY(transform.isValid());
        QCOMPARE(transform.resizedSize(), QSize(8, 6));
        QCOMPARE(transform.padding(), QMargins(1, 1, 2, 2));
        QCOMPARE(transform.paddedSize(), QSize(11, 9));
        QCOMPARE(transform.cropRect(), QRect(0, 0, 11, 9));

        const QPointF image_point = transform.modelToImage(QPointF(1.0, 1.0));
        const QPointF round_trip = transform.imageToModel(image_point);
        QVERIFY(std::abs(round_trip.x() - 1.0) < 1e-9);
        QVERIFY(std::abs(round_trip.y() - 1.0) < 1e-9);
    }

    void heatmapProviderUsesFixedModelSizeAndGlobalThreshold()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString image_path = QDir(temporary.path()).filePath(QStringLiteral("source.png"));
        const QString score_path = QDir(temporary.path()).filePath(QStringLiteral("1.tiff"));
        QImage        source(4, 4, QImage::Format_RGB888);
        source.fill(QColor(80, 80, 80));
        QVERIFY(source.save(image_path, "PNG"));

        cv::Mat score_map = cv::Mat::zeros(4, 4, CV_32FC1);
        score_map.at<float>(0, 0) = 0.5F;
        score_map.at<float>(3, 3) = 1.0F;
        QVERIFY(cv::imwrite(score_path.toStdString(), score_map));

        QUrlQuery query;
        query.addQueryItem(QStringLiteral("path"), image_path);
        query.addQueryItem(QStringLiteral("scorePath"), score_path);
        query.addQueryItem(QStringLiteral("heatmap"), QStringLiteral("1"));
        query.addQueryItem(QStringLiteral("heatmapThreshold"), QStringLiteral("1"));

        EvaluationThumbnailImageProvider provider;
        QSize                              output_size;
        const QImage first = provider.requestImage(QStringLiteral("heatmap?%1").arg(query.toString(QUrl::FullyEncoded)),
                                                   &output_size, QSize(1, 1));
        QVERIFY(!first.isNull());
        QCOMPARE(output_size, QSize(4, 4));
        QCOMPARE(first.size(), QSize(4, 4));

        query.removeAllQueryItems(QStringLiteral("heatmapThreshold"));
        query.addQueryItem(QStringLiteral("heatmapThreshold"), QStringLiteral("0.25"));
        const QImage second
            = provider.requestImage(QStringLiteral("heatmap?%1").arg(query.toString(QUrl::FullyEncoded)), nullptr,
                                    QSize(1, 1));
        QVERIFY(!second.isNull());
        QCOMPARE(second.size(), QSize(4, 4));
        QVERIFY(first != second);
    }

    void heatmapProviderReadsDoublePrecisionScoreMaps()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString image_path = QDir(temporary.path()).filePath(QStringLiteral("source.png"));
        const QString score_path = QDir(temporary.path()).filePath(QStringLiteral("double.tiff"));
        QImage        source(4, 4, QImage::Format_RGB888);
        source.fill(QColor(80, 80, 80));
        QVERIFY(source.save(image_path, "PNG"));

        cv::Mat score_map(4, 4, CV_64FC1, cv::Scalar(0.0));
        score_map.at<double>(0, 0) = 0.5;
        score_map.at<double>(3, 3) = 1.0;
        QVERIFY(cv::imwrite(score_path.toStdString(), score_map));

        QUrlQuery query;
        query.addQueryItem(QStringLiteral("path"), image_path);
        query.addQueryItem(QStringLiteral("scorePath"), score_path);
        query.addQueryItem(QStringLiteral("heatmap"), QStringLiteral("1"));
        query.addQueryItem(QStringLiteral("heatmapThreshold"), QStringLiteral("1"));

        EvaluationThumbnailImageProvider provider;
        const QImage rendered
            = provider.requestImage(QStringLiteral("heatmap-double?%1").arg(query.toString(QUrl::FullyEncoded)),
                                    nullptr, QSize(1, 1));
        QVERIFY(!rendered.isNull());
        QCOMPARE(rendered.size(), QSize(4, 4));
    }

    void cancellationAndInvalidInputFailWithoutPartialResult()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        auto cancelled = std::make_shared<std::atomic_bool>(true);
        auto options = optionsFor(fixture, evaluation::Method::Detection);
        options.cancel_token = cancelled;
        DetectionEvaluationEngine engine;
        EvaluationResult result;
        QString error;
        QVERIFY(!engine.evaluate(options, &result, &error));
        QVERIFY(error.contains(QStringLiteral("取消")));
        QVERIFY(result.images.isEmpty());

        error.clear();
        options.cancel_token.reset();
        options.project_database_path.clear();
        QVERIFY(!engine.evaluate(options, &result, &error));
        QVERIFY(error.contains(QStringLiteral("路径")));
    }

    void cancellationDuringInputLoadingLeavesNoPartialImages()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 cat = fixture.addClass(QStringLiteral("Cat"), QStringLiteral("normal"));
        for (int index = 0; index < 3; ++index)
        {
            const qint64 image = fixture.addImage(QStringLiteral("cancel-%1").arg(index));
            QVERIFY(fixture.addDetectionLabel(image, cat, 0, 0, 5, 5) >= 0);
        }
        QVERIFY(fixture.writeImageList());
        QVERIFY(fixture.setTestSelection({cat}));

        auto cancelled = std::make_shared<std::atomic_bool>(false);
        auto options = optionsFor(fixture, evaluation::Method::Detection);
        options.cancel_token = cancelled;
        int dimensions_calls = 0;
        options.image_dimensions_provider = [cancelled, &dimensions_calls](qint64, int *width, int *height)
        {
            *width = 32;
            *height = 32;
            ++dimensions_calls;
            if (dimensions_calls == 1)
                cancelled->store(true, std::memory_order_relaxed);
            return true;
        };
        DetectionEvaluationEngine engine;
        EvaluationResult result;
        QString error;
        QVERIFY(!engine.evaluate(options, &result, &error));
        QVERIFY(error.contains(QStringLiteral("取消")));
        QVERIFY(result.images.isEmpty());
    }

    void builtInRegistryCreatesConcreteEngines()
    {
        auto &registry = EvaluationEngineRegistry::instance();
        QVERIFY(dynamic_cast<AnomalyEvaluationEngine *>(
                    registry.createEngine(evaluation::Method::AnomalyDetection).get())
                != nullptr);
        QVERIFY(dynamic_cast<DetectionEvaluationEngine *>(registry.createEngine(evaluation::Method::Detection).get())
                != nullptr);
        QVERIFY(registry.createEngine(evaluation::Method::Unknown) == nullptr);
    }
};

REGISTER_TEST(EvaluationEngineTest)

#include "test_EvaluationEngine.moc"
