#include "../test_runner.h"

#include "TestFixture.h"

#include "database/DataBase.h"
#include "database/ModelTaskDataBase.h"
#include "model/DetectionEvaluationEngine.h"
#include "model/EvaluationEngineRegistry.h"
#include "model/IParams.h"
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

ParamGroupModel *findGroup(const ITestParams *params, const QString &name)
{
    if (params == nullptr)
        return nullptr;
    for (QObject *object : params->groupObjects())
    {
        auto *group = qobject_cast<ParamGroupModel *>(object);
        if (group != nullptr && group->nameEn() == name)
            return group;
    }
    return nullptr;
}

class BlockingEvaluationEngine final : public DetectionEvaluationEngine
{
public:
    inline static std::atomic_bool first_entered{false};
    inline static std::atomic_bool cancel_block{false};
    inline static std::atomic_bool cancel_observed{false};
    inline static std::atomic_int  active{0};
    inline static std::atomic_int  entered_count{0};
    inline static std::atomic_int  cancel_observed_count{0};

    static void reset()
    {
        first_entered.store(false, std::memory_order_relaxed);
        cancel_block.store(false, std::memory_order_relaxed);
        cancel_observed.store(false, std::memory_order_relaxed);
        active.store(0, std::memory_order_relaxed);
        entered_count.store(0, std::memory_order_relaxed);
        cancel_observed_count.store(0, std::memory_order_relaxed);
    }

protected:
    bool computeInstanceCounts(const QMap<qint64, EvaluationImageData> &images, const QMap<int, QString> &classes,
                               QMap<int, EvaluationCounts> &per_class, EvaluationCounts &overall,
                               QString *err_msg) override
    {
        active.fetch_add(1, std::memory_order_relaxed);
        entered_count.fetch_add(1, std::memory_order_release);
        first_entered.store(true, std::memory_order_release);
        while (!cancel_block.load(std::memory_order_acquire)
               || !cancelled(scratch_.cancel_token))
            QThread::msleep(1);
        cancel_observed.store(true, std::memory_order_release);
        if (cancelled(scratch_.cancel_token))
            cancel_observed_count.fetch_add(1, std::memory_order_release);
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

    void shutdownWithMultipleControlledEvaluationsCancelsAllExecutorsBeforeWaiting()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 cat   = fixture.addClass(QStringLiteral("Cat"), QStringLiteral("normal"));
        const qint64 image = fixture.addImage(QStringLiteral("cat"));
        QVERIFY(cat >= 0 && image >= 0);
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
        QCOMPARE(manager.count(), 1);
        auto *first_evaluation = manager.currentEvaluation();
        QVERIFY(first_evaluation != nullptr);
        const QString first_uuid = manager.currentTaskUuid();

        const QString second_uuid = manager.createTask(QStringLiteral("Task 2"));
        QVERIFY(!second_uuid.isEmpty());
        QCOMPARE(manager.count(), 2);
        auto *second_evaluation = manager.currentEvaluation();
        QVERIFY(second_evaluation != nullptr);
        QVERIFY(second_evaluation != first_evaluation);

        BlockingEvaluationEngine::reset();
        BlockingEvaluationEngine::cancel_block.store(true, std::memory_order_release);
        EvaluationEngineRegistry::instance().registerEngine(
            evaluation::Method::Detection, []() { return std::make_unique<BlockingEvaluationEngine>(); });
        RestoreDetectionEvaluationEngine restore_registration;

        first_evaluation->setEvaluationOptions(evaluationOptionsFor(fixture));
        second_evaluation->setEvaluationOptions(evaluationOptionsFor(fixture));

        first_evaluation->evaluate();
        second_evaluation->evaluate();

        QTRY_VERIFY_WITH_TIMEOUT(BlockingEvaluationEngine::entered_count.load(std::memory_order_acquire) == 2, 5000);
        QCOMPARE(BlockingEvaluationEngine::active.load(std::memory_order_acquire), 2);

        QElapsedTimer shutdown_timer;
        shutdown_timer.start();
        manager.shutdown();

        QVERIFY2(shutdown_timer.elapsed() < 3000, "关闭超时，执行者未在等待前全部收到取消导致死锁或超时");
        QCOMPARE(BlockingEvaluationEngine::cancel_observed_count.load(std::memory_order_acquire), 2);
        QCOMPARE(BlockingEvaluationEngine::active.load(std::memory_order_acquire), 0);
        QVERIFY(!first_evaluation->available());
        QVERIFY(!second_evaluation->available());
    }

bool prepareEvaluationInputs(EvaluationFixture &fixture, const ModelTestTaskManager &manager,
                             const ModelManager::ModelRecordView &record, const qint64 image_id,
                             const QVariant &prediction, const bool write_prediction, QString *error)
{
    const ModelStorageService storage(fixture.rootPath());
    const QString             file_list_path
        = storage.testTaskFileListPath(record.name, manager.currentTaskDirectory());
    const QString task_database_path = storage.testTaskDatabasePath(record.name, manager.currentTaskDirectory());
    if (file_list_path.isEmpty() || task_database_path.isEmpty())
    {
        if (error != nullptr)
            *error = QStringLiteral("测试任务存储路径为空");
        return false;
    }
    if (QFileInfo::exists(file_list_path) && !QFile::remove(file_list_path))
    {
        if (error != nullptr)
            *error = QString("删除旧测试图像列表失败: %1").arg(file_list_path);
        return false;
    }
    if (!QFile::copy(fixture.fileListPath(), file_list_path))
    {
        if (error != nullptr)
            *error = QString("复制测试图像列表失败: %1").arg(file_list_path);
        return false;
    }

    dltool::database::ModelTaskDataBase task_database(task_database_path);
    if (!task_database.replaceDatasets({
            {QStringLiteral("test"), fixture.datasetId(), fixture.classIds()}
    }, error))
        return false;
    if (write_prediction && !task_database.upsertPrediction({image_id, prediction}, error))
        return false;
    return true;
}

    void reopenReevalReinferMaintainsFirstEvaluationAppliedOnlyOnce()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 cat   = fixture.addClass(QStringLiteral("Cat"), QStringLiteral("normal"));
        const qint64 image = fixture.addImage(QStringLiteral("cat"));
        QVERIFY(cat >= 0 && image >= 0);
        QVERIFY(fixture.addDetectionLabel(image, cat, 0, 0, 10, 10) >= 0);
        QVERIFY(fixture.writeImageList());
        QVERIFY(fixture.setTestSelection({cat}));

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager model_manager(static_cast<int>(evaluation::Method::Detection), &database, nullptr);
        QString error;
        const auto record = model_manager.addModelRecord(QStringLiteral("Managed"), QStringLiteral("ultralytics"),
                                                         QStringLiteral("YOLOv8"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));
        TaskManager task_manager;
        ModelTestTaskManager manager(fixture.rootPath(), &model_manager, nullptr, &task_manager);
        manager.setModelUuid(record.uuid);

        QVERIFY2(prepareEvaluationInputs(fixture, manager, record, image,
                                          detectionPrediction(static_cast<int>(cat), QStringLiteral("Cat"), 0.9, 0, 0, 10, 10),
                                          true, &error),
                  qPrintable(error));

        auto *evaluation = manager.currentEvaluation();
        QVERIFY(evaluation != nullptr);
        evaluation->evaluate(false);
        QTRY_COMPARE_WITH_TIMEOUT(evaluation->stateKind(), ModelEvaluationViewModel::Ready, 5000);
        QVERIFY(evaluation->available());

        // 首次评估自动应用最优阈值 0.9
        QCOMPARE(evaluation->confidenceThreshold(), 0.9);

        // 验收条件 1 & 3: 验证持久化在 task.db，且 extra_data 无 test_tasks
        const ModelStorageService storage(fixture.rootPath());
        const QString task_db_path = storage.testTaskDatabasePath(record.name, manager.currentTaskDirectory());
        dltool::database::ModelTaskDataBase task_database(task_db_path);
        bool applied = false;
        QVERIFY(task_database.readAdaptiveThresholdApplied(applied));
        QVERIFY(applied);

        const QVariantMap extra_data = model_manager.modelRecordForUuid(record.uuid).value(QStringLiteral("extra_data")).toMap();
        QVERIFY(!extra_data.contains(QStringLiteral("test_tasks")));

        // 用户手动调整阈值为 0.6
        auto *evaluation_params = findGroup(manager.currentTestParams(), QStringLiteral("evaluation"));
        QVERIFY(evaluation_params != nullptr);
        QVERIFY(evaluation_params->setValueForName(QStringLiteral("conf"), 0.6));
        manager.saveCurrentTask();

        // 1. 重评估保持手动选择
        evaluation->evaluate(false);
        QTRY_COMPARE_WITH_TIMEOUT(evaluation->stateKind(), ModelEvaluationViewModel::Ready, 5000);
        QCOMPARE(evaluation->confidenceThreshold(), 0.6);
        QCOMPARE(evaluation->bestThreshold(), 0.9);

        // 2. 模拟重开项目后保持首评标记与用户选择
        manager.shutdown();
        task_manager.clearTasks();

        ModelManager reopened_model_manager(static_cast<int>(evaluation::Method::Detection), &database, nullptr);
        TaskManager  reopened_task_manager;
        ModelTestTaskManager reopened_manager(fixture.rootPath(), &reopened_model_manager, nullptr, &reopened_task_manager);
        reopened_manager.setModelUuid(record.uuid);
        auto *reopened_eval = reopened_manager.currentEvaluation();
        QVERIFY(reopened_eval != nullptr);
        reopened_eval->evaluate(false);
        QTRY_COMPARE_WITH_TIMEOUT(reopened_eval->stateKind(), ModelEvaluationViewModel::Ready, 5000);
        QCOMPARE(reopened_eval->confidenceThreshold(), 0.6);

        // 3. 重新推理后评估保持首评标记与用户选择
        QVERIFY(task_database.upsertPrediction({image, detectionPrediction(static_cast<int>(cat), QStringLiteral("Cat"), 0.85, 0, 0, 10, 10)}));
        reopened_eval->evaluate(false);
        QTRY_COMPARE_WITH_TIMEOUT(reopened_eval->stateKind(), ModelEvaluationViewModel::Ready, 5000);
        QCOMPARE(reopened_eval->confidenceThreshold(), 0.6);

        bool reopened_applied = false;
        QVERIFY(task_database.readAdaptiveThresholdApplied(reopened_applied));
        QVERIFY(reopened_applied);
    }

    void taskDbWriteFailureDoesNotReportSuccess()
    {
        QTemporaryDir temp_dir;
        QVERIFY(temp_dir.isValid());
        const QString read_only_db_path = QDir(temp_dir.path()).filePath(QStringLiteral("task.db"));
        {
            dltool::database::ModelTaskDataBase temp_db(read_only_db_path);
            dltool::database::TaskInfoRecord info{QStringLiteral("task1"), 100, 100};
            QVERIFY(temp_db.upsertTaskInfo(info));
        }

        // 设置为只读权限
        QVERIFY(QFile::setPermissions(read_only_db_path, QFileDevice::ReadOwner | QFileDevice::ReadUser));

        dltool::database::ModelTaskDataBase ro_db(read_only_db_path);
        QString write_error;
        const bool write_res = ro_db.writeAdaptiveThresholdApplied(true, &write_error);
        QVERIFY(!write_res);
        QVERIFY(!write_error.isEmpty());

        QVariantMap state;
        state.insert(QStringLiteral("status"), QStringLiteral("running"));
        const bool state_res = ro_db.writeExecutionState(state, &write_error);
        QVERIFY(!state_res);
        QVERIFY(!write_error.isEmpty());

        // 恢复写权限以确保临时目录正常释放
        QFile::setPermissions(read_only_db_path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
};

REGISTER_TEST(ModelTestTaskManagerTest)

#include "test_ModelTestTaskManager.moc"
