#include "../test_runner.h"

#include "model/AnomalyEvaluationViewModel.h"
#include "model/DetectionEvaluationViewModel.h"
#include "model/EvaluationEngineRegistry.h"
#include "model/EvaluationResult.h"
#include "model/EvaluationViewModelRegistry.h"
#include "model/IEvaluationEngine.h"
#include "model/ModelEvaluationOptions.h"
#include "model/SegmentationEvaluationViewModel.h"

#include <QTest>

#include <atomic>
#include <memory>

using namespace dltool::model;

class EvaluationRegistriesTest : public QObject
{
    Q_OBJECT

private slots:
    void engineRegistryCreatesBuiltins()
    {
        const auto anomaly = EvaluationEngineRegistry::instance().createEngine(evaluation::Method::AnomalyDetection);
        QVERIFY(anomaly != nullptr);
        QCOMPARE(anomaly->method(), evaluation::Method::AnomalyDetection);

        const auto detection = EvaluationEngineRegistry::instance().createEngine(evaluation::Method::Detection);
        QVERIFY(detection != nullptr);
        QCOMPARE(detection->method(), evaluation::Method::Detection);

        const auto segmentation = EvaluationEngineRegistry::instance().createEngine(evaluation::Method::Segmentation);
        QVERIFY(segmentation != nullptr);
        QCOMPARE(segmentation->method(), evaluation::Method::Segmentation);

        QVERIFY(EvaluationEngineRegistry::instance().createEngine(evaluation::Method::Unknown) == nullptr);
    }

    void engineRejectsIncompleteOptions()
    {
        const auto engine = EvaluationEngineRegistry::instance().createEngine(evaluation::Method::Detection);
        QVERIFY(engine != nullptr);

        const ModelEvaluationOptions options;
        QString                     error;
        QVERIFY(!engine->evaluate(options, nullptr, &error));
        QVERIFY(!error.isEmpty());
    }

    void engineHonorsPreCancelledToken()
    {
        const auto engine = EvaluationEngineRegistry::instance().createEngine(evaluation::Method::AnomalyDetection);
        QVERIFY(engine != nullptr);

        ModelEvaluationOptions options;
        options.cancel_token = std::make_shared<std::atomic_bool>(true);
        QString error;
        QVERIFY(!engine->evaluate(options, nullptr, &error));
        QVERIFY(error.contains(QStringLiteral("取消")));
    }

    void viewModelRegistryCreatesSubclasses()
    {
        QObject scope;

        const auto anomaly = EvaluationViewModelRegistry::instance().createViewModel(
            evaluation::Method::AnomalyDetection, &scope);
        QVERIFY(qobject_cast<AnomalyEvaluationViewModel *>(anomaly) != nullptr);

        const auto detection = EvaluationViewModelRegistry::instance().createViewModel(
            evaluation::Method::Detection, &scope);
        QVERIFY(qobject_cast<DetectionEvaluationViewModel *>(detection) != nullptr);

        const auto segmentation = EvaluationViewModelRegistry::instance().createViewModel(
            evaluation::Method::Segmentation, &scope);
        QVERIFY(qobject_cast<SegmentationEvaluationViewModel *>(segmentation) != nullptr);

        QVERIFY(EvaluationViewModelRegistry::instance().createViewModel(evaluation::Method::Unknown, &scope)
                == nullptr);
    }

    void viewModelMethodPropertyTracksOptions()
    {
        QObject scope;
        const auto viewModel = EvaluationViewModelRegistry::instance().createViewModel(
            evaluation::Method::AnomalyDetection, &scope);
        QVERIFY(viewModel != nullptr);

        ModelEvaluationOptions options;
        options.method = evaluation::Method::AnomalyDetection;
        viewModel->setEvaluationOptions(options);
        QCOMPARE(viewModel->method(), static_cast<int>(evaluation::Method::AnomalyDetection));
    }
};

REGISTER_TEST(EvaluationRegistriesTest)

#include "test_EvaluationRegistries.moc"
