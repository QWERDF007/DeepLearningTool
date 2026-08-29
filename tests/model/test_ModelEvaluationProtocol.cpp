#include "../test_runner.h"

#include "model/ModelEvaluationProtocol.h"

#include <QTest>

using namespace dltool::model;

class ModelEvaluationProtocolTest : public QObject
{
    Q_OBJECT

private slots:
    void chartKindRoundTrip()
    {
        QCOMPARE(evaluation::chartKindFromKey(evaluation::chartKindKey(evaluation::ChartKind::Line)),
                 evaluation::ChartKind::Line);
        QCOMPARE(evaluation::chartKindFromKey(evaluation::chartKindKey(evaluation::ChartKind::Bar)),
                 evaluation::ChartKind::Bar);
        QCOMPARE(evaluation::chartKindFromKey(evaluation::chartKindKey(evaluation::ChartKind::Pie)),
                 evaluation::ChartKind::Pie);
    }

    void chartIdRoundTrip()
    {
        QCOMPARE(evaluation::chartIdFromKey(evaluation::chartIdKey(evaluation::ChartId::AnomalyScoreDistribution)),
                 evaluation::ChartId::AnomalyScoreDistribution);
        QCOMPARE(evaluation::chartIdFromKey(evaluation::chartIdKey(evaluation::ChartId::PrecisionRecall)),
                 evaluation::ChartId::PrecisionRecall);
        QCOMPARE(evaluation::chartIdFromKey(evaluation::chartIdKey(evaluation::ChartId::PerClassMetrics)),
                 evaluation::ChartId::PerClassMetrics);
    }

    void seriesAndFilterRoundTrip()
    {
        for (const evaluation::SeriesKind kind : {evaluation::SeriesKind::Good, evaluation::SeriesKind::Anomaly,
                                                  evaluation::SeriesKind::Micro, evaluation::SeriesKind::Class,
                                                  evaluation::SeriesKind::Overall,
                                                  evaluation::SeriesKind::BestThreshold})
            QCOMPARE(evaluation::seriesKindFromKey(evaluation::seriesKindKey(kind)), kind);

        for (const evaluation::FilterKind kind : {evaluation::FilterKind::ImageScore,
                                                  evaluation::FilterKind::PrecisionRecall,
                                                  evaluation::FilterKind::PerClassMetrics})
            QCOMPARE(evaluation::filterKindFromKey(evaluation::filterKindKey(kind)), kind);
    }

    void chartAxisIdRoundTrip()
    {
        QCOMPARE(evaluation::chartAxisIdFromKey(evaluation::chartAxisIdKey(evaluation::ChartAxisId::ScoreAxis)),
                 evaluation::ChartAxisId::ScoreAxis);
        QCOMPARE(evaluation::chartAxisIdFromKey(evaluation::chartAxisIdKey(evaluation::ChartAxisId::CountAxis)),
                 evaluation::ChartAxisId::CountAxis);
    }

    void matrixAxisKeys()
    {
        QCOMPARE(evaluation::matrixAxisKey(evaluation::MatrixAxisKey::Total), QStringLiteral("TOTAL"));
        QCOMPARE(evaluation::matrixAxisKey(evaluation::MatrixAxisKey::FalseNegative), QStringLiteral("FN"));
        QCOMPARE(evaluation::matrixAxisKey(evaluation::MatrixAxisKey::FalsePositive), QStringLiteral("FP"));
        QCOMPARE(evaluation::matrixAxisKey(evaluation::MatrixAxisKey::UnmatchedGroundTruth),
                 QStringLiteral("UNMATCHED_GT"));
        QCOMPARE(evaluation::matrixAxisKey(evaluation::MatrixAxisKey::UnmatchedPrediction),
                 QStringLiteral("UNMATCHED_PRED"));
    }

    void methodCapabilities()
    {
        QVERIFY(evaluation::isAnomaly(evaluation::Method::AnomalyDetection));
        QVERIFY(!evaluation::isAnomaly(evaluation::Method::Detection));
        QVERIFY(evaluation::hasInstanceMetrics(evaluation::Method::Detection));
        QVERIFY(evaluation::hasInstanceMetrics(evaluation::Method::Segmentation));
        QVERIFY(!evaluation::hasInstanceMetrics(evaluation::Method::AnomalyDetection));
        QVERIFY(evaluation::hasImageMetrics(evaluation::Method::AnomalyDetection));
        QVERIFY(evaluation::hasConfusionMatrix(evaluation::Method::AnomalyDetection));
    }

    void normalizedEvaluationConfigFillsDefaults()
    {
        const QVariantMap normalized = evaluation::normalizedEvaluationConfig({});
        QCOMPARE(normalized.value(evaluation::fieldName(evaluation::Field::ConfidenceThreshold)).toDouble(),
                 evaluation::kDefaultConfidenceThreshold);
        QCOMPARE(normalized.value(evaluation::fieldName(evaluation::Field::IouThreshold)).toDouble(),
                 evaluation::kDefaultIouThreshold);
        QCOMPARE(normalized.value(evaluation::fieldName(evaluation::Field::MatchingStrategy)).toString(),
                 evaluation::matchingStrategyKey(evaluation::MatchingStrategy::GreedyIoU));
    }

    void normalizedEvaluationConfigPreservesCustomValues()
    {
        QVariantMap source;
        source.insert(QStringLiteral("conf"), 0.7);
        source.insert(QStringLiteral("iou"), 0.4);
        source.insert(evaluation::fieldName(evaluation::Field::MatchingStrategy),
                      evaluation::matchingStrategyKey(evaluation::MatchingStrategy::HungarianIoU));
        const QVariantMap normalized = evaluation::normalizedEvaluationConfig(source);
        QCOMPARE(normalized.value(evaluation::fieldName(evaluation::Field::ConfidenceThreshold)).toDouble(), 0.7);
        QCOMPARE(normalized.value(evaluation::fieldName(evaluation::Field::IouThreshold)).toDouble(), 0.4);
        QCOMPARE(normalized.value(evaluation::fieldName(evaluation::Field::MatchingStrategy)).toString(),
                 evaluation::matchingStrategyKey(evaluation::MatchingStrategy::HungarianIoU));
    }

    void normalizedEvaluationConfigMapsAnomalyClassificationThreshold()
    {
        const QVariantMap normalized
            = evaluation::normalizedEvaluationConfig({{QStringLiteral("classification_threshold"), 0.8}});
        QCOMPARE(normalized.value(evaluation::fieldName(evaluation::Field::ConfidenceThreshold)).toDouble(), 0.8);
        QCOMPARE(normalized.value(evaluation::fieldName(evaluation::Field::IouThreshold)).toDouble(),
                 evaluation::kDefaultIouThreshold);
    }
};

REGISTER_TEST(ModelEvaluationProtocolTest)

#include "test_ModelEvaluationProtocol.moc"
