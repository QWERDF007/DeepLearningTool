#include "data/DataOperationWorkflow.h"

#include <QTest>
#include <QThread>

#include <atomic>
#include <memory>

using namespace dltool::data;

class DataOperationWorkflowTest : public QObject
{
    Q_OBJECT

private slots:
    void cancellationIsVisibleToWorkAndWaitReturnsAfterWorkerExits()
    {
        QObject context;
        std::atomic_bool work_started{false};
        std::atomic_bool observed_cancel{false};
        bool              completion_called = false;
        DataOperationWorkflow::Result completion_result;

        const auto handle = DataOperationWorkflow::start(
            &context, {},
            [&work_started, &observed_cancel](DataOperationWorkflow::Result &result)
            {
                work_started.store(true, std::memory_order_relaxed);
                for (int i = 0; i < 100 && !result.cancellationRequested(); ++i)
                    QThread::msleep(5);
                observed_cancel.store(result.cancellationRequested(), std::memory_order_relaxed);
            },
            [&completion_called, &completion_result](const DataOperationWorkflow::Result &result)
            {
                completion_called = true;
                completion_result  = result;
            });

        QVERIFY(handle != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(work_started.load(std::memory_order_relaxed), 1000);
        QVERIFY(!handle->isFinished());
        QVERIFY(handle->requestCancel());
        QVERIFY(handle->isCancellationRequested());
        QVERIFY(handle->waitForDone(2000));
        QVERIFY(handle->isFinished());
        QVERIFY(observed_cancel.load(std::memory_order_relaxed));

        QTRY_VERIFY_WITH_TIMEOUT(completion_called, 1000);
        QVERIFY(completion_result.cancelled);
        QVERIFY(!completion_result.success);
    }

    void waitForDoneDoesNotReplaceCompletionOnSuccessfulWork()
    {
        QObject context;
        bool    completion_called = false;
        DataOperationWorkflow::Result completion_result;

        const auto handle = DataOperationWorkflow::start(
            &context, {},
            [](DataOperationWorkflow::Result &result)
            {
                QThread::msleep(30);
                result.success = true;
            },
            [&completion_called, &completion_result](const DataOperationWorkflow::Result &result)
            {
                completion_called = true;
                completion_result  = result;
            });

        QVERIFY(handle != nullptr);
        QVERIFY(handle->waitForDone(2000));
        QVERIFY(handle->isFinished());
        QTRY_VERIFY_WITH_TIMEOUT(completion_called, 1000);
        QVERIFY(completion_result.success);
        QVERIFY(!completion_result.cancelled);
        QVERIFY(handle->isCompletionFinished());
    }

    void destroyedContextCancelsWorkerAndDiscardsCompletion()
    {
        auto              context = std::make_unique<QObject>();
        std::atomic_bool  work_started{false};
        bool              completion_called = false;

        const auto handle = DataOperationWorkflow::start(
            context.get(), {},
            [&work_started](DataOperationWorkflow::Result &result)
            {
                work_started.store(true, std::memory_order_relaxed);
                while (!result.cancellationRequested())
                    QThread::msleep(5);
            },
            [&completion_called](const DataOperationWorkflow::Result &)
            {
                completion_called = true;
            });

        QVERIFY(handle != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(work_started.load(std::memory_order_relaxed), 1000);
        context.reset();

        QVERIFY(handle->waitForDone(2000));
        QVERIFY(handle->isFinished());
        QVERIFY(handle->isCompletionFinished());
        QVERIFY(!completion_called);
    }
};

QTEST_GUILESS_MAIN(DataOperationWorkflowTest)

#include "test_DataOperationWorkflow.moc"
