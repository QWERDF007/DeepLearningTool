#include "../test_runner.h"

#include "model/EvaluationResult.h"
#include "model/ModelEvaluationOptions.h"
#include "model/ModelEvaluationProtocol.h"

#include <QMetaType>
#include <QTest>

using namespace dltool::model;

class EvaluationResultTest : public QObject
{
    Q_OBJECT

private slots:
    void metatypesAreRegistered()
    {
        QVERIFY(QMetaType::fromType<EvaluationResult>().isValid());
        QVERIFY(QMetaType::fromType<std::shared_ptr<EvaluationResult>>().isValid());
    }

    void typedResultStoresEvaluationOutput()
    {
        EvaluationResult result;
        result.official_metrics = QVariantMap{
            {evaluation::fieldName(evaluation::Field::Available), true},
            {evaluation::fieldName(evaluation::Field::Instance), QVariantMap{}}
        };
        result.image_metric_definition = QVariantMap{
            {evaluation::fieldName(evaluation::Field::SampleUnit), QStringLiteral("image")}
        };
        result.threshold_search.available = true;

        QVERIFY(result.official_metrics.value(evaluation::fieldName(evaluation::Field::Available)).toBool());
        QCOMPARE(result.image_metric_definition.value(evaluation::fieldName(evaluation::Field::SampleUnit)).toString(),
                 QStringLiteral("image"));
        QVERIFY(result.threshold_search.available);
    }

    void protocolKeysSingletonMapsAxisIds()
    {
        const auto keys = evaluation::EvaluationProtocolKeys::create(nullptr, nullptr);
        QVERIFY(keys != nullptr);
        QCOMPARE(keys->chartAxisIdKey(static_cast<int>(evaluation::ChartAxisId::ScoreAxis)),
                 QStringLiteral("score-axis"));
        QCOMPARE(keys->chartAxisIdKey(static_cast<int>(evaluation::ChartAxisId::CountAxis)),
                 QStringLiteral("count-axis"));
        QCOMPARE(keys->chartIdKey(static_cast<int>(evaluation::ChartId::PrecisionRecall)),
                 QStringLiteral("precision_recall"));
    }

    void optionsHaveProtocolDefaults()
    {
        const ModelEvaluationOptions options;
        QCOMPARE(options.confidence_threshold, evaluation::kDefaultConfidenceThreshold);
        QCOMPARE(options.iou_threshold, evaluation::kDefaultIouThreshold);
        QCOMPARE(options.matching_strategy, evaluation::MatchingStrategy::GreedyIoU);
    }
};

REGISTER_TEST(EvaluationResultTest)

#include "test_EvaluationResult.moc"
