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
    void projectScopedInstancesDoNotShareState()
    {
        TaskManager first;
        TaskManager second;

        const int first_id = first.addTask(QStringLiteral("project-one-model"), QStringLiteral("Project one"),
                                           ModelTaskType::Train);
        QVERIFY(first_id > 0);
        QCOMPARE(second.count(), 0);
        QVERIFY(second.findTask(first_id) == nullptr);
        QVERIFY(second.findModelTask(QStringLiteral("project-one-model"), ModelTaskType::Train) < 0);

        const int second_id = second.addTask(QStringLiteral("project-two-model"), QStringLiteral("Project two"),
                                             ModelTaskType::Train);
        QCOMPARE(second_id, first_id);
        QCOMPARE(first.count(), 1);
        QVERIFY(first.findTask(second_id) != nullptr);
        QVERIFY(first.findModelTask(QStringLiteral("project-two-model"), ModelTaskType::Train) < 0);
    }

    void lateEventsAfterTerminalStateAreDropped()
    {
        TaskManager manager;
        const int task_id = manager.addTask(QStringLiteral("terminal-model"), QStringLiteral("Terminal model"),
                                            ModelTaskType::Train);
        QVERIFY(task_id > 0);
        QVERIFY(manager.startTask(task_id));
        QVERIFY(manager.markTaskRunning(task_id));

        QSignalSpy messages(&manager, &TaskManager::taskMessageReceived);
        TaskMessage finished;
        finished.identity = manager.findTask(task_id)->identity;
        finished.type     = TaskMessageType::Status;
        finished.status   = TaskProtocolStatus::Finished;
        QMetaObject::invokeMethod(&manager, "handleTaskMessage", Qt::DirectConnection,
                                  Q_ARG(TaskMessage, finished));
        QCOMPARE(manager.findTask(task_id)->status, TaskManager::Finished);
        QCOMPARE(messages.count(), 1);

        TaskMessage late_running = finished;
        late_running.status      = TaskProtocolStatus::Running;
        late_running.progress    = 17;
        QMetaObject::invokeMethod(&manager, "handleTaskMessage", Qt::DirectConnection,
                                  Q_ARG(TaskMessage, late_running));
        QMetaObject::invokeMethod(&manager, "handleTaskMessage", Qt::DirectConnection,
                                  Q_ARG(TaskMessage, finished));

        QCOMPARE(manager.findTask(task_id)->status, TaskManager::Finished);
        QCOMPARE(manager.findTask(task_id)->progress, 100);
        QCOMPARE(messages.count(), 1);
    }

    void taskIdsAreNotReusedAfterClear()
    {
        TaskManager manager;
        const int first_id = manager.addTask(QStringLiteral("first-model"), QStringLiteral("First"),
                                             ModelTaskType::Train);
        QVERIFY(first_id > 0);

        manager.clearTasks();

        const int second_id = manager.addTask(QStringLiteral("second-model"), QStringLiteral("Second"),
                                              ModelTaskType::Train);
        QVERIFY(second_id > first_id);
    }

    void messagesFromPreviousRunAreDroppedAfterManagerRestart()
    {
        TaskMessage old_message;
        {
            TaskManager old_manager;
            const int old_task_id = old_manager.addTask(QStringLiteral("model"), QStringLiteral("Model"),
                                                        ModelTaskType::Train);
            QVERIFY(old_task_id > 0);
            QVERIFY(old_manager.startTask(old_task_id));
            const TaskManager::Task *old_task = old_manager.findTask(old_task_id);
            QVERIFY(old_task != nullptr);
            QVERIFY(old_task->identity.isValid());

            old_message.identity = old_task->identity;
            old_message.type     = TaskMessageType::Status;
            old_message.status   = TaskProtocolStatus::Finished;
        }

        TaskManager restarted_manager;
        const int new_task_id = restarted_manager.addTask(QStringLiteral("model"), QStringLiteral("Model"),
                                                           ModelTaskType::Train);
        QCOMPARE(new_task_id, old_message.identity.task_id);
        QVERIFY(restarted_manager.startTask(new_task_id));

        QMetaObject::invokeMethod(&restarted_manager, "handleTaskMessage", Qt::DirectConnection,
                                  Q_ARG(TaskMessage, old_message));

        const TaskManager::Task *new_task = restarted_manager.findTask(new_task_id);
        QVERIFY(new_task != nullptr);
        QVERIFY(new_task->identity.run_id != old_message.identity.run_id);
        QCOMPARE(new_task->status, TaskManager::Preparing);
    }

    void stateMachineNormalizesProgressAndProtectsTerminalStates()
    {
        TaskManager manager_instance;
        TaskManager *manager = &manager_instance;
        manager->clearTasks();
        QSignalSpy start_requested(manager, &TaskManager::taskStartRequested);
        QSignalSpy stop_requested(manager, &TaskManager::taskStopRequested);

        QCOMPARE(manager->addTask({}, QStringLiteral("Model"), ModelTaskType::Test), -1);
        const int id = manager->addTask(QStringLiteral("model-1"), QStringLiteral("Model"), ModelTaskType::Test,
                                        QStringLiteral("scope-1"), QStringLiteral("Scope"));
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
        TaskManager manager_instance;
        TaskManager *manager = &manager_instance;
        manager->clearTasks();
        const int id = manager->addTask(QStringLiteral("model-2"), QStringLiteral("Model 2"), ModelTaskType::Train);
        QVERIFY(id > 0);
        QVERIFY(manager->startTask(id));

        TaskMessage message;
        message.identity = manager->findTask(id)->identity;
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
        TaskManager manager_instance;
        TaskManager *manager = &manager_instance;
        manager->clearTasks();

        const int id = manager->addTask(QStringLiteral("model-runtime"), QStringLiteral("Runtime"),
                                        ModelTaskType::Train);
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

    void coversFailureRestartAndInvalidTransitions()
    {
        TaskManager manager_instance;
        TaskManager *manager = &manager_instance;
        manager->clearTasks();
        QSignalSpy start_requested(manager, &TaskManager::taskStartRequested);
        QSignalSpy stop_requested(manager, &TaskManager::taskStopRequested);

        const int stopped = manager->addTask(QStringLiteral("model-stopped"), QStringLiteral("Stopped"),
                                              ModelTaskType::Test, QStringLiteral("scope-stopped"),
                                              QStringLiteral("Stopped"));
        QVERIFY(stopped > 0);
        QVERIFY(manager->startTask(stopped));
        QCOMPARE(manager->findTask(stopped)->status, TaskManager::Preparing);
        QVERIFY(manager->stopTask(stopped));
        QCOMPARE(manager->findTask(stopped)->status, TaskManager::Stopping);
        QVERIFY(!manager->failTask(stopped));
        QVERIFY(manager->markTaskStopped(stopped));
        QVERIFY(!manager->markTaskRunning(stopped));
        QVERIFY(manager->canStartTask(stopped));
        QVERIFY(manager->startTask(stopped));
        QCOMPARE(manager->findTask(stopped)->progress, 0);
        QVERIFY(manager->markTaskRunning(stopped));
        QVERIFY(manager->finishTask(stopped));
        QCOMPARE(manager->findTask(stopped)->progress, 100);
        QVERIFY(!manager->finishTask(stopped));
        QVERIFY(!manager->updateTaskProgress(stopped, 20));
        QVERIFY(manager->deleteTask(stopped));

        const int failed = manager->addTask(QStringLiteral("model-failed"), QStringLiteral("Failed"),
                                             ModelTaskType::Train);
        QVERIFY(failed > 0);
        QVERIFY(manager->startTask(failed));
        QVERIFY(manager->failTask(failed));
        QCOMPARE(manager->findTask(failed)->status, TaskManager::Failed);
        QVERIFY(manager->canStartTask(failed));
        QVERIFY(manager->startTask(failed));
        QVERIFY(manager->markTaskRunning(failed));
        QVERIFY(!manager->canStartTask(failed));
        QVERIFY(!manager->startTask(failed));
        QVERIFY(manager->finishTask(failed));
        QVERIFY(manager->deleteTask(failed));

        QCOMPARE(start_requested.count(), 4);
        QCOMPARE(stop_requested.count(), 1);
        QVERIFY(!manager->startTask(-1));
        QVERIFY(!manager->deleteTask(-1));
        manager->clearTasks();
    }

    void shutdownRejectsNewTasksAndLateMessages()
    {
        TaskManager manager;
        const int task_id = manager.addTask(QStringLiteral("model-shutdown"), QStringLiteral("Shutdown"),
                                            ModelTaskType::Train);
        QVERIFY(task_id > 0);
        QVERIFY(manager.startTask(task_id));
        const TaskIdentity identity = manager.findTask(task_id)->identity;

        manager.shutdown();

        QCOMPARE(manager.count(), 0);
        QCOMPARE(manager.addTask(QStringLiteral("new-model"), QStringLiteral("New"), ModelTaskType::Train), -1);
        QVERIFY(!manager.startTask(task_id));
        QVERIFY(!manager.stopTask(task_id));
        QVERIFY(!manager.finishTask(task_id));
        QVERIFY(!manager.updateTaskProgress(task_id, 50));

        TaskMessage late_message;
        late_message.identity = identity;
        late_message.type    = TaskMessageType::Progress;
        late_message.status  = TaskProtocolStatus::Running;
        late_message.progress = 75;
        QMetaObject::invokeMethod(&manager, "handleTaskMessage", Qt::DirectConnection,
                                  Q_ARG(TaskMessage, late_message));
        QCOMPARE(manager.count(), 0);

        QString error;
        QVERIFY(!manager.ensureTaskServer(&error));
        QVERIFY(error.contains(QStringLiteral("关闭")));
    }
};

REGISTER_TEST(TaskManagerTest)

#include "test_TaskManager.moc"
