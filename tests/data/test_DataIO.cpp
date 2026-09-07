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
};

QTEST_GUILESS_MAIN(DataIOTest)

#include "test_DataIO.moc"
