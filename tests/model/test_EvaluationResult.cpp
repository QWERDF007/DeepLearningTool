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

    void protocolBridgeProducesExpectedFields()
    {
        const EvaluationResult result;
        QString                error;
        const QVariantMap      map = evaluationResultToProtocolMap(result, &error);
        QVERIFY(!map.isEmpty());
        QVERIFY(map.contains(evaluation::fieldName(evaluation::Field::PrimaryMetricSet)));
        QVERIFY(map.contains(evaluation::fieldName(evaluation::Field::Capabilities)));
        QVERIFY(map.contains(evaluation::fieldName(evaluation::Field::Charts)));
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
