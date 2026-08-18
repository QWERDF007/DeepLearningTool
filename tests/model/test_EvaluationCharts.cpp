#include "../test_runner.h"
#include "model/EvaluationCharts.h"
#include "model/ModelEvaluationProtocol.h"

#include <QTest>

using namespace dltool::model;

namespace {

EvaluationImageData detectionImage()
{
    EvaluationImageData image;
    image.id     = 1;
    image.name   = QStringLiteral("image.png");
    image.width  = 32;
    image.height = 32;
    image.gt.push_back(EvaluationGroundTruthData{
        1,
        3,
        QStringLiteral("Cat"),
        QVariantMap{{QStringLiteral("x"), 0.0},
                    {QStringLiteral("y"), 0.0},
                    {QStringLiteral("width"), 10.0},
                    {QStringLiteral("height"), 10.0}},
        {},
        EvaluationBox{0, 0, 10, 10},
        false
    });
    image.predictions.push_back(EvaluationPredictionData{
        QStringLiteral("p1"),
        1,
        3,
        QStringLiteral("Cat"),
        0.9,
        QVariantMap{{QStringLiteral("x"), 0.0},
                    {QStringLiteral("y"), 0.0},
                    {QStringLiteral("width"), 10.0},
                    {QStringLiteral("height"), 10.0}},
        {},
        EvaluationBox{0, 0, 10, 10}
    });
    return image;
}

} // namespace

class EvaluationChartsTest : public QObject
{
    Q_OBJECT

private slots:

    void metricMapTracksDefinedFields()
    {
        const QVariantMap defined = evaluationMetricMap(2, 1, 1);
        QCOMPARE(defined.value(evaluation::fieldName(evaluation::Field::Tp)).toLongLong(), qint64(2));
        QCOMPARE(defined.value(evaluation::fieldName(evaluation::Field::Fp)).toLongLong(), qint64(1));
        QCOMPARE(defined.value(evaluation::fieldName(evaluation::Field::Fn)).toLongLong(), qint64(1));
        QVERIFY(defined.value(evaluation::fieldName(evaluation::Field::PrecisionDefined)).toBool());
        QVERIFY(defined.value(evaluation::fieldName(evaluation::Field::RecallDefined)).toBool());
        QVERIFY(defined.value(evaluation::fieldName(evaluation::Field::F1Defined)).toBool());
        QCOMPARE(defined.value(evaluation::fieldName(evaluation::Field::Precision)).toDouble(), 2.0 / 3.0);
        QCOMPARE(defined.value(evaluation::fieldName(evaluation::Field::Recall)).toDouble(), 2.0 / 3.0);

        const QVariantMap undefined = evaluationMetricMap(0, 0, 0);
        QVERIFY(!undefined.value(evaluation::fieldName(evaluation::Field::PrecisionDefined)).toBool());
        QVERIFY(!undefined.value(evaluation::fieldName(evaluation::Field::RecallDefined)).toBool());
        QVERIFY(!undefined.value(evaluation::fieldName(evaluation::Field::F1Defined)).toBool());
    }

    void anomalyChartHasStructuredDistribution()
    {
        QMap<qint64, EvaluationImageData> images;
        EvaluationImageData               normal;
        normal.id                   = 1;
        normal.name                 = QStringLiteral("normal.png");
        normal.max_prediction_score = 0.2;
        EvaluationImageData anomaly;
        anomaly.id                   = 2;
        anomaly.name                 = QStringLiteral("anomaly.png");
        anomaly.max_prediction_score = 0.9;
        images.insert(normal.id, normal);
        images.insert(anomaly.id, anomaly);

        const QVariantMap diagnostic = {
            {evaluation::fieldName(evaluation::Field::Image), evaluationMetricMap(1, 0, 0)}
        };
        const EvaluationChartOutput output = buildAnomalyEvaluationCharts(images, diagnostic, 0.5);
        QVERIFY(output.available);
        QCOMPARE(output.charts.size(), 1);
        const QVariantMap chart = output.charts.first().toMap();
        QCOMPARE(chart.value(evaluation::fieldName(evaluation::Field::ChartId)).toString(),
                 evaluation::chartIdKey(evaluation::ChartId::AnomalyScoreDistribution));
        QCOMPARE(chart.value(evaluation::fieldName(evaluation::Field::Kind)).toString(),
                 evaluation::chartKindKey(evaluation::ChartKind::Line));
        const QVariantMap data = chart.value(evaluation::fieldName(evaluation::Field::Data)).toMap();
        QVERIFY(data.contains(evaluation::fieldName(evaluation::Field::Labels)));
        QVERIFY(data.contains(evaluation::fieldName(evaluation::Field::Datasets)));
    }

    void precisionRecallChartContainsAverageAndClassSeries()
    {
        const EvaluationImageData image = detectionImage();
        const QVariantMap         chart = precisionRecallChartForImages(
            {
                image
        },
            {{3, QStringLiteral("Cat")}}, 0.5, evaluation::MatchingStrategy::GreedyIoU);
        QCOMPARE(chart.value(evaluation::fieldName(evaluation::Field::ChartId)).toString(),
                 evaluation::chartIdKey(evaluation::ChartId::PrecisionRecall));
        const QVariantList datasets = chart.value(evaluation::fieldName(evaluation::Field::Data))
                                          .toMap()
                                          .value(evaluation::fieldName(evaluation::Field::Datasets))
                                          .toList();
        QVERIFY(datasets.size() >= 2);
        bool has_average = false;
        bool has_class   = false;
        for (const QVariant &value : datasets)
        {
            const QVariantMap dataset = value.toMap();
            const auto        kind    = evaluation::seriesKindFromKey(
                dataset.value(evaluation::fieldName(evaluation::Field::SeriesKind)).toString());
            has_average = has_average || kind == evaluation::SeriesKind::Average;
            has_class   = has_class || kind == evaluation::SeriesKind::Class;
            QVERIFY(dataset.value(evaluation::fieldName(evaluation::Field::Data)).toList().size()
                    == kPrecisionRecallInterpolationPoints);
        }
        QVERIFY(has_average);
        QVERIFY(has_class);
    }

    void completeAssemblyContainsDiagnosticAndMatrix()
    {
        const EvaluationImageData         image = detectionImage();
        QMap<qint64, EvaluationImageData> images{
            {image.id, image}
        };
        const QMap<int, QString> classes{
            {3, QStringLiteral("Cat")}
        };
        const QMap<int, QString> class_colors{
            {3, QStringLiteral("#ef5350")}
        };
        const QMap<int, EvaluationCounts> per_class{
            {3, EvaluationCounts{1, 0, 0}}
        };
        const EvaluationCounts      overall{1, 0, 0};
        const EvaluationCounts      image_counts{1, 0, 0};
        const QMap<QString, qint64> matrix{
            {QStringLiteral("3\x1f"
                            "3"),
             1}
        };
        const QVariantList events{QVariantMap{{QStringLiteral("event_uuid"), QStringLiteral("event-1")}}};
        const QVariantMap  result = assembleEvaluationResult({images,
                                                              classes,
                                                              class_colors,
                                                              per_class,
                                                              overall,
                                                              image_counts,
                                                              matrix,
                                                              events,
                                                              1,
                                                              evaluation::Method::Detection,
                                                              0.5,
                                                              0.5,
                                                              evaluation::MatchingStrategy::GreedyIoU,
                                                              {},
                                                              {},
                                                              nullptr});
        QVERIFY(!result.isEmpty());
        QVERIFY(result.contains(evaluation::fieldName(evaluation::Field::DiagnosticMetrics)));
        QVERIFY(result.contains(evaluation::fieldName(evaluation::Field::ConfusionMatrix)));
        const QVariantList cells = result.value(evaluation::fieldName(evaluation::Field::ConfusionMatrix))
                                       .toMap()
                                       .value(evaluation::fieldName(evaluation::Field::Cells))
                                       .toList();
        QVERIFY(!cells.isEmpty());
        QCOMPARE(result.value(evaluation::fieldName(evaluation::Field::ImageRecords)).toList().size(), 1);
        QCOMPARE(result.value(evaluation::fieldName(evaluation::Field::InstanceRecords)).toList().size(), 1);
    }
};

REGISTER_TEST(EvaluationChartsTest)

#include "test_EvaluationCharts.moc"
