#include "data/DataIO.h"
#include "data/ParallelFor.h"
#include <latch>
#include <stdexcept>

#include <QCoreApplication>
#include <QEventLoop>
#include <QTest>
#include <QThread>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

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
    void exportPublication_data()
    {
        QTest::addColumn<bool>("existing");
        QTest::addColumn<int>("failed_rename");
        QTest::newRow("new-target") << false << 0;
        QTest::newRow("replace-target") << true << 0;
        QTest::newRow("new-publish-fails") << false << 1;
        QTest::newRow("backup-fails") << true << 1;
        QTest::newRow("publish-fails-restores-original") << true << 2;
    }

    void exportPublication()
    {
        QFETCH(bool, existing);
        QFETCH(int, failed_rename);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString target = directory.filePath(QStringLiteral("export"));
        if (existing)
        {
            QVERIFY(QDir().mkpath(target));
            QFile original(QDir(target).filePath(QStringLiteral("value.txt")));
            QVERIFY(original.open(QIODevice::WriteOnly));
            QCOMPARE(original.write("original"), qint64(8));
        }
        QString staging;
        QString error;
        int calls = 0;
        {
            dltool::data::SafeExportScope scope(target,
                [&](const QString &from, const QString &to)
                {
                    return ++calls != failed_rename && QDir().rename(from, to);
                });
            QVERIFY(scope.isValid());
            staging = scope.stagingDir();
            QCOMPARE(QFileInfo(staging).absolutePath(), directory.path());
            QFile replacement(QDir(staging).filePath(QStringLiteral("value.txt")));
            QVERIFY(replacement.open(QIODevice::WriteOnly));
            QCOMPARE(replacement.write("replacement"), qint64(11));
            replacement.close();
            QCOMPARE(scope.publish(error), failed_rename == 0);
            if (failed_rename == 0)
            {
                const int published_calls = calls;
                QVERIFY(scope.publish(error));
                QCOMPARE(calls, published_calls);
            }
            else
                QVERIFY(!error.isEmpty());
        }
        QVERIFY(!QFileInfo::exists(staging));
        if (existing || failed_rename == 0)
        {
            QFile result(QDir(target).filePath(QStringLiteral("value.txt")));
            QVERIFY(result.open(QIODevice::ReadOnly));
            QCOMPARE(result.readAll(), failed_rename == 0 ? QByteArray("replacement") : QByteArray("original"));
        }
        else
            QVERIFY(!QFileInfo::exists(target));
        QVERIFY(QDir(directory.path()).entryList({QStringLiteral(".backup_*")},
                    QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
    }

    void failedExportRestorePreservesBackup()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString target = directory.filePath(QStringLiteral("export"));
        QVERIFY(QDir().mkpath(target));
        QFile original(QDir(target).filePath(QStringLiteral("original.txt")));
        QVERIFY(original.open(QIODevice::WriteOnly));
        QCOMPARE(original.write("original"), qint64(8));
        original.close();
        QString backup;
        QString error;
        {
            dltool::data::SafeExportScope scope(target,
                [&](const QString &from, const QString &to)
                {
                    if (from == target)
                    {
                        backup = to;
                        return QDir().rename(from, to);
                    }
                    // Remove only this test's staging to force the legacy copy fallback to fail too.
                    if (from != backup)
                        QDir(from).removeRecursively();
                    else
                    {
                        QFile blocker(target);
                        if (!blocker.open(QIODevice::WriteOnly))
                            return false;
                        blocker.write("concurrent target");
                    }
                    return false;
                });
            QVERIFY(scope.isValid());
            QVERIFY(!scope.publish(error));
        }
        QFile saved(QDir(backup).filePath(QStringLiteral("original.txt")));
        QVERIFY2(saved.open(QIODevice::ReadOnly), qPrintable(error));
        QCOMPARE(saved.readAll(), QByteArray("original"));
        QVERIFY(error.contains(backup));
        QVERIFY(!error.contains(QStringLiteral("已恢复")));
    }

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
                std::atomic_bool cancelled{false};
                std::latch finished_workers(3);
                dltool::data::parallelFor(4, 4, cancelled, [&](std::size_t index)
                {
                    if (index == 3)
                    {
                        finished_workers.wait();
                        throw std::runtime_error("最后一个 worker 失败");
                    }
                    finished_workers.count_down();
                });
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

    void parallelExecutionCompletesEachItemOnce()
    {
        std::atomic_bool cancelled{false};
        std::vector<int> visits(37, 0);
        dltool::data::parallelFor(visits.size(), 4, cancelled,
                                 [&](std::size_t index) { ++visits[index]; });
        for (int count : visits)
            QCOMPARE(count, 1);
    }

    void parallelExecutionSkipsPrecancelledWork()
    {
        std::atomic_bool cancelled{true};
        std::atomic_int calls{0};
        dltool::data::parallelFor(37, 4, cancelled,
                                 [&](std::size_t) { ++calls; });
        QCOMPARE(calls.load(), 0);
    }

    void parallelCancellationWaitsForInFlightWork()
    {
        std::atomic_bool cancelled{false};
        std::atomic_int completed{0};
        std::latch started(4);
        dltool::data::parallelFor(37, 4, cancelled, [&](std::size_t)
        {
            started.arrive_and_wait();
            cancelled.store(true);
            ++completed;
        });
        QCOMPARE(completed.load(), 4);
    }
};

QTEST_GUILESS_MAIN(DataIOTest)

#include "test_DataIO.moc"
