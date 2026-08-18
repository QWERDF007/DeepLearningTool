#include "../test_runner.h"

#include "TestFixture.h"

#include "model/AnomalyEvaluationEngine.h"
#include "model/DetectionEvaluationEngine.h"
#include "model/EvaluationEngineRegistry.h"
#include "model/EvaluationResult.h"
#include "model/ModelEvaluationOptions.h"
#include "model/ModelEvaluationProtocol.h"
#include "model/SegmentationEvaluationEngine.h"

#include <QTest>

#include <atomic>
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

        QString conversion_error;
        const QVariantMap protocol = evaluationResultToProtocolMap(result, &conversion_error);
        QVERIFY2(!protocol.isEmpty(), qPrintable(conversion_error));
        QCOMPARE(protocol.value(evaluation::fieldName(evaluation::Field::PredictionCount)).toInt(), 1);
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

    void detectionCountsClassMismatchFalsePositiveAndFalseNegative()
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
        QVERIFY(findEvent(result, evaluation::Status::ClassMismatch) != nullptr);
        QVERIFY(findEvent(result, evaluation::Status::FalsePositive) != nullptr);
        QVERIFY(findEvent(result, evaluation::Status::FalseNegative) != nullptr);
        QVERIFY(findCell(result, QStringLiteral("FN"), QString::number(cat)) != nullptr);
        QVERIFY(findCell(result, QString::number(cat), QStringLiteral("FP")) != nullptr);
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

        const auto *good_cell = findCell(result, QStringLiteral("0"), QString::number(good));
        const auto *bad_cell = findCell(result, QStringLiteral("1"), QString::number(anomaly));
        QVERIFY(good_cell != nullptr);
        QVERIFY(bad_cell != nullptr);
        QCOMPARE(good_cell->count, qint64(1));
        QCOMPARE(bad_cell->count, qint64(1));
        QVERIFY(findCell(result, QStringLiteral("TOTAL"), QStringLiteral("TOTAL")) != nullptr);
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
