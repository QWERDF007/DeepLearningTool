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
        spec.task_id = 1;
        error.clear();
        QVERIFY(!runner.start(spec, &error));
        QVERIFY(error.contains(QStringLiteral("程序")));
        spec.program = QStringLiteral("F:/tmp/no-such-executable");
        error.clear();
        QVERIFY(!runner.start(spec, &error));
        QVERIFY(error.contains(QStringLiteral("不存在")));
        QVERIFY(!runner.hasRunningTask(1));
    }

    void startsAndCollectsHelperProcessOutput()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        ExternalModelTaskRunner runner;
        QSignalSpy started(&runner, &ExternalModelTaskRunner::taskStarted);
        QSignalSpy finished(&runner, &ExternalModelTaskRunner::taskFinished);
        ExternalProcessSpec spec;
        spec.task_id = 41;
        spec.program = qEnvironmentVariable("ComSpec", QStringLiteral("C:/Windows/System32/cmd.exe"));
        spec.arguments = {QStringLiteral("/C"), QStringLiteral("echo runner-output")};
        spec.working_directory = temp.path();
        spec.log_path = QDir(temp.path()).filePath(QStringLiteral("runner.log"));
        QString error;
        QVERIFY2(runner.start(spec, &error), qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QCOMPARE(finished.at(0).at(0).toInt(), 41);
        QCOMPARE(finished.at(0).at(1).toInt(), 0);
        QVERIFY(finished.at(0).at(2).toBool());
        QVERIFY(QFileInfo::exists(spec.log_path));
        QFile log(spec.log_path);
        QVERIFY(log.open(QIODevice::ReadOnly));
        QVERIFY(QString::fromLocal8Bit(log.readAll()).contains(QStringLiteral("runner-output")));
        QVERIFY(!runner.hasRunningTask(41));
        QVERIFY(runner.stop(41));
        QVERIFY(runner.deleteTask(41));
    }
};

REGISTER_TEST(ExternalModelTaskRunnerTest)

#include "test_ExternalModelTaskRunner.moc"
