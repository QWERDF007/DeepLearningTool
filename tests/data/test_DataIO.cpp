#include "data/DataIO.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTest>
#include <QThread>

#include <atomic>

namespace {

class ReusableDataIO final : public dltool::data::DataIO
{
    Q_OBJECT

public:
    using DataIO::isCancelRequested;
    using DataIO::requestCancel;
    using DataIO::runInThread;

    void startExport(dltool::data::ExportDataset dataset, const QString &output_dir,
                     const QVariantMap &options = {}) override
    {
        Q_UNUSED(dataset)
        Q_UNUSED(output_dir)
        Q_UNUSED(options)

        runInThread([this]()
                    {
                        started_.store(true, std::memory_order_release);
                        while (!isCancelRequested() && !release_.load(std::memory_order_acquire))
                            QThread::msleep(1);

                        const bool cancelled = isCancelRequested();
                        emit exportFinished(!cancelled, cancelled ? QStringLiteral("cancelled")
                                                               : QStringLiteral("ok"));
                    });
    }

    bool started() const
    {
        return started_.load(std::memory_order_acquire);
    }

    void resetProbe()
    {
        started_.store(false, std::memory_order_release);
        release_.store(false, std::memory_order_release);
    }

    void release()
    {
        release_.store(true, std::memory_order_release);
    }

private:
    std::atomic_bool started_{false};
    std::atomic_bool release_{false};
};

} // namespace

class DataIOTest final : public QObject
{
    Q_OBJECT

private slots:
    void reusedOperationClearsPreviousCancellation()
    {
        ReusableDataIO      io;
        QVector<bool>       results;
        QVector<QString>    messages;
        const QMetaObject::Connection connection
            = connect(&io, &dltool::data::DataIO::exportFinished, this,
                      [&results, &messages](const bool success, const QString &message)
                      {
                          results.push_back(success);
                          messages.push_back(message);
                      });

        io.startExport({}, {});
        QTRY_VERIFY_WITH_TIMEOUT(io.started(), 1000);
        io.requestCancel();
        QVERIFY(io.waitForDone(2000));
        QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 1000);
        QVERIFY(!results.at(0));
        QCOMPARE(messages.at(0), QStringLiteral("cancelled"));

        io.resetProbe();
        io.release();
        io.startExport({}, {});
        QTRY_VERIFY_WITH_TIMEOUT(io.started(), 1000);
        QVERIFY(io.waitForDone(2000));
        QTRY_COMPARE_WITH_TIMEOUT(results.size(), 2, 1000);
        QVERIFY(results.at(1));
        QCOMPARE(messages.at(1), QStringLiteral("ok"));

        disconnect(connection);
    }

    void parallelWorkerExceptionPropagatesAndPublishesFailureOnce()
    {
        ReusableDataIO   io;
        QVector<bool>    results;
        QVector<QString> messages;
        const QMetaObject::Connection connection
            = connect(&io, &dltool::data::DataIO::exportFinished, this,
                      [&results, &messages](const bool success, const QString &message)
                      {
                          results.push_back(success);
                          messages.push_back(message);
                      });

        // 启动后台任务：多个 worker 并行执行，最后一个 worker 抛出异常
        io.runInThread(
            []()
            {
                std::vector<std::jthread> workers;
                std::atomic_bool          stop_requested{false};
                std::exception_ptr        first_exception;
                std::mutex                mutex;

                for (int i = 0; i < 4; ++i)
                {
                    workers.emplace_back(
                        [&, i]()
                        {
                            if (i == 3)
                            {
                                QThread::msleep(15);
                                stop_requested.store(true, std::memory_order_relaxed);
                                std::lock_guard lock(mutex);
                                if (!first_exception)
                                    first_exception = std::make_exception_ptr(std::runtime_error("最后一个 worker 失败"));
                            }
                            else
                            {
                                while (!stop_requested.load(std::memory_order_relaxed))
                                    QThread::msleep(2);
                            }
                        });
                }

                for (auto &w : workers)
                {
                    if (w.joinable())
                        w.join();
                }

                if (first_exception)
                    std::rethrow_exception(first_exception);
            },
            [&io](const QString &error)
            {
                emit io.exportFinished(false, error);
            });

        QVERIFY(io.waitForDone(2000));
        QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 1000);
        QVERIFY(!results.at(0));
        QVERIFY(messages.at(0).contains(QStringLiteral("最后一个 worker 失败")));

        disconnect(connection);
    }
};

QTEST_GUILESS_MAIN(DataIOTest)

#include "test_DataIO.moc"
