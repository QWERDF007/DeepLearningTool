#include "../test_runner.h"

#include "model/ExternalModelTaskRunner.h"

#include <QDir>
#include <QElapsedTimer>
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
        spec.identity = {1, QStringLiteral("invalid-program"), QStringLiteral("project-1")};
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
        spec.identity = {41, QStringLiteral("run-41"), QStringLiteral("project-41")};
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
        spec.identity           = {42, QStringLiteral("run-42"), QStringLiteral("project-42")};
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

    void shutdownHasBoundedWaitForLongRunningProcess()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        ExternalModelTaskRunner runner;
        ExternalProcessSpec spec;
        spec.identity           = {46, QStringLiteral("run-46"), QStringLiteral("project-46")};
        spec.program             = qEnvironmentVariable("ComSpec", QStringLiteral("C:/Windows/System32/cmd.exe"));
        spec.arguments           = {QStringLiteral("/C"), QStringLiteral("ping -n 20 127.0.0.1 >NUL")};
        spec.working_directory   = temp.path();
        spec.log_path            = QDir(temp.path()).filePath(QStringLiteral("shutdown.log"));

        QString error;
        QVERIFY2(runner.start(spec, &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(runner.hasRunningTask(spec.identity), 3000);

        QElapsedTimer shutdown_timer;
        shutdown_timer.start();
        runner.shutdown();

        QVERIFY2(shutdown_timer.elapsed() < 12000,
                 qPrintable(QStringLiteral("关闭外部任务等待超时: %1 ms").arg(shutdown_timer.elapsed())));
        QVERIFY(!runner.hasRunningTask(spec.identity));

        error.clear();
        QVERIFY(!runner.start(spec, &error));
        QVERIFY(error.contains(QStringLiteral("关闭")));
    }

    void rejectsIdentityReuseAndCrossTaskControl()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        ExternalModelTaskRunner runner;
        ExternalProcessSpec spec;
        spec.identity           = {43, QStringLiteral("run-43"), QStringLiteral("project-43")};
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

        const TaskIdentity other_task{44, spec.identity.run_id, spec.identity.project_id};
        QVERIFY(!runner.hasRunningTask(other_task));
        QVERIFY(!runner.stop(other_task));
        QVERIFY(!runner.deleteTask(other_task));

        QVERIFY(runner.stop(spec.identity));
        QVERIFY(runner.waitForDone(5000));
    }

    void isolatesSameRunIdAcrossProjects()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        ExternalModelTaskRunner runner;
        const QString program = qEnvironmentVariable("ComSpec", QStringLiteral("C:/Windows/System32/cmd.exe"));

        ExternalProcessSpec first;
        first.identity          = {45, QStringLiteral("shared-run"), QStringLiteral("project-a")};
        first.program           = program;
        first.arguments         = {QStringLiteral("/C"), QStringLiteral("ping -n 20 127.0.0.1 >NUL")};
        first.working_directory = temp.path();
        first.log_path          = QDir(temp.path()).filePath(QStringLiteral("project-a.log"));

        ExternalProcessSpec second = first;
        second.identity.project_id = QStringLiteral("project-b");
        second.log_path            = QDir(temp.path()).filePath(QStringLiteral("project-b.log"));

        QString error;
        QVERIFY2(runner.start(first, &error), qPrintable(error));
        QVERIFY2(runner.start(second, &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(runner.hasRunningTask(first.identity), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(runner.hasRunningTask(second.identity), 3000);

        QVERIFY(runner.stop(first.identity));
        QVERIFY(runner.hasRunningTask(second.identity));

        QVERIFY(runner.stop(second.identity));
        QVERIFY(runner.waitForDone(5000));
    }
};

REGISTER_TEST(ExternalModelTaskRunnerTest)

#include "test_ExternalModelTaskRunner.moc"
