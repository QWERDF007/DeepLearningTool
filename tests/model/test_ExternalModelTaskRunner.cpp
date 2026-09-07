#include "../test_runner.h"

#include "model/ExternalModelTaskRunner.h"

#include <QDir>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace dltool::model;

class ExternalModelTaskRunnerTest : public QObject
{
    Q_OBJECT

private slots:
    void rejectsInvalidSpecsWithoutStarting()
    {
        ExternalModelTaskRunner runner;
        ExternalProcessSpec spec;
        QString error;
        QVERIFY(!runner.start(spec, &error));
        QVERIFY(error.contains(QStringLiteral("任务")));
        spec.identity = {1, QStringLiteral("invalid-program")};
        error.clear();
        QVERIFY(!runner.start(spec, &error));
        QVERIFY(error.contains(QStringLiteral("程序")));
        spec.program = QStringLiteral("F:/tmp/no-such-executable");
        error.clear();
        QVERIFY(!runner.start(spec, &error));
        QVERIFY(error.contains(QStringLiteral("不存在")));
        QVERIFY(!runner.hasRunningTask(spec.identity));
    }

    void startsAndCollectsHelperProcessOutput()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        ExternalModelTaskRunner runner;
        QSignalSpy started(&runner, &ExternalModelTaskRunner::taskStarted);
        QSignalSpy finished(&runner, &ExternalModelTaskRunner::taskFinished);
        ExternalProcessSpec spec;
        spec.identity = {41, QStringLiteral("run-41")};
        spec.program = qEnvironmentVariable("ComSpec", QStringLiteral("C:/Windows/System32/cmd.exe"));
        spec.arguments = {QStringLiteral("/C"), QStringLiteral("echo runner-output")};
        spec.working_directory = temp.path();
        spec.log_path = QDir(temp.path()).filePath(QStringLiteral("runner.log"));
        QString error;
        QVERIFY2(runner.start(spec, &error), qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QCOMPARE(qvariant_cast<TaskIdentity>(started.at(0).at(0)), spec.identity);
        QCOMPARE(qvariant_cast<TaskIdentity>(finished.at(0).at(0)), spec.identity);
        QCOMPARE(finished.at(0).at(1).toInt(), 0);
        QVERIFY(finished.at(0).at(2).toBool());
        QVERIFY(QFileInfo::exists(spec.log_path));
        QFile log(spec.log_path);
        QVERIFY(log.open(QIODevice::ReadOnly));
        QVERIFY(QString::fromLocal8Bit(log.readAll()).contains(QStringLiteral("runner-output")));
        QVERIFY(!runner.hasRunningTask(spec.identity));
        QVERIFY(runner.stop(spec.identity));
        QVERIFY(runner.deleteTask(spec.identity));
    }

    void stopCanBeWaitedUntilTheProcessHasExited()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        ExternalModelTaskRunner runner;
        QSignalSpy         finished(&runner, &ExternalModelTaskRunner::taskFinished);
        ExternalProcessSpec spec;
        spec.identity           = {42, QStringLiteral("run-42")};
        spec.program           = qEnvironmentVariable("ComSpec", QStringLiteral("C:/Windows/System32/cmd.exe"));
        spec.arguments         = {QStringLiteral("/C"), QStringLiteral("ping -n 6 127.0.0.1 >NUL")};
        spec.working_directory = temp.path();
        spec.log_path          = QDir(temp.path()).filePath(QStringLiteral("long-running.log"));

        QString error;
        QVERIFY2(runner.start(spec, &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(runner.hasRunningTask(spec.identity), 3000);

        QVERIFY(runner.stop(spec.identity));
        QVERIFY(runner.waitForDone(5000));
        QVERIFY(!runner.hasRunningTask(spec.identity));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 1000);
        QCOMPARE(qvariant_cast<TaskIdentity>(finished.at(0).at(0)), spec.identity);
        QVERIFY(finished.at(0).at(3).toBool());
    }

    void rejectsIdentityReuseAndCrossTaskControl()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        ExternalModelTaskRunner runner;
        ExternalProcessSpec spec;
        spec.identity           = {43, QStringLiteral("run-43")};
        spec.program            = qEnvironmentVariable("ComSpec", QStringLiteral("C:/Windows/System32/cmd.exe"));
        spec.arguments          = {QStringLiteral("/C"), QStringLiteral("ping -n 4 127.0.0.1 >NUL")};
        spec.working_directory  = temp.path();
        spec.log_path           = QDir(temp.path()).filePath(QStringLiteral("identity.log"));

        QString error;
        QVERIFY2(runner.start(spec, &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(runner.hasRunningTask(spec.identity), 3000);

        ExternalProcessSpec duplicate = spec;
        error.clear();
        QVERIFY(!runner.start(duplicate, &error));
        QVERIFY(error.contains(QStringLiteral("已注册")));

        const TaskIdentity other_task{44, spec.identity.run_id};
        QVERIFY(!runner.hasRunningTask(other_task));
        QVERIFY(!runner.stop(other_task));
        QVERIFY(!runner.deleteTask(other_task));

        QVERIFY(runner.stop(spec.identity));
        QVERIFY(runner.waitForDone(5000));
    }
};

REGISTER_TEST(ExternalModelTaskRunnerTest)

#include "test_ExternalModelTaskRunner.moc"
