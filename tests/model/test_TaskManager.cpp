#include "../test_runner.h"

#include "model/TaskManager.h"
#include "model/TaskCommunication.h"

#include <QSignalSpy>
#include <QTest>

using namespace dltool::model;

class TaskManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void stateMachineNormalizesProgressAndProtectsTerminalStates()
    {
        TaskManager *manager = TaskManager::getInstance();
        manager->clearTasks();
        QSignalSpy start_requested(manager, &TaskManager::taskStartRequested);
        QSignalSpy stop_requested(manager, &TaskManager::taskStopRequested);

        QCOMPARE(manager->addTask({}, QStringLiteral("Model"), ModelTaskType::Test), -1);
        const int id = manager->addTask(QStringLiteral("model-1"), QStringLiteral("Model"), ModelTaskType::Test,
                                        QStringLiteral("scope-1"), QStringLiteral("Scope"), true);
        QVERIFY(id > 0);
        QCOMPARE(manager->findModelTask(QStringLiteral("model-1"), ModelTaskType::Test, QStringLiteral("scope-1")), id);
        QCOMPARE(manager->findTask(id)->status, TaskManager::Pending);
        QVERIFY(!manager->hasActiveModelTasks(QStringLiteral("model-1")));
        QVERIFY(manager->canStartTask(id));
        QVERIFY(manager->startTask(id));
        QCOMPARE(manager->findTask(id)->status, TaskManager::Preparing);
        QVERIFY(manager->hasActiveModelTasks(QStringLiteral("model-1")));
        QCOMPARE(start_requested.count(), 1);
        QVERIFY(!manager->startTask(id));

        QVERIFY(manager->markTaskRunning(id));
        QCOMPARE(manager->findTask(id)->status, TaskManager::Running);
        QVERIFY(manager->canPauseTask(id));
        QVERIFY(manager->pauseTask(id));
        QCOMPARE(manager->findTask(id)->status, TaskManager::Paused);
        QVERIFY(manager->updateTaskProgress(id, 200));
        QCOMPARE(manager->findTask(id)->progress, 100);
        QVERIFY(manager->updateTaskEta(id, -5));
        QCOMPARE(manager->findTask(id)->eta_seconds, qint64(-1));
        QVERIFY(manager->updateTaskPhase(id, QStringLiteral("evaluate")));
        QCOMPARE(manager->findTask(id)->phase, QStringLiteral("evaluate"));

        QVERIFY(manager->stopTask(id));
        QCOMPARE(manager->findTask(id)->status, TaskManager::Stopping);
        QCOMPARE(stop_requested.count(), 1);
        QVERIFY(manager->markTaskStopped(id));
        QCOMPARE(manager->findTask(id)->status, TaskManager::Stopped);
        QVERIFY(!manager->hasActiveModelTasks(QStringLiteral("model-1")));
        QVERIFY(TaskManager::isTerminal(TaskManager::Stopped));
        QVERIFY(!manager->markTaskRunning(id));
        QVERIFY(!manager->stopTask(id));
        QVERIFY(manager->canDeleteTask(id));
        QVERIFY(manager->deleteTask(id));
        QCOMPARE(manager->count(), 0);
    }

    void communicationEventsUpdateStatusAndRoles()
    {
        TaskManager *manager = TaskManager::getInstance();
        manager->clearTasks();
        const int id = manager->addTask(QStringLiteral("model-2"), QStringLiteral("Model 2"), ModelTaskType::Train,
                                        false);
        QVERIFY(id > 0);
        QVERIFY(manager->startTask(id));

        TaskMessage message;
        message.task_id = id;
        message.type = TaskMessageType::Progress;
        message.status = TaskProtocolStatus::Running;
        message.progress = 37;
        message.eta_seconds = 12;
        message.payload = {{QStringLiteral("phase"), QStringLiteral("training")},
                           {taskProtocolFieldName(TaskProtocolField::EtaSeconds), 12}};
        QMetaObject::invokeMethod(manager, "handleTaskMessage", Qt::DirectConnection, Q_ARG(TaskMessage, message));
        const auto *task = manager->findTask(id);
        QVERIFY(task != nullptr);
        QCOMPARE(task->status, TaskManager::Running);
        QCOMPARE(task->progress, 37);
        QCOMPARE(task->eta_seconds, qint64(12));
        QCOMPARE(task->phase, QStringLiteral("training"));
        const QModelIndex index = manager->index(0, 0);
        QCOMPARE(index.data(TaskManager::StatusValueRole).toInt(), static_cast<int>(TaskManager::Running));
        QVERIFY(index.data(TaskManager::CanStopRole).toBool());
        QVERIFY(!index.data(TaskManager::CanDeleteRole).toBool());

        message.status = TaskProtocolStatus::Finished;
        message.progress = 101;
        QMetaObject::invokeMethod(manager, "handleTaskMessage", Qt::DirectConnection, Q_ARG(TaskMessage, message));
        QCOMPARE(manager->findTask(id)->status, TaskManager::Finished);
        QCOMPARE(manager->findTask(id)->progress, 100);
        QVERIFY(!manager->updateTaskProgress(id, -1));
        QVERIFY(manager->deleteTask(id));
        manager->clearTasks();
    }

    void localRunningTimeDoesNotDependOnPythonElapsed()
    {
        TaskManager *manager = TaskManager::getInstance();
        manager->clearTasks();

        const int id = manager->addTask(QStringLiteral("model-runtime"), QStringLiteral("Runtime"),
                                        ModelTaskType::Train, false);
        QVERIFY(id > 0);
        QVERIFY(manager->startTask(id));
        QVERIFY(manager->markTaskRunning(id));

        const QModelIndex index = manager->index(0, 0);
        QCOMPARE(index.data(TaskManager::RunningTimeRole).toString(), QStringLiteral("00:00:00"));
        QTest::qWait(1200);

        const QString running_time = manager->taskRunningTime(id);
        QVERIFY(running_time != QStringLiteral("00:00:00"));
        QCOMPARE(index.data(TaskManager::RunningTimeRole).toString(), running_time);

        QVERIFY(manager->finishTask(id));
        const QString finished_time = manager->taskRunningTime(id);
        QTest::qWait(1100);
        QCOMPARE(manager->taskRunningTime(id), finished_time);

        QVERIFY(manager->deleteTask(id));
        manager->clearTasks();
    }

    void coversFailureRestartPauseCapabilityAndInvalidTransitions()
    {
        TaskManager *manager = TaskManager::getInstance();
        manager->clearTasks();
        QSignalSpy start_requested(manager, &TaskManager::taskStartRequested);
        QSignalSpy stop_requested(manager, &TaskManager::taskStopRequested);

        const int no_pause = manager->addTask(QStringLiteral("model-no-pause"), QStringLiteral("No pause"),
                                               ModelTaskType::Test, QStringLiteral("scope-no-pause"),
                                               QStringLiteral("No pause"), false);
        QVERIFY(no_pause > 0);
        QVERIFY(manager->startTask(no_pause));
        QCOMPARE(manager->findTask(no_pause)->status, TaskManager::Preparing);
        QVERIFY(!manager->pauseTask(no_pause));
        QVERIFY(manager->stopTask(no_pause));
        QCOMPARE(manager->findTask(no_pause)->status, TaskManager::Stopping);
        QVERIFY(!manager->failTask(no_pause));
        QVERIFY(manager->markTaskStopped(no_pause));
        QVERIFY(!manager->markTaskRunning(no_pause));
        QVERIFY(manager->canStartTask(no_pause));
        QVERIFY(manager->startTask(no_pause));
        QCOMPARE(manager->findTask(no_pause)->progress, 0);
        QVERIFY(manager->markTaskRunning(no_pause));
        QVERIFY(manager->finishTask(no_pause));
        QCOMPARE(manager->findTask(no_pause)->progress, 100);
        QVERIFY(!manager->finishTask(no_pause));
        QVERIFY(!manager->updateTaskProgress(no_pause, 20));
        QVERIFY(manager->deleteTask(no_pause));

        const int failed = manager->addTask(QStringLiteral("model-failed"), QStringLiteral("Failed"),
                                             ModelTaskType::Train, true);
        QVERIFY(failed > 0);
        QVERIFY(manager->startTask(failed));
        QVERIFY(manager->failTask(failed));
        QCOMPARE(manager->findTask(failed)->status, TaskManager::Failed);
        QVERIFY(manager->canStartTask(failed));
        QVERIFY(manager->startTask(failed));
        QVERIFY(manager->markTaskRunning(failed));
        QVERIFY(manager->pauseTask(failed));
        QVERIFY(manager->canStartTask(failed));
        QVERIFY(manager->startTask(failed));
        QVERIFY(manager->markTaskRunning(failed));
        QVERIFY(manager->finishTask(failed));
        QVERIFY(manager->deleteTask(failed));

        QCOMPARE(start_requested.count(), 5);
        QCOMPARE(stop_requested.count(), 1);
        QVERIFY(!manager->startTask(-1));
        QVERIFY(!manager->deleteTask(-1));
        manager->clearTasks();
    }
};

REGISTER_TEST(TaskManagerTest)

#include "test_TaskManager.moc"
