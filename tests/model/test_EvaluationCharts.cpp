#include "../test_runner.h"
#include "model/EvaluationCharts.h"
#include "model/ModelEvaluationProtocol.h"

#include <QTest>

#include <algorithm>
#include <memory>

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
        normal.anomaly_score_map = std::make_shared<const EvaluationScoreMap>(EvaluationScoreMap{1, 1, {0.2}});
        EvaluationImageData anomaly;
        anomaly.id                   = 2;
        anomaly.name                 = QStringLiteral("anomaly.png");
        anomaly.gt.push_back(EvaluationGroundTruthData{1, 2, QStringLiteral("Scratch"), {}, {}, {}, true});
        anomaly.anomaly_score_map = std::make_shared<const EvaluationScoreMap>(EvaluationScoreMap{1, 1, {0.9}});
        images.insert(normal.id, normal);
        images.insert(anomaly.id, anomaly);

        const QVariantMap diagnostic = {
            {evaluation::fieldName(evaluation::Field::Image), evaluationMetricMap(1, 0, 0)}
        };
        const EvaluationChartOutput output = buildAnomalyEvaluationCharts(images, diagnostic, 0.5);
        QVERIFY(output.available);
        QCOMPARE(output.charts.size(), 2);
        QCOMPARE(output.chart_kinds.size(), 2);
        const QVariantMap pr_chart = output.charts.at(0).toMap();
        QCOMPARE(pr_chart.value(evaluation::fieldName(evaluation::Field::ChartId)).toString(),
                 evaluation::chartIdKey(evaluation::ChartId::PrecisionRecall));
        const QVariantMap chart = output.charts.at(1).toMap();
        QCOMPARE(chart.value(evaluation::fieldName(evaluation::Field::ChartId)).toString(),
                 evaluation::chartIdKey(evaluation::ChartId::AnomalyScoreDistribution));
        QCOMPARE(chart.value(evaluation::fieldName(evaluation::Field::Kind)).toString(),
                 evaluation::chartKindKey(evaluation::ChartKind::Line));
        const QVariantMap data = chart.value(evaluation::fieldName(evaluation::Field::Data)).toMap();
        QVERIFY(data.contains(evaluation::fieldName(evaluation::Field::Labels)));
        const QVariantList datasets = data.value(evaluation::fieldName(evaluation::Field::Datasets)).toList();
        QCOMPARE(datasets.size(), 5);
        bool has_good = false;
        bool has_anomaly = false;
        bool has_overall = false;
        bool has_best = false;
        bool has_good_max = false;
        bool has_anomaly_min = false;
        int distribution_count = 0;
        for (const QVariant &value : datasets)
        {
            const QVariantMap dataset = value.toMap();
            const auto kind = evaluation::seriesKindFromKey(
                dataset.value(evaluation::fieldName(evaluation::Field::SeriesKind)).toString());
            if (kind == evaluation::SeriesKind::Good)
            {
                has_good = true;
                ++distribution_count;
            }
            else if (kind == evaluation::SeriesKind::Anomaly)
            {
                has_anomaly = true;
                ++distribution_count;
            }
            if (kind == evaluation::SeriesKind::BestThreshold)
            {
                const QString label = dataset.value(evaluation::fieldName(evaluation::Field::Label)).toString();
                QCOMPARE(dataset.value(QStringLiteral("order")).toInt(), -1);
                const QString tooltip_label = dataset.value(QStringLiteral("tooltipLabel")).toString();
                has_good_max = has_good_max || (label.startsWith(QStringLiteral("正常 最大分数："))
                                                && tooltip_label == QStringLiteral("正常最大分数"));
                has_anomaly_min = has_anomaly_min || (label.startsWith(QStringLiteral("异常 最小分数："))
                                                      && tooltip_label == QStringLiteral("异常最小分数"));
            }
            has_overall = has_overall || kind == evaluation::SeriesKind::Overall;
            has_best = has_best || kind == evaluation::SeriesKind::BestThreshold;
        }
        QCOMPARE(distribution_count, 2);
        QVERIFY(has_good);
        QVERIFY(has_anomaly);
        QVERIFY(!has_overall);
        QVERIFY(has_best);
        QVERIFY(has_good_max);
        QVERIFY(has_anomaly_min);

        const QVariantMap good_dataset = datasets.at(0).toMap();
        const QVariantMap anomaly_dataset = datasets.at(1).toMap();
        QCOMPARE(good_dataset.value(QStringLiteral("borderColor")).toString(), QStringLiteral("#43A047"));
        QCOMPARE(anomaly_dataset.value(QStringLiteral("borderColor")).toString(), QStringLiteral("#E53935"));
    }

    void anomalyChartRetainsBothDistributionSeriesWhenOneGroupIsEmpty()
    {
        EvaluationImageData normal;
        normal.id = 1;
        normal.anomaly_score_map
            = std::make_shared<const EvaluationScoreMap>(EvaluationScoreMap{1, 1, {0.2}});

        const QVariantMap chart = anomalyScoreChartForImages({normal}, 0.5);
        const QVariantList datasets = chart.value(evaluation::fieldName(evaluation::Field::Data))
                                          .toMap()
                                          .value(evaluation::fieldName(evaluation::Field::Datasets))
                                          .toList();
        QCOMPARE(datasets.size(), 3);
        QCOMPARE(evaluation::seriesKindFromKey(
                     datasets.at(0).toMap().value(evaluation::fieldName(evaluation::Field::SeriesKind)).toString()),
                 evaluation::SeriesKind::Good);
        QCOMPARE(evaluation::seriesKindFromKey(
                     datasets.at(1).toMap().value(evaluation::fieldName(evaluation::Field::SeriesKind)).toString()),
                 evaluation::SeriesKind::Anomaly);
        QVERIFY(!datasets.at(1).toMap().value(evaluation::fieldName(evaluation::Field::Data)).toList().isEmpty());
    }

    void confidenceDistributionUsesOneOverallSeries()
    {
        EvaluationImageData image = detectionImage();
        image.predictions.push_back(EvaluationPredictionData{
            QStringLiteral("p2"),
            image.id,
            4,
            QStringLiteral("Dog"),
            0.2,
            {},
            {},
            EvaluationBox{20, 20, 5, 5}
        });

        const QVariantMap chart = confidenceDistributionChartForImages({image});
        QCOMPARE(chart.value(evaluation::fieldName(evaluation::Field::ChartId)).toString(),
                 evaluation::chartIdKey(evaluation::ChartId::ConfidenceDistribution));
        const QVariantList datasets = chart.value(evaluation::fieldName(evaluation::Field::Data))
                                          .toMap()
                                          .value(evaluation::fieldName(evaluation::Field::Datasets))
                                          .toList();
        QCOMPARE(datasets.size(), 1);
        const QVariantMap dataset = datasets.front().toMap();
        QCOMPARE(evaluation::seriesKindFromKey(
                     dataset.value(evaluation::fieldName(evaluation::Field::SeriesKind)).toString()),
                 evaluation::SeriesKind::Overall);
        const QVariantList points = dataset.value(evaluation::fieldName(evaluation::Field::Data)).toList();
        QCOMPARE(points.size(), 24);
        int total = 0;
        for (const QVariant &value : points)
            total += value.toMap().value(QStringLiteral("y")).toInt();
        QCOMPARE(total, 2);
    }

    void anomalyChartAxisIncludesBestThresholdOutsideSampleRange()
    {
        EvaluationImageData normal;
        normal.id = 1;
        normal.anomaly_score_map
            = std::make_shared<const EvaluationScoreMap>(EvaluationScoreMap{1, 1, {0.2}});

        EvaluationThresholdSearchResult search;
        search.available = true;
        search.best_point.threshold = 0.9;
        const QVariantMap chart = anomalyScoreChartForImages({normal}, 0.5, &search);
        const QVariantMap options = chart.value(evaluation::fieldName(evaluation::Field::Options)).toMap();
        const QVariantMap scales  = options.value(QStringLiteral("scales")).toMap();
        const QVariantMap ticks   = scales.value(QStringLiteral("xAxes")).toList().front().toMap()
                                         .value(QStringLiteral("ticks"))
                                         .toMap();
        QVERIFY(ticks.value(QStringLiteral("max")).toDouble() >= 0.9);
    }

    void instanceThresholdSearchMatchesEachClassIndependently()
    {
        EvaluationImageData image;
        image.id = 1;
        image.gt = {
            EvaluationGroundTruthData{1, 2, QStringLiteral("Dog"), {}, {}, EvaluationBox{0, 0, 10, 10}, false}
        };
        image.predictions = {
            EvaluationPredictionData{QStringLiteral("cat-pred"), 1, 1, QStringLiteral("Cat"), 0.9, {}, {},
                                     EvaluationBox{0, 0, 10, 10}}
        };

        const EvaluationThresholdSearchResult search = searchInstanceThresholdForImages(
            {image}, 0.5, evaluation::MatchingStrategy::GreedyIoU);
        QVERIFY(search.available);
        const auto point = std::find_if(search.points.cbegin(), search.points.cend(),
                                        [](const EvaluationThresholdPoint &value)
                                        { return qFuzzyCompare(value.threshold + 1.0, 1.9); });
        QVERIFY(point != search.points.cend());
        QCOMPARE(point->counts.tp, qint64(0));
        QCOMPARE(point->counts.fp, qint64(1));
        QCOMPARE(point->counts.fn, qint64(1));
    }

    void precisionRecallChartContainsMicroAndClassSeries()
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
        bool has_micro   = false;
        bool has_class   = false;
        bool has_best    = false;
        for (const QVariant &value : datasets)
        {
            const QVariantMap dataset = value.toMap();
            const auto        kind    = evaluation::seriesKindFromKey(
                dataset.value(evaluation::fieldName(evaluation::Field::SeriesKind)).toString());
            has_micro   = has_micro || kind == evaluation::SeriesKind::Micro;
            has_class   = has_class || kind == evaluation::SeriesKind::Class;
            const QVariantList points = dataset.value(evaluation::fieldName(evaluation::Field::Data)).toList();
            if (kind == evaluation::SeriesKind::BestThreshold)
            {
                has_best = true;
                QCOMPARE(points.size(), 1);
                QVERIFY(dataset.value(evaluation::fieldName(evaluation::Field::ReadOnly)).toBool());
                QCOMPARE(dataset.value(QStringLiteral("order")).toInt(), -1);
                QVERIFY(dataset.contains(evaluation::fieldName(evaluation::Field::F1)));
                const QVariantMap best_point = points.front().toMap();
                QVERIFY(best_point.contains(evaluation::fieldName(evaluation::Field::F1)));
                QCOMPARE(best_point.value(evaluation::fieldName(evaluation::Field::F1)).toDouble(),
                         dataset.value(evaluation::fieldName(evaluation::Field::F1)).toDouble());
            }
            else
            {
                QCOMPARE(points.size(), kPrecisionRecallInterpolationPoints);
                QVERIFY(points.front().toMap().contains(evaluation::fieldName(evaluation::Field::Threshold)));
                QVERIFY(points.back().toMap().contains(evaluation::fieldName(evaluation::Field::Threshold)));
                for (const QVariant &point_value : points)
                {
                    const QVariantMap point = point_value.toMap();
                    QVERIFY(point.contains(evaluation::fieldName(evaluation::Field::F1)));
                    const double precision = point.value(QStringLiteral("y")).toDouble();
                    const double recall    = point.value(QStringLiteral("x")).toDouble();
                    const double expected_f1
                        = precision + recall > 0.0 ? 2.0 * precision * recall / (precision + recall) : 0.0;
                    QVERIFY(qFuzzyCompare(point.value(evaluation::fieldName(evaluation::Field::F1)).toDouble() + 1.0,
                                          expected_f1 + 1.0));
                }
            }
        }
        QVERIFY(has_micro);
        QVERIFY(has_class);
        QVERIFY(has_best);
    }

    void precisionRecallChartIncludesPredictionOnlyClasses()
    {
        EvaluationImageData image = detectionImage();
        image.predictions.push_back(EvaluationPredictionData{
            QStringLiteral("p2"),
            image.id,
            4,
            QStringLiteral("Dog"),
            0.8,
            QVariantMap{{QStringLiteral("x"), 20.0},
                        {QStringLiteral("y"), 20.0},
                        {QStringLiteral("width"), 5.0},
                        {QStringLiteral("height"), 5.0}},
            {},
            EvaluationBox{20, 20, 5, 5}
        });

        const QVariantMap chart = precisionRecallChartForImages(
            {image}, {{3, QStringLiteral("Cat")}, {4, QStringLiteral("Dog")}}, 0.5,
            evaluation::MatchingStrategy::GreedyIoU);
        const QVariantList datasets = chart.value(evaluation::fieldName(evaluation::Field::Data))
                                          .toMap()
                                          .value(evaluation::fieldName(evaluation::Field::Datasets))
                                          .toList();

        bool has_cat = false;
        bool has_dog = false;
        for (const QVariant &value : datasets)
        {
            const QVariantMap dataset = value.toMap();
            if (evaluation::seriesKindFromKey(
                    dataset.value(evaluation::fieldName(evaluation::Field::SeriesKind)).toString())
                != evaluation::SeriesKind::Class)
                continue;
            const int class_id = dataset.value(evaluation::fieldName(evaluation::Field::ClassId)).toInt();
            has_cat = has_cat || class_id == 3;
            has_dog = has_dog || class_id == 4;
        }
        QVERIFY(has_cat);
        QVERIFY(has_dog);
    }

    void chartOutputCarriesOfficialMetricsAndBestThreshold()
    {
        const EvaluationImageData image = detectionImage();
        const QMap<qint64, EvaluationImageData> images{{image.id, image}};
        const QVariantMap diagnostic = {
            {evaluation::fieldName(evaluation::Field::Instance),
             QVariantMap{
                 {evaluation::fieldName(evaluation::Field::Overall), evaluationMetricMap(1, 0, 0)},
                 {evaluation::fieldName(evaluation::Field::PerClass),
                  QVariantList{QVariantMap{{evaluation::fieldName(evaluation::Field::ClassId), 3},
                                            {evaluation::fieldName(evaluation::Field::ClassName), QStringLiteral("Cat")},
                                            {evaluation::fieldName(evaluation::Field::Tp), 1},
                                            {evaluation::fieldName(evaluation::Field::Fp), 0},
                                            {evaluation::fieldName(evaluation::Field::Fn), 0}}}}}},
            {evaluation::fieldName(evaluation::Field::Image), evaluationMetricMap(1, 0, 0)}};
        EvaluationThresholdSearchResult search;
        search.available = true;
        search.points = {EvaluationThresholdPoint{0.9, EvaluationCounts{1, 0, 0}, 1.0, 1.0, 1.0}};
        search.best_point = search.points.front();

        const EvaluationChartOutput output = buildInstanceMatchingEvaluationCharts(
            images, 0.5, 0.5, evaluation::MatchingStrategy::GreedyIoU, diagnostic, {}, &search);
        QVERIFY(output.available);
        QVERIFY(output.metrics.value(evaluation::fieldName(evaluation::Field::Available)).toBool());
        QVERIFY(!output.image_definition.isEmpty());
        QCOMPARE(output.charts.size(), 2);
        const QVariantList datasets = output.charts.front().toMap()
                                           .value(evaluation::fieldName(evaluation::Field::Data))
                                           .toMap()
                                           .value(evaluation::fieldName(evaluation::Field::Datasets))
                                           .toList();
        bool found_best_threshold = false;
        for (const QVariant &value : datasets)
        {
            const QVariantMap dataset = value.toMap();
            if (dataset.value(evaluation::fieldName(evaluation::Field::SeriesKind)).toString()
                == evaluation::seriesKindKey(evaluation::SeriesKind::BestThreshold))
            {
                found_best_threshold = true;
                QCOMPARE(dataset.value(evaluation::fieldName(evaluation::Field::Threshold)).toDouble(), 0.9);
            }
        }
        QVERIFY(found_best_threshold);
    }
};

REGISTER_TEST(EvaluationChartsTest)

#include "test_EvaluationCharts.moc"
