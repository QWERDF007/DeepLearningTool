#include "../test_runner.h"

#include "TestFixture.h"

#include "database/DataBase.h"
#include "model/ModelManager.h"
#include "model/ModelStorageService.h"
#include "model/ModelTestTaskManager.h"
#include "model/ModelTestTaskRepository.h"
#include "model/TaskManager.h"

#include <QTest>

#include <algorithm>

using namespace dltool::model;
using namespace dltool::model::testsupport;

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

        TaskManager *task_manager = TaskManager::getInstance();
        task_manager->clearTasks();
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
                                                  manager.currentTaskName(), true);
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

};

REGISTER_TEST(ModelTestTaskManagerTest)

#include "test_ModelTestTaskManager.moc"
