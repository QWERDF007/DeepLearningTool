#include "common/AsyncOperationWorkflow.h"

#include <QTest>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>

namespace {

using dltool::common::AsyncOperationResult;
using dltool::common::AsyncOperationState;
using dltool::common::AsyncOperationWorkflow;

/** 不携带额外字段的领域结果，用于验证公共生命周期本身。 */
struct PlainResult final : AsyncOperationResult
{
};

/** 可控执行闸门：由测试放行 worker，避免用固定 sleep 碰运气。 */
class ExecutionGate
{
public:
    void release()
    {
        {
            std::lock_guard lock(mutex_);
            open_ = true;
        }
        condition_.notify_all();
    }

    bool waitUntilReleased(const int timeout_ms = 10000) const
    {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this]() { return open_; });
    }

private:
    mutable std::mutex      mutex_;
    mutable std::condition_variable condition_;
    bool                            open_{false};
};

} // namespace

class AsyncOperationWorkflowTest final : public QObject
{
    Q_OBJECT

private slots:
    void successfulWorkCompletesOnce()
    {
        QObject context;
        int     completion_count  = 0;
        int     settled_count     = 0;
        bool    delivered_success = false;
        qint64  delivered_elapsed = -1;

        const auto handle = AsyncOperationWorkflow::start<PlainResult>(
            &context, [](PlainResult &result) { result.success = true; },
            [&](const PlainResult &result)
            {
                ++completion_count;
                delivered_success = result.success;
                delivered_elapsed = result.elapsed_ms;
            },
            [&](const AsyncOperationResult &) { ++settled_count; });

        QVERIFY(handle != nullptr);
        QVERIFY(handle->waitForDone(-1));
        QTRY_COMPARE(completion_count, 1);
        QCOMPARE(settled_count, 1);
        QVERIFY(delivered_success);
        QVERIFY(delivered_elapsed >= 0);
        QCOMPARE(handle->state(), AsyncOperationState::Completed);
        QVERIFY(!handle->isCancellationRequested());
        QVERIFY(handle->isCompletionFinished());
    }

    void workerExceptionBecomesFailedResult()
    {
        QObject context;
        int     completion_count = 0;
        QString delivered_error;
        bool    delivered_success = true;

        const auto handle = AsyncOperationWorkflow::start<PlainResult>(
            &context, [](PlainResult &) { throw std::runtime_error("worker 失败"); },
            [&](const PlainResult &result)
            {
                ++completion_count;
                delivered_success = result.success;
                delivered_error   = result.error;
            });

        QVERIFY(handle != nullptr);
        QVERIFY(handle->waitForDone(-1));
        QTRY_COMPARE(completion_count, 1);
        QVERIFY(!delivered_success);
        QVERIFY(delivered_error.contains(QStringLiteral("worker 失败")));
        QCOMPARE(handle->state(), AsyncOperationState::Failed);
    }

    void cancelRequestedBeforeWorkCommitIsNotTreatedAsSuccess()
    {
        QObject       context;
        ExecutionGate gate;
        int           completion_count = 0;
        bool          committed        = false;

        const auto handle = AsyncOperationWorkflow::start<PlainResult>(
            &context,
            [&](PlainResult &result)
            {
                if (!gate.waitUntilReleased())
                    return;
                if (result.cancellationRequested())
                    return;
                result.success = true;
                committed      = true;
            },
            [&](const PlainResult &result)
            {
                ++completion_count;
                committed = result.success;
            });

        QVERIFY(handle != nullptr);
        QVERIFY(handle->requestCancel());
        gate.release();

        QVERIFY(handle->waitForDone(-1));
        QTRY_COMPARE(completion_count, 1);
        QVERIFY(!committed);
        QCOMPARE(handle->state(), AsyncOperationState::Cancelled);
    }

    void cancelAfterCommitKeepsCommittedSuccess()
    {
        QObject       context;
        ExecutionGate gate;
        int           completion_count = 0;
        bool          delivered_success = false;
        bool          delivered_cancelled = true;

        const auto handle = AsyncOperationWorkflow::start<PlainResult>(
            &context,
            [&](PlainResult &result)
            {
                if (!gate.waitUntilReleased())
                    return;

                // 取消请求已经到达，但本次执行已经提交：提交语义优先，结果仍为成功。
                result.success = true;
            },
            [&](const PlainResult &result)
            {
                ++completion_count;
                delivered_success   = result.success;
                delivered_cancelled = result.cancelled;
            });

        QVERIFY(handle != nullptr);
        QVERIFY(!handle->isCancellationRequested());
        QVERIFY(handle->requestCancel());
        gate.release();

        QVERIFY(handle->waitForDone(-1));
        QTRY_COMPARE(completion_count, 1);
        QVERIFY(delivered_success);
        QVERIFY(!delivered_cancelled);
        QCOMPARE(handle->state(), AsyncOperationState::Completed);
    }

    void cancelRequestedWhileWorkerRunsReportsCancellingState()
    {
        QObject       context;
        ExecutionGate gate;
        int           completion_count  = 0;
        bool          delivered_cancelled = false;

        const auto handle = AsyncOperationWorkflow::start<PlainResult>(
            &context,
            [&](PlainResult &result)
            {
                if (!gate.waitUntilReleased())
                    return;
                if (result.cancellationRequested())
                    return;
                result.success = true;
            },
            [&](const PlainResult &result)
            {
                ++completion_count;
                delivered_cancelled = result.cancelled;
            });

        QVERIFY(handle != nullptr);
        QTRY_COMPARE(handle->state(), AsyncOperationState::Running);

        QVERIFY(handle->requestCancel());
        QCOMPARE(handle->state(), AsyncOperationState::Cancelling);
        QVERIFY(handle->isCancellationRequested());
        QVERIFY(!handle->isFinished());

        gate.release();

        QVERIFY(handle->waitForDone(-1));
        QTRY_COMPARE(completion_count, 1);
        QVERIFY(delivered_cancelled);
        QCOMPARE(handle->state(), AsyncOperationState::Cancelled);
    }

    void repeatedCancelAfterSettleIsRejectedAndCompletionStaysSingle()
    {
        QObject context;
        int     completion_count = 0;
        int     settled_count    = 0;

        const auto handle = AsyncOperationWorkflow::start<PlainResult>(
            &context, [](PlainResult &result) { result.success = true; },
            [&](const PlainResult &) { ++completion_count; },
            [&](const AsyncOperationResult &) { ++settled_count; });

        QVERIFY(handle != nullptr);
        QVERIFY(handle->waitForDone(-1));
        QTRY_COMPARE(completion_count, 1);

        QVERIFY(!handle->requestCancel());
        QVERIFY(!handle->requestCancel());
        QVERIFY(!handle->requestCancel());
        QCOMPARE(completion_count, 1);
        QCOMPARE(settled_count, 1);
        QCOMPARE(handle->state(), AsyncOperationState::Completed);
    }

    void finiteWaitTimesOutWhileWorkerBlocks()
    {
        QObject       context;
        ExecutionGate gate;

        const auto handle = AsyncOperationWorkflow::start<PlainResult>(
            &context, [&](PlainResult &result)
            {
                if (gate.waitUntilReleased())
                    result.success = true;
            });

        QVERIFY(handle != nullptr);
        QVERIFY(!handle->waitForDone(30));
        QVERIFY(!handle->isFinished());

        gate.release();

        QVERIFY(handle->waitForDone(-1));
        QCOMPARE(handle->state(), AsyncOperationState::Completed);
    }

    void waitForCompletionsDrainsQueuedCallbacks()
    {
        QObject context;
        int     completion_count = 0;

        QList<AsyncOperationWorkflow::HandlePtr> handles;
        handles.append(AsyncOperationWorkflow::start<PlainResult>(
            &context, [](PlainResult &result) { result.success = true; },
            [&](const PlainResult &) { ++completion_count; }));

        QVERIFY(handles.first() != nullptr);
        QVERIFY(AsyncOperationWorkflow::waitForCompletions(handles, -1));
        QCOMPARE(completion_count, 1);
        QVERIFY(handles.first()->isCompletionFinished());
    }

    void destroyedContextDiscardsCompletionButStillSettles()
    {
        int completion_count = 0;
        int settled_count    = 0;
        ExecutionGate gate;

        AsyncOperationWorkflow::HandlePtr handle;
        {
            QObject context;
            handle = AsyncOperationWorkflow::start<PlainResult>(
                &context,
                [&](PlainResult &result)
                {
                    if (gate.waitUntilReleased())
                        result.success = true;
                },
                [&](const PlainResult &) { ++completion_count; },
                [&](const AsyncOperationResult &) { ++settled_count; });

            QVERIFY(handle != nullptr);
            QTRY_COMPARE(handle->state(), AsyncOperationState::Running);
        }

        // context 已随作用域销毁：完成通知必须被丢弃，但收敛钩子仍恰好执行一次。
        QVERIFY(handle->isCancellationRequested());
        gate.release();

        QVERIFY(handle->waitForDone(-1));
        QTRY_COMPARE(settled_count, 1);
        QCOMPARE(completion_count, 0);
        QVERIFY(handle->isCompletionFinished());
        QCOMPARE(handle->state(), AsyncOperationState::Completed);
    }
};

QTEST_GUILESS_MAIN(AsyncOperationWorkflowTest)

#include "test_AsyncOperationWorkflow.moc"
