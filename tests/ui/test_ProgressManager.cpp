#include "test_ProgressManager.h"

#include "ui/ProgressManager.h"

#include <spdlog/spdlog.h>
#include <QSignalSpy>

namespace dltool::ui {

void ProgressManagerTest::init()
{
    ProgressManager::getInstance()->reset();
}

void ProgressManagerTest::cleanup()
{
    ProgressManager::getInstance()->reset();
}

void ProgressManagerTest::taskIdentityIsEnforcedOnStartUpdateComplete()
{
    auto *pm = ProgressManager::getInstance();

    const QString task_id = pm->startTask("Task A", "task_A");
    QCOMPARE(task_id, QString("task_A"));
    QCOMPARE(pm->activeTaskId(), QString("task_A"));
    QCOMPARE(pm->getIsRunning(), true);
    QCOMPARE(pm->getProgress(), 0);

    // Matching task ID updates progress
    pm->updateProgress(30, "task_A");
    QCOMPARE(pm->getProgress(), 30);

    // Stale / mismatched task ID is rejected
    pm->updateProgress(80, "stale_task");
    QCOMPARE(pm->getProgress(), 30);

    // Empty task ID cannot update a task that was started with an identity
    pm->updateProgress(90);
    QCOMPARE(pm->getProgress(), 30);

    // Matching task ID finishes successfully
    pm->finishTask("task_A", true);
    QCOMPARE(pm->getIsRunning(), false);
    QCOMPARE(pm->getProgress(), 100);

    // Late update after task completed is ignored
    pm->updateProgress(50, "task_A");
    QCOMPARE(pm->getIsRunning(), false);
    QCOMPARE(pm->getProgress(), 100);
}

void ProgressManagerTest::interleavedTasksDoNotOverwriteEachOther()
{
    auto *pm = ProgressManager::getInstance();

    pm->startTask("Task 1", "task_1");
    pm->updateProgress(40, "task_1");
    QCOMPARE(pm->getProgress(), 40);

    // Task 2 preempts / starts as the active task
    pm->startTask("Task 2", "task_2");
    QCOMPARE(pm->activeTaskId(), QString("task_2"));
    QCOMPARE(pm->getIsRunning(), true);
    QCOMPARE(pm->getProgress(), 0);

    // Late worker from Task 1 reports progress -> ignored
    pm->updateProgress(60, "task_1");
    QCOMPARE(pm->getProgress(), 0);

    // Late worker from Task 1 reports finish -> ignored
    pm->finishTask("task_1", true);
    QCOMPARE(pm->getIsRunning(), true);
    QCOMPARE(pm->getProgress(), 0);

    // Task 2 reports progress -> accepted
    pm->updateProgress(75, "task_2");
    QCOMPARE(pm->getProgress(), 75);

    // Task 2 finishes -> accepted
    pm->finishTask("task_2", true);
    QCOMPARE(pm->getIsRunning(), false);
    QCOMPARE(pm->getProgress(), 100);
}

void ProgressManagerTest::failureAndCancellationDoNotDisguiseAs100Percent()
{
    auto *pm = ProgressManager::getInstance();

    pm->startTask("Failing Task", "task_fail");
    pm->updateProgress(45, "task_fail");
    QCOMPARE(pm->getProgress(), 45);

    QSignalSpy progress_spy(pm, &ProgressManager::progressChanged);
    QSignalSpy running_spy(pm, &ProgressManager::runningStateChanged);

    // Task fails: success = false
    pm->finishTask("task_fail", false);

    QCOMPARE(pm->getIsRunning(), false);
    // Crucial: Progress remains at 45%, NOT forced to 100%!
    QCOMPARE(pm->getProgress(), 45);
    QCOMPARE(running_spy.count(), 1);
    // progressChanged shouldn't fire since progress remained 45
    QCOMPARE(progress_spy.count(), 0);
}

void ProgressManagerTest::duplicateTerminalCallsDoNotReNotify()
{
    auto *pm = ProgressManager::getInstance();

    pm->startTask("Terminal Task", "task_term");
    pm->updateProgress(50, "task_term");

    QSignalSpy progress_spy(pm, &ProgressManager::progressChanged);
    QSignalSpy running_spy(pm, &ProgressManager::runningStateChanged);

    // First finish call
    pm->finishTask("task_term", false);
    QCOMPARE(running_spy.count(), 1);
    QCOMPARE(progress_spy.count(), 0);

    // Second finish call on the same terminal task
    pm->finishTask("task_term", false);
    QCOMPARE(running_spy.count(), 1); // No new signal!
    QCOMPARE(progress_spy.count(), 0);

    // Attempting completeTask on already finished task
    pm->completeTask("task_term", true);
    QCOMPARE(running_spy.count(), 1); // No new signal!
    QCOMPARE(progress_spy.count(), 0);
    QCOMPARE(pm->getProgress(), 50); // State remains untouched!
}

void ProgressManagerTest::resetClearsStateAndInvalidatesPreviousTaskId()
{
    auto *pm = ProgressManager::getInstance();

    pm->startTask("Reset Task", "task_reset");
    pm->updateProgress(60, "task_reset");
    QCOMPARE(pm->getProgress(), 60);

    pm->reset();
    QCOMPARE(pm->getIsRunning(), false);
    QCOMPARE(pm->getProgress(), 0);
    QCOMPARE(pm->activeTaskId(), QString());

    // Late updates or finishes from previous task are rejected
    pm->updateProgress(80, "task_reset");
    QCOMPARE(pm->getProgress(), 0);
    QCOMPARE(pm->getIsRunning(), false);

    pm->finishTask("task_reset", true);
    QCOMPARE(pm->getProgress(), 0);
    QCOMPARE(pm->getIsRunning(), false);
}

void ProgressManagerTest::messageFilteringByTaskId()
{
    auto *pm = ProgressManager::getInstance();

    pm->startTask("Message Task", "task_msg");
    pm->addMessage(spdlog::level::info, "valid message 1", "task_msg");
    QVERIFY(pm->getMessage().contains("valid message 1"));

    // Stale message with different task ID is ignored
    pm->addMessage(spdlog::level::err, "stale message", "other_task");
    QVERIFY(!pm->getMessage().contains("stale message"));

    pm->addMessage(spdlog::level::info, "valid message 2", "task_msg");
    QVERIFY(pm->getMessage().contains("valid message 2"));
}

REGISTER_TEST(ProgressManagerTest);

} // namespace dltool::ui
