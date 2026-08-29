#include "../test_runner.h"

#include "model/AggregateEvaluation.h"
#include "model/ModelEvaluationProtocol.h"

#include <QTest>

#include <memory>

using namespace dltool::model;

class AggregateEvaluationTest : public QObject
{
    Q_OBJECT

private slots:
    void aggregatesInstanceAndImageMetrics()
    {
        EvaluationAggregateInput input;
        input.has_instance_metrics = true;
        input.has_image_metrics = true;
        input.has_confusion_matrix = true;
        input.class_catalog = {{3, QStringLiteral("Cat")}, {4, QStringLiteral("Dog")}};
        input.instances = {{evaluation::Status::TruePositive, QStringLiteral("Cat"), QStringLiteral("Cat"), 3, 3},
                           {evaluation::Status::FalsePositive, {}, QStringLiteral("Cat"), -1, 3},
                           {evaluation::Status::FalseNegative, QStringLiteral("Dog"), {}, 4, -1},
                           {evaluation::Status::ClassMismatch, QStringLiteral("Cat"), QStringLiteral("Dog"), 3, 4}};

        EvaluationImageRecord good;
        good.id = 1;
        good.dataset_id = 10;
        EvaluationImageRecord bad;
        bad.id = 2;
        bad.dataset_id = 10;
        bad.gt.push_back(EvaluationGroundTruthData{1, 3, QStringLiteral("Cat"), {}, {}, {}, false});
        bad.predictions.push_back(EvaluationPredictionData{QStringLiteral("p"), 2, 3,
                                                             QStringLiteral("Cat"), 0.8, {}, {}, {}});
        rebuildImageDerivedValues(bad);
        input.images = {good, bad};

        const EvaluationAggregateOutput output = aggregateEvaluation(input);
        QVERIFY(!output.instance_metrics.empty());
        QVERIFY(!output.image_metrics.empty());
        QVERIFY(!output.per_class_metrics.empty());
        QVERIFY(!output.confusion.empty());

        auto metric = [&output](const std::vector<EvaluationMetricRecord> &records, const QString &key)
            -> const EvaluationMetricRecord *
        {
            for (const auto &record : records)
                if (record.key == key)
                    return &record;
            return nullptr;
        };
        const auto *overall = metric(output.instance_metrics, QStringLiteral("overall"));
        QVERIFY(overall != nullptr);
        QCOMPARE(overall->tp, qint64(1));
        QCOMPARE(overall->fp, qint64(2));
        QCOMPARE(overall->fn, qint64(2));
        const auto *image = metric(output.image_metrics, QStringLiteral("image"));
        QVERIFY(image != nullptr);
        QCOMPARE(image->tp, qint64(1));
        QCOMPARE(image->fn, qint64(0));
    }

    void helperPredicatesHonorThreshold()
    {
        EvaluationImageRecord record;
        record.gt.push_back(EvaluationGroundTruthData{1, 3, QStringLiteral("Cat"), {}, {}, {}, false});
        record.predictions.push_back(EvaluationPredictionData{QStringLiteral("p1"), 1, 3,
                                                                QStringLiteral("Cat"), 0.4, {}, {}, {}});
        record.predictions.push_back(EvaluationPredictionData{QStringLiteral("p2"), 1, 4,
                                                                QStringLiteral("Dog"), 0.8, {}, {}, {}});
        rebuildImageDerivedValues(record);
        QVERIFY(hasGroundTruth(record));
        QVERIFY(hasPredictions(record, 0.5));
        QCOMPARE(predClassIds(record, 0.5), QList<int>({4}));
        QCOMPARE(imageScore(record), 0.8);
        QVERIFY(!predClassIds(record, 0.9).contains(4));
    }

    void chartsReuseCompleteSearchAndRecomputeFilteredSearch()
    {
        EvaluationAggregateInput input;
        input.class_catalog = {{3, QStringLiteral("Cat")}};
        input.chart_descriptors = {
            QVariantMap{{evaluation::fieldName(evaluation::Field::ChartId),
                         evaluation::chartIdKey(evaluation::ChartId::PrecisionRecall)},
                        {evaluation::fieldName(evaluation::Field::FilterKind),
                         evaluation::filterKindKey(evaluation::FilterKind::PrecisionRecall)}},
            QVariantMap{{evaluation::fieldName(evaluation::Field::ChartId),
                         evaluation::chartIdKey(evaluation::ChartId::ConfidenceDistribution)}}};

        EvaluationImageRecord image;
        image.id = 1;
        image.gt.push_back(EvaluationGroundTruthData{1, 3, QStringLiteral("Cat"),
                                                     QVariantMap{{QStringLiteral("x"), 0.0},
                                                                 {QStringLiteral("y"), 0.0},
                                                                 {QStringLiteral("width"), 10.0},
                                                                 {QStringLiteral("height"), 10.0}},
                                                     {}, EvaluationBox{0, 0, 10, 10}, false});
        image.predictions.push_back(EvaluationPredictionData{QStringLiteral("prediction"), 1, 3,
                                                              QStringLiteral("Cat"), 0.8,
                                                              QVariantMap{{QStringLiteral("x"), 0.0},
                                                                          {QStringLiteral("y"), 0.0},
                                                                          {QStringLiteral("width"), 10.0},
                                                                          {QStringLiteral("height"), 10.0}},
                                                              {}, EvaluationBox{0, 0, 10, 10}});
        rebuildImageDerivedValues(image);
        input.images = {image};
        // This is the complete evaluation result.  The aggregate receives a
        // filtered image set and must derive its own best threshold from it.
        input.threshold_search.available = true;
        input.threshold_search.points = {EvaluationThresholdPoint{0.9, {}, 0.0, 0.0, 0.0},
                                         EvaluationThresholdPoint{0.95, {}, 0.0, 0.0, 0.0}};
        input.threshold_search.best_point.threshold = 0.95;

        const EvaluationAggregateOutput output = aggregateEvaluation(input);
        QCOMPARE(output.charts.size(), 2);
        for (const QVariantMap &chart : output.charts)
        {
            const QVariantList datasets = chart.value(evaluation::fieldName(evaluation::Field::Data))
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
                    QCOMPARE(dataset.value(evaluation::fieldName(evaluation::Field::Threshold)).toDouble(), 0.8);
                }
            }
            QVERIFY(found_best_threshold);
        }

        input.threshold_search_is_complete = true;
        const EvaluationAggregateOutput complete_output = aggregateEvaluation(input);
        QCOMPARE(complete_output.charts.size(), 2);
        for (const QVariantMap &chart : complete_output.charts)
        {
            const QVariantList datasets = chart.value(evaluation::fieldName(evaluation::Field::Data))
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
                    QCOMPARE(dataset.value(evaluation::fieldName(evaluation::Field::Threshold)).toDouble(), 0.95);
                }
            }
            QVERIFY(found_best_threshold);
        }
    }

    void anomalyChartsKeepNormalAndAnomalyDistributionsAfterFiltering()
    {
        EvaluationAggregateInput input;
        input.anomaly_detection = true;
        input.chart_descriptors = {
            QVariantMap{{evaluation::fieldName(evaluation::Field::ChartId),
                         evaluation::chartIdKey(evaluation::ChartId::AnomalyScoreDistribution)},
                        {evaluation::fieldName(evaluation::Field::FilterKind),
                         evaluation::filterKindKey(evaluation::FilterKind::ImageScore)}},
            QVariantMap{{evaluation::fieldName(evaluation::Field::ChartId),
                         evaluation::chartIdKey(evaluation::ChartId::PrecisionRecall)},
                        {evaluation::fieldName(evaluation::Field::FilterKind),
                         evaluation::filterKindKey(evaluation::FilterKind::PrecisionRecall)}},
            QVariantMap{{evaluation::fieldName(evaluation::Field::ChartId),
                         evaluation::chartIdKey(evaluation::ChartId::ConfidenceDistribution)}}};

        EvaluationImageRecord normal;
        normal.id = 1;
        normal.anomaly_score_map = std::make_shared<const EvaluationScoreMap>(EvaluationScoreMap{1, 1, {0.2}});
        EvaluationImageRecord anomaly;
        anomaly.id = 2;
        anomaly.gt.push_back(EvaluationGroundTruthData{1, 1, QStringLiteral("Anomaly"), {}, {}, {}, true});
        anomaly.anomaly_score_map = std::make_shared<const EvaluationScoreMap>(EvaluationScoreMap{1, 1, {0.9}});
        input.images = {normal, anomaly};
        input.threshold_search.available = true;
        input.threshold_search.points = {EvaluationThresholdPoint{0.8, {}, 0.0, 0.0, 0.0},
                                         EvaluationThresholdPoint{0.95, {}, 0.0, 0.0, 0.0}};
        input.threshold_search.best_point.threshold = 0.95;
        const EvaluationAggregateOutput output = aggregateEvaluation(input);
        QCOMPARE(output.charts.size(), 2);
        QCOMPARE(output.charts.at(0).value(evaluation::fieldName(evaluation::Field::ChartId)).toString(),
                 evaluation::chartIdKey(evaluation::ChartId::PrecisionRecall));
        QCOMPARE(output.charts.at(1).value(evaluation::fieldName(evaluation::Field::ChartId)).toString(),
                 evaluation::chartIdKey(evaluation::ChartId::AnomalyScoreDistribution));

        const QVariantList datasets = output.charts.at(1)
                                           .value(evaluation::fieldName(evaluation::Field::Data))
                                           .toMap()
                                           .value(evaluation::fieldName(evaluation::Field::Datasets))
                                           .toList();
        QCOMPARE(datasets.size(), 5);
        int distribution_count = 0;
        QCOMPARE(evaluation::seriesKindFromKey(
                     datasets.at(0).toMap().value(evaluation::fieldName(evaluation::Field::SeriesKind)).toString()),
                 evaluation::SeriesKind::Good);
        ++distribution_count;
        QCOMPARE(evaluation::seriesKindFromKey(
                     datasets.at(1).toMap().value(evaluation::fieldName(evaluation::Field::SeriesKind)).toString()),
                 evaluation::SeriesKind::Anomaly);
        ++distribution_count;
        bool has_good_max = false;
        bool has_anomaly_min = false;
        bool has_best = false;
        for (const QVariant &value : datasets)
        {
            const QVariantMap dataset = value.toMap();
            const auto         kind = evaluation::seriesKindFromKey(
                dataset.value(evaluation::fieldName(evaluation::Field::SeriesKind)).toString());
            QVERIFY(kind != evaluation::SeriesKind::Overall);
            if (kind == evaluation::SeriesKind::BestThreshold)
            {
                const QString label = dataset.value(evaluation::fieldName(evaluation::Field::Label)).toString();
                has_good_max = has_good_max || label.startsWith(QStringLiteral("正常 最大分数："));
                has_anomaly_min = has_anomaly_min || label.startsWith(QStringLiteral("异常 最小分数："));
                if (label.startsWith(QStringLiteral("最佳阈值：")))
                {
                    has_best = true;
                    QCOMPARE(dataset.value(evaluation::fieldName(evaluation::Field::Threshold)).toDouble(), 0.9);
                }
            }
        }
        QVERIFY(has_good_max);
        QVERIFY(has_anomaly_min);
        QVERIFY(has_best);
        QCOMPARE(distribution_count, 2);

        const QVariantList pr_datasets = output.charts.at(0)
                                              .value(evaluation::fieldName(evaluation::Field::Data))
                                              .toMap()
                                              .value(evaluation::fieldName(evaluation::Field::Datasets))
                                              .toList();
        QCOMPARE(pr_datasets.size(), 2);
        QCOMPARE(evaluation::seriesKindFromKey(
                     pr_datasets.at(0).toMap().value(evaluation::fieldName(evaluation::Field::SeriesKind)).toString()),
                 evaluation::SeriesKind::Micro);
        QCOMPARE(evaluation::seriesKindFromKey(
                     pr_datasets.at(1).toMap().value(evaluation::fieldName(evaluation::Field::SeriesKind)).toString()),
                 evaluation::SeriesKind::BestThreshold);
    }
};

REGISTER_TEST(AggregateEvaluationTest)

#include "test_AggregateEvaluation.moc"
