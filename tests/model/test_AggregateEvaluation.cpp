#include "../test_runner.h"

#include "model/AggregateEvaluation.h"
#include "model/ModelEvaluationProtocol.h"

#include <QTest>

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
};

REGISTER_TEST(AggregateEvaluationTest)

#include "test_AggregateEvaluation.moc"
