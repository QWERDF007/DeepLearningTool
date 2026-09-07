#include "../test_runner.h"

#include "TestFixture.h"

#include "database/DataBase.h"
#include "model/DetectionEvaluationEngine.h"
#include "model/EvaluationEngineRegistry.h"
#include "model/ModelEvaluationOptions.h"
#include "model/ModelManager.h"
#include "model/ModelStorageService.h"
#include "model/ModelTestTaskManager.h"
#include "model/ModelTestTaskRepository.h"
#include "model/TaskManager.h"

#include <QThread>
#include <QTest>

#include <algorithm>
#include <atomic>
#include <memory>

using namespace dltool::model;
using namespace dltool::model::testsupport;

namespace {

class BlockingEvaluationEngine final : public DetectionEvaluationEngine
{
public:
    inline static std::atomic_bool first_entered{false};
    inline static std::atomic_bool cancel_block{false};
    inline static std::atomic_bool cancel_observed{false};
    inline static std::atomic_int  active{0};

    static void reset()
    {
        first_entered.store(false, std::memory_order_relaxed);
        cancel_block.store(false, std::memory_order_relaxed);
        cancel_observed.store(false, std::memory_order_relaxed);
        active.store(0, std::memory_order_relaxed);
    }

protected:
    bool computeInstanceCounts(const QMap<qint64, EvaluationImageData> &images, const QMap<int, QString> &classes,
                               QMap<int, EvaluationCounts> &per_class, EvaluationCounts &overall,
                               QString *err_msg) override
    {
        active.fetch_add(1, std::memory_order_relaxed);
        first_entered.store(true, std::memory_order_release);
        while (!cancel_block.load(std::memory_order_acquire)
               || !cancelled(scratch_.cancel_token))
            QThread::msleep(1);
        cancel_observed.store(true, std::memory_order_release);
        const bool result = DetectionEvaluationEngine::computeInstanceCounts(images, classes, per_class, overall,
                                                                               err_msg);
        active.fetch_sub(1, std::memory_order_relaxed);
        return result;
    }
};

struct RestoreDetectionEvaluationEngine
{
    ~RestoreDetectionEvaluationEngine()
    {
        EvaluationEngineRegistry::instance().registerEngine(
            evaluation::Method::Detection, []() { return std::make_unique<DetectionEvaluationEngine>(); });
    }
};

ModelEvaluationOptions evaluationOptionsFor(const EvaluationFixture &fixture)
{
    ModelEvaluationOptions options;
    options.model_uuid             = QStringLiteral("manager-model");
    options.test_task_uuid         = QStringLiteral("manager-task");
    options.method                 = evaluation::Method::Detection;
    options.project_database_path  = fixture.projectDatabasePath();
    options.dataset_file_list_path = fixture.fileListPath();
    options.task_database_path     = fixture.taskDatabasePath();
    options.prediction_dir         = fixture.predictionDirectory();
    options.confidence_threshold   = 0.5;
    options.iou_threshold          = 0.5;
    options.matching_strategy      = evaluation::MatchingStrategy::GreedyIoU;
    return options;
}

} // namespace

class ModelTestTaskManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void managesTaskLifecyclePersistenceAndEvaluationCache()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager model_manager(static_cast<int>(evaluation::Method::Detection), &database, nullptr);
        QString       error;
        const auto    record = model_manager.addModelRecord(QStringLiteral("Managed"), QStringLiteral("ultralytics"),
                                                             QStringLiteral("YOLOv8"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        TaskManager task_manager_instance;
        TaskManager *task_manager = &task_manager_instance;
        ModelTestTaskManager manager(fixture.rootPath(), &model_manager, nullptr, task_manager);
        manager.setModelUuid(record.uuid);

        QCOMPARE(manager.count(), 1);
        QCOMPARE(manager.currentIndex(), 0);
        QVERIFY(!manager.currentTaskUuid().isEmpty());
        QCOMPARE(manager.currentTaskName(), QStringLiteral("测试 1"));
        QVERIFY(manager.currentTestParams() != nullptr);
        QVERIFY(manager.currentEvaluation() != nullptr);
        const QString first_uuid = manager.currentTaskUuid();
        auto          *first_evaluation = manager.currentEvaluation();

        QVERIFY(manager.validateTaskName(QStringLiteral("Second")).isEmpty());
        QVERIFY(!manager.validateTaskName(QStringLiteral("测试 1")).isEmpty());
        const QString second_uuid = manager.createTask(QStringLiteral("Second"));
        QVERIFY(!second_uuid.isEmpty());
        QCOMPARE(manager.count(), 2);
        QCOMPARE(manager.currentTaskName(), QStringLiteral("Second"));
        auto *second_evaluation = manager.currentEvaluation();
        QVERIFY(second_evaluation != nullptr);
        QVERIFY(second_evaluation != first_evaluation);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.currentModelBusy(), 5000);
        QVERIFY(manager.switchTask(first_uuid));
        QCOMPARE(manager.currentTaskUuid(), first_uuid);
        QCOMPARE(manager.currentEvaluation(), first_evaluation);
        QVERIFY(manager.switchTask(second_uuid));
        QCOMPARE(manager.currentEvaluation(), second_evaluation);
        QVERIFY(manager.commitCurrentDatasetSelection());
        QVERIFY(manager.flush());

        const int task_id = task_manager->addTask(record.uuid, record.name, ModelTaskType::Test, first_uuid,
                                                  manager.currentTaskName());
        QVERIFY(task_id > 0);
        QCOMPARE(manager.taskId(first_uuid), task_id);
        QVERIFY(task_manager->startTask(task_id));
        QCOMPARE(manager.currentTaskRunning(), false);
        QVERIFY(!manager.switchTask(first_uuid));
        QCOMPARE(manager.currentTaskRunning(), false);
        QVERIFY(!manager.renameTask(first_uuid, QStringLiteral("Running rename")));
        QVERIFY(!manager.deleteTask(first_uuid));
        QVERIFY(task_manager->markTaskStopped(task_id));
        QVERIFY(!manager.currentTaskRunning());
        QVERIFY(manager.switchTask(first_uuid));
        QCOMPARE(manager.currentTaskRunning(), false);
        QVERIFY(manager.renameTask(first_uuid, QStringLiteral("Renamed")));
        QCOMPARE(manager.currentTaskName(), QStringLiteral("Renamed"));
        QVERIFY(manager.flush());

        ModelTestTaskRepository repository(fixture.rootPath());
        repository.setProjectDatabasePath(fixture.projectDatabasePath());
        const auto persisted = repository.listTasks(QStringLiteral("Managed"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(persisted.size(), 2);
        QVERIFY(std::any_of(persisted.cbegin(), persisted.cend(),
                            [](const ModelTestTaskDefinition &task) { return task.name == QStringLiteral("Renamed"); }));

        QVERIFY(manager.deleteTask(first_uuid));
        QCOMPARE(manager.count(), 1);
        QCOMPARE(manager.currentTaskUuid(), second_uuid);
        QVERIFY(manager.deleteTask(second_uuid));
        QCOMPARE(manager.count(), 1);
        QCOMPARE(manager.currentTaskName(), QStringLiteral("测试 1"));
        QVERIFY(manager.currentEvaluation() != nullptr);
        QVERIFY(manager.currentTestParams() != nullptr);
        task_manager->clearTasks();

        manager.setModelUuid({});
        QCOMPARE(manager.count(), 0);
        manager.setModelUuid(record.uuid);
        QCOMPARE(manager.count(), 1);
        QCOMPARE(manager.currentTaskName(), QStringLiteral("测试 1"));
    }

    void switchingModelStopsCachedEvaluationBeforeReturning()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 cat   = fixture.addClass(QStringLiteral("Cat"), QStringLiteral("normal"));
        const qint64 image = fixture.addImage(QStringLiteral("cat"));
        QVERIFY(cat >= 0);
        QVERIFY(image >= 0);
        QVERIFY(fixture.addDetectionLabel(image, cat, 0, 0, 10, 10) >= 0);
        QVERIFY(fixture.writeImageList());
        QVERIFY(fixture.setTestSelection({cat}));
        QVERIFY(fixture.writePrediction(
            image, detectionPrediction(static_cast<int>(cat), QStringLiteral("Cat"), 0.9, 0, 0, 10, 10)));

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager model_manager(static_cast<int>(evaluation::Method::Detection), &database, nullptr);
        QString       error;
        const auto    record = model_manager.addModelRecord(QStringLiteral("Managed"), QStringLiteral("ultralytics"),
                                                             QStringLiteral("YOLOv8"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));
        TaskManager task_manager;
        ModelTestTaskManager manager(fixture.rootPath(), &model_manager, nullptr, &task_manager);
        manager.setModelUuid(record.uuid);
        QVERIFY(manager.currentEvaluation() != nullptr);

        BlockingEvaluationEngine::reset();
        BlockingEvaluationEngine::cancel_block.store(false, std::memory_order_release);
        EvaluationEngineRegistry::instance().registerEngine(
            evaluation::Method::Detection, []() { return std::make_unique<BlockingEvaluationEngine>(); });
        RestoreDetectionEvaluationEngine restore_registration;
        manager.currentEvaluation()->setEvaluationOptions(evaluationOptionsFor(fixture));
        manager.currentEvaluation()->evaluate();
        QTRY_VERIFY_WITH_TIMEOUT(BlockingEvaluationEngine::first_entered.load(std::memory_order_acquire), 5000);

        BlockingEvaluationEngine::cancel_block.store(true, std::memory_order_release);
        manager.setModelUuid({});

        QVERIFY(BlockingEvaluationEngine::cancel_observed.load(std::memory_order_acquire));
        QCOMPARE(BlockingEvaluationEngine::active.load(std::memory_order_acquire), 0);
        QCOMPARE(manager.currentEvaluation(), nullptr);
        QCOMPARE(manager.count(), 0);
    }

};

REGISTER_TEST(ModelTestTaskManagerTest)

#include "test_ModelTestTaskManager.moc"
