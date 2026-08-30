#include "../test_runner.h"
#include "TestFixture.h"
#include "model/AnomalyEvaluationViewModel.h"
#include "model/DetectionEvaluationEngine.h"
#include "model/DetectionEvaluationViewModel.h"
#include "model/EvaluationEngineRegistry.h"
#include "model/ModelEvaluationOptions.h"
#include "model/ModelEvaluationProtocol.h"

#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QThread>
#include <QTest>

#include <atomic>
#include <memory>

using namespace dltool::model;
using namespace dltool::model::testsupport;

namespace {

ModelEvaluationOptions optionsFor(const EvaluationFixture &fixture, evaluation::Method method)
{
    ModelEvaluationOptions options;
    options.model_uuid             = QStringLiteral("view-model");
    options.test_task_uuid         = QStringLiteral("view-task");
    options.model_name             = QStringLiteral("View model");
    options.task_directory         = fixture.rootPath();
    options.method                 = method;
    options.project_database_path  = fixture.projectDatabasePath();
    options.dataset_file_list_path = fixture.fileListPath();
    options.task_database_path     = fixture.taskDatabasePath();
    options.prediction_dir         = fixture.predictionDirectory();
    options.confidence_threshold   = 0.5;
    options.iou_threshold          = 0.5;
    options.matching_strategy      = evaluation::MatchingStrategy::GreedyIoU;
    return options;
}

struct RestoreDetectionEvaluationEngine
{
    ~RestoreDetectionEvaluationEngine()
    {
        EvaluationEngineRegistry::instance().registerEngine(
            evaluation::Method::Detection, []() { return std::make_unique<DetectionEvaluationEngine>(); });
    }
};

class SerializedEvaluationProbeEngine final : public DetectionEvaluationEngine
{
public:
    inline static std::atomic_int active{0};
    inline static std::atomic_int max_active{0};
    inline static std::atomic_bool block_first{false};
    inline static std::atomic_bool first_entered{false};
    inline static std::atomic_bool release_first{false};
    inline static std::atomic_bool cancel_observed{false};
    inline static std::atomic_bool cancel_block{false};

    static void reset()
    {
        active.store(0, std::memory_order_relaxed);
        max_active.store(0, std::memory_order_relaxed);
        block_first.store(true, std::memory_order_relaxed);
        first_entered.store(false, std::memory_order_relaxed);
        release_first.store(false, std::memory_order_relaxed);
        cancel_observed.store(false, std::memory_order_relaxed);
        cancel_block.store(false, std::memory_order_relaxed);
    }

protected:
    bool computeInstanceCounts(const QMap<qint64, EvaluationImageData> &images, const QMap<int, QString> &classes,
                               QMap<int, EvaluationCounts> &per_class, EvaluationCounts &overall,
                               QString *err_msg) override
    {
        const int current = active.fetch_add(1, std::memory_order_relaxed) + 1;
        int       previous = max_active.load(std::memory_order_relaxed);
        while (current > previous
               && !max_active.compare_exchange_weak(previous, current, std::memory_order_relaxed))
        {
        }

        if (block_first.exchange(false, std::memory_order_relaxed))
        {
            first_entered.store(true, std::memory_order_release);
            while (!release_first.load(std::memory_order_acquire)
                   && (!cancel_block.load(std::memory_order_acquire) || !cancelled(scratch_.cancel_token)))
                QThread::msleep(1);
            if (cancel_block.load(std::memory_order_acquire) && cancelled(scratch_.cancel_token))
                cancel_observed.store(true, std::memory_order_release);
        }

        const bool result = DetectionEvaluationEngine::computeInstanceCounts(images, classes, per_class, overall,
                                                                               err_msg);
        active.fetch_sub(1, std::memory_order_relaxed);
        return result;
    }
};

} // namespace

class ModelEvaluationViewModelTest : public QObject
{
    Q_OBJECT

private slots:

    void noOptionsAndMissingInputUseExplicitStates()
    {
        DetectionEvaluationViewModel view_model;
        view_model.evaluate();
        QCOMPARE(view_model.stateKind(), ModelEvaluationViewModel::MissingResult);
        QVERIFY(!view_model.available());

        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        ModelEvaluationOptions options;
        options.method                 = evaluation::Method::Detection;
        options.dataset_file_list_path = QDir(temp.path()).filePath(QStringLiteral("missing.csv"));
        view_model.setEvaluationOptions(options);
        view_model.evaluate();
        QCOMPARE(view_model.stateKind(), ModelEvaluationViewModel::MissingResult);
        QVERIFY(view_model.error().isEmpty());
    }

    void detectionEvaluationLoadsModelsAndSupportsFiltersAndSelection()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 cat      = fixture.addClass(QStringLiteral("Cat"), QStringLiteral("normal"));
        const qint64 tp_image = fixture.addImage(QStringLiteral("tp"));
        const qint64 fn_image = fixture.addImage(QStringLiteral("fn"));
        QVERIFY(fixture.addDetectionLabel(tp_image, cat, 0, 0, 10, 10) >= 0);
        QVERIFY(fixture.addDetectionLabel(fn_image, cat, 0, 0, 10, 10) >= 0);
        QVERIFY(fixture.writeImageList());
        QVERIFY(fixture.setTestSelection({cat}));
        QVERIFY(fixture.writePrediction(
            tp_image, detectionPrediction(static_cast<int>(cat), QStringLiteral("Cat"), 0.9, 0, 0, 10, 10)));

        DetectionEvaluationViewModel view_model;
        view_model.setEvaluationOptions(optionsFor(fixture, evaluation::Method::Detection));
        QSignalSpy image_metric_resets(view_model.imageMetrics(), &QAbstractItemModel::modelReset);
        view_model.evaluate();
        QTRY_COMPARE_WITH_TIMEOUT(view_model.stateKind(), ModelEvaluationViewModel::Ready, 5000);
        QVERIFY(view_model.available());
        const int image_metric_resets_after_evaluation = image_metric_resets.count();
        QTest::qWait(200);
        QCOMPARE(image_metric_resets.count(), image_metric_resets_after_evaluation);
        QVERIFY(!view_model.loading());
        QCOMPARE(view_model.method(), static_cast<int>(evaluation::Method::Detection));
        QCOMPARE(view_model.hasInstanceMetrics(), true);
        QCOMPARE(view_model.hasImageMetrics(), true);
        QCOMPARE(view_model.hasConfusionMatrix(), true);
        QCOMPARE(view_model.instances()->rowCount(), 2);
        QVERIFY(view_model.confusionMatrix()->rowCount() > 0);
        QVERIFY(view_model.charts()->rowCount() > 0);

        const int all_rows = view_model.filteredInstances()->rowCount();
        QVERIFY(all_rows >= 2);
        view_model.setStatusFilter(evaluation::statusKey(evaluation::Status::TruePositive));
        QTRY_COMPARE_WITH_TIMEOUT(view_model.filteredInstances()->rowCount(), 1, 2000);
        const QString event_uuid
            = view_model.filteredInstances()->index(0, 0).data(EvaluationInstanceModel::EventUuidRole).toString();
        QVERIFY(!event_uuid.isEmpty());
        QVERIFY(view_model.selectInstance(event_uuid));
        QCOMPARE(view_model.selectedEventUuid(), event_uuid);
        QVERIFY(view_model.selectedInstanceRow() >= 0);
        QVERIFY(!view_model.selectedInstance().isEmpty());

        view_model.clearFilters();
        QTRY_COMPARE_WITH_TIMEOUT(view_model.filteredInstances()->rowCount(), all_rows, 2000);
        view_model.setDatasetFilter({fixture.datasetId()});
        QTRY_COMPARE_WITH_TIMEOUT(view_model.filteredInstances()->rowCount(), all_rows, 2000);
        view_model.selectMatrixCell(evaluation::matrixAxisKey(evaluation::MatrixAxisKey::FalseNegative),
                                    QString::number(cat));
        QTRY_COMPARE_WITH_TIMEOUT(view_model.filteredInstances()->rowCount(), 1, 2000);
        QVERIFY(!view_model.selectInstance(QStringLiteral("does-not-exist")));
        QCOMPARE(view_model.selectedInstanceRow(), -1);
    }

    void anomalySubclassReceivesMethodSpecificData()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::AnomalyDetection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 good    = fixture.addClass(QStringLiteral("Good"), QStringLiteral("good"));
        const qint64 anomaly = fixture.addClass(QStringLiteral("Scratch"), QStringLiteral("anomaly"));
        const qint64 normal
            = fixture.addImage(QStringLiteral("normal"), {
                                                             {QStringLiteral("image_label_class_id"),                   good},
                                                             {               QStringLiteral("group"), QStringLiteral("good")}
        });
        const qint64 bad
            = fixture.addImage(QStringLiteral("bad"), {
                                                          {QStringLiteral("image_label_class_id"),                   anomaly},
                                                          {               QStringLiteral("group"), QStringLiteral("anomaly")}
        });
        QVERIFY(fixture.writeImageList());
        QVERIFY(fixture.setTestSelection({good, anomaly}));
        QVERIFY(fixture.writePrediction(normal, anomalyPrediction(0.1)));
        QVERIFY(fixture.writePrediction(bad, anomalyPrediction(0.9)));

        AnomalyEvaluationViewModel view_model;
        view_model.setEvaluationOptions(optionsFor(fixture, evaluation::Method::AnomalyDetection));
        view_model.evaluate();
        QTRY_COMPARE_WITH_TIMEOUT(view_model.stateKind(), ModelEvaluationViewModel::Ready, 5000);
        QVERIFY(view_model.anomalyDetection());
        QVERIFY(!view_model.hasInstanceMetrics());
        QVERIFY(view_model.hasImageMetrics());
        QCOMPARE(view_model.classificationThreshold(), 0.5);
        QCOMPARE(view_model.images()->rowCount(), 2);
        QVERIFY(view_model.confusionMatrix()->rowCount() > 0);
    }

    void invalidatedEvaluationWaitsForActiveWorkerBeforeStartingNext()
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

        SerializedEvaluationProbeEngine::reset();
        EvaluationEngineRegistry::instance().registerEngine(
            evaluation::Method::Detection, []() { return std::make_unique<SerializedEvaluationProbeEngine>(); });
        RestoreDetectionEvaluationEngine restore_registration;

        DetectionEvaluationViewModel view_model;
        view_model.setEvaluationOptions(optionsFor(fixture, evaluation::Method::Detection));
        view_model.evaluate();
        QTRY_VERIFY_WITH_TIMEOUT(SerializedEvaluationProbeEngine::first_entered.load(std::memory_order_acquire),
                                 5000);

        view_model.invalidate();
        view_model.evaluate();
        QTest::qWait(100);
        QCOMPARE(SerializedEvaluationProbeEngine::active.load(std::memory_order_relaxed), 1);
        QCOMPARE(SerializedEvaluationProbeEngine::max_active.load(std::memory_order_relaxed), 1);

        SerializedEvaluationProbeEngine::release_first.store(true, std::memory_order_release);
        QTRY_COMPARE_WITH_TIMEOUT(view_model.stateKind(), ModelEvaluationViewModel::Ready, 5000);
        QCOMPARE(SerializedEvaluationProbeEngine::max_active.load(std::memory_order_relaxed), 1);
    }

    void shutdownCancelsActiveEvaluationBeforeReturning()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 cat = fixture.addClass(QStringLiteral("Cat"), QStringLiteral("normal"));
        const qint64 image = fixture.addImage(QStringLiteral("cat"));
        QVERIFY(fixture.addDetectionLabel(image, cat, 0, 0, 10, 10) >= 0);
        QVERIFY(fixture.writeImageList());
        QVERIFY(fixture.setTestSelection({cat}));
        QVERIFY(fixture.writePrediction(
            image, detectionPrediction(static_cast<int>(cat), QStringLiteral("Cat"), 0.9, 0, 0, 10, 10)));

        SerializedEvaluationProbeEngine::reset();
        SerializedEvaluationProbeEngine::cancel_block.store(true, std::memory_order_release);
        EvaluationEngineRegistry::instance().registerEngine(
            evaluation::Method::Detection, []() { return std::make_unique<SerializedEvaluationProbeEngine>(); });
        RestoreDetectionEvaluationEngine restore_registration;

        DetectionEvaluationViewModel view_model;
        view_model.setEvaluationOptions(optionsFor(fixture, evaluation::Method::Detection));
        view_model.evaluate();
        QTRY_VERIFY_WITH_TIMEOUT(SerializedEvaluationProbeEngine::first_entered.load(std::memory_order_acquire),
                                 5000);

        view_model.invalidate();
        QElapsedTimer timer;
        timer.start();
        ModelEvaluationViewModel::shutdownEvaluationWorkers();

        QVERIFY(SerializedEvaluationProbeEngine::cancel_observed.load(std::memory_order_acquire));
        QCOMPARE(SerializedEvaluationProbeEngine::active.load(std::memory_order_relaxed), 0);
        QVERIFY2(timer.elapsed() < 2000, "取消后的评估线程未及时收敛");
    }

    void engineFailureAndRuntimeStateClearOldResult()
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

        DetectionEvaluationViewModel view_model;
        auto                         options = optionsFor(fixture, evaluation::Method::Detection);
        view_model.setEvaluationOptions(options);
        view_model.evaluate();
        QTRY_COMPARE_WITH_TIMEOUT(view_model.stateKind(), ModelEvaluationViewModel::Ready, 5000);
        QVERIFY(view_model.available());

        view_model.setRuntimeState(evaluation::ViewState::Running);
        QCOMPARE(view_model.stateKind(), ModelEvaluationViewModel::Running);
        QVERIFY(!view_model.available());
        QCOMPARE(view_model.instances()->rowCount(), 0);

        options.project_database_path = QDir(fixture.rootPath()).filePath(QStringLiteral("missing-project.db"));
        view_model.setEvaluationOptions(options);
        view_model.evaluate();
        QTRY_COMPARE_WITH_TIMEOUT(view_model.stateKind(), ModelEvaluationViewModel::Error, 5000);
        QVERIFY(!view_model.error().isEmpty());
        QVERIFY(!view_model.available());
        QCOMPARE(view_model.instances()->rowCount(), 0);
    }

    void viewModelProvidesDatabaseClassColorsAndStrictClassCatalog()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::AnomalyDetection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 good = fixture.addClass(QStringLiteral("good"), QStringLiteral("good"), QStringLiteral("#112233"));
        const qint64 scratch
            = fixture.addClass(QStringLiteral("scratch"), QStringLiteral("anomaly"), QStringLiteral("#445566"));
        const qint64 normal
            = fixture.addImage(QStringLiteral("normal"), {
                                                             {QStringLiteral("image_label_class_id"),                   good},
                                                             {               QStringLiteral("group"), QStringLiteral("good")}
        });
        const qint64 bad
            = fixture.addImage(QStringLiteral("bad"), {
                                                          {QStringLiteral("image_label_class_id"),                   scratch},
                                                          {               QStringLiteral("group"), QStringLiteral("anomaly")}
        });
        QVERIFY(fixture.writeImageList());
        QVERIFY(fixture.setTestSelection({good, scratch}));
        QVERIFY(fixture.writePrediction(normal, anomalyPrediction(0.1)));
        QVERIFY(fixture.writePrediction(bad, anomalyPrediction(0.9)));

        AnomalyEvaluationViewModel view_model;
        view_model.setEvaluationOptions(optionsFor(fixture, evaluation::Method::AnomalyDetection));
        view_model.evaluate();
        QTRY_COMPARE_WITH_TIMEOUT(view_model.stateKind(), ModelEvaluationViewModel::Ready, 5000);
        QVERIFY(view_model.available());

        // 验证从数据库透传的颜色与接口
        QCOMPARE(view_model.classColor(static_cast<int>(good)), QStringLiteral("#112233"));
        QCOMPARE(view_model.classColor(static_cast<int>(scratch)), QStringLiteral("#445566"));
        // 未知类别回退到默认调色板
        QVERIFY(!view_model.classColor(9999).isEmpty());

        // 验证混淆矩阵列数严格遵循类别标签（good 与 scratch，加 FP 与 TOTAL），没有多余的 0 / 正常 列
        const auto *matrix = view_model.confusionMatrix();
        QVERIFY(matrix != nullptr);
        QCOMPARE(matrix->columnCount(), 4);
        for (int c = 0; c < matrix->columnCount(); ++c)
        {
            const QString col_key
                = matrix->data(matrix->index(0, c), EvaluationConfusionModel::ColumnKeyRole).toString();
            QVERIFY(col_key != QStringLiteral("0"));
        }
    }
};

REGISTER_TEST(ModelEvaluationViewModelTest)

#include "test_ModelEvaluationViewModel.moc"
