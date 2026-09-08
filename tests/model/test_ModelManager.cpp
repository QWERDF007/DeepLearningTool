#include "../test_runner.h"

#include "TestFixture.h"

#include "database/DataBase.h"
#include "model/ModelEvaluationProtocol.h"
#include "model/ModelManager.h"
#include "model/ModelStorageService.h"
#include "model/TensorBoardRunner.h"
#include "model/TaskManager.h"

#include <QElapsedTimer>
#include <QDir>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QSignalSpy>
#include <QTest>

using namespace dltool::model;
using namespace dltool::model::testsupport;

class ModelManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void modelCrudPersistsExtraDataAndCachesInstances()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        TaskManager task_manager;
        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        QString                  error;
        ModelManager::ModelRecordView record;
        {
            ModelManager manager(static_cast<int>(evaluation::Method::Detection), &database, nullptr, &task_manager);
            QCOMPARE(manager.rowCount(), 0);
            QVERIFY(!manager.validateModelName(QString()).isEmpty());
            QVERIFY(!manager.validateModelName(QStringLiteral("bad/name")).isEmpty());
            QVERIFY(manager.validateModelName(QStringLiteral("Detector")).isEmpty());

            record = manager.addModelRecord(QStringLiteral("Detector"), QStringLiteral("ultralytics"),
                                            QStringLiteral("YOLOv8"), &error);
            QVERIFY2(record.isValid(), qPrintable(error));
            QCOMPARE(manager.rowCount(), 1);
            QCOMPARE(manager.supportedFrameworks().contains(QStringLiteral("ultralytics")), true);
            QVERIFY(manager.supportedModelArchitectures(QStringLiteral("ultralytics"))
                        .contains(QStringLiteral("YOLOv8")));
            QCOMPARE(manager.modelAt(0).value(QStringLiteral("uuid")).toString(), record.uuid);
            QCOMPARE(manager.modelAt(-1), QVariantMap{});
            QCOMPARE(manager.modelRecordForUuid(QStringLiteral("missing")), QVariantMap{});

            QSignalSpy extra_changed(&manager, &ModelManager::modelExtraDataChanged);
            QVERIFY(manager.updateModelExtraData(record.uuid,
                                                 {{QStringLiteral("train"),
                                                   QVariantMap{{QStringLiteral("progress"), 37},
                                                               {QStringLiteral("phase"), QStringLiteral("fit")}}},
                                                  {QStringLiteral("custom"), QStringLiteral("value")}},
                                                 &error));
            QCOMPARE(extra_changed.count(), 1);
            QCOMPARE(manager.modelRecordForUuid(record.uuid)
                         .value(QStringLiteral("extra_data"))
                         .toMap()
                         .value(QStringLiteral("train"))
                         .toMap()
                         .value(QStringLiteral("progress"))
                         .toInt(),
                     37);

            QVERIFY(manager.resetModelTaskState(record.uuid, QStringLiteral("train"), {QStringLiteral("phase")},
                                                {{QStringLiteral("progress"), 0}}, &error));
            const QVariantMap reset_extra
                = manager.modelRecordForUuid(record.uuid).value(QStringLiteral("extra_data")).toMap();
            QVERIFY(!reset_extra.value(QStringLiteral("train")).toMap().contains(QStringLiteral("phase")));
            QCOMPARE(reset_extra.value(QStringLiteral("train")).toMap().value(QStringLiteral("progress")).toInt(), 0);

            IModel *first = manager.modelForUuid(record.uuid);
            QVERIFY(first != nullptr);
            QCOMPARE(first->uuid(), record.uuid);
            QCOMPARE(first->frameworkName(), QStringLiteral("ultralytics"));
            QCOMPARE(manager.modelForUuid(record.uuid), first);

            const qint64 model_id = record.model_id;
            QVERIFY(manager.renameModel(model_id, QStringLiteral("DetectorRenamed")));
            QVERIFY(QDir(ModelStorageService(fixture.rootPath()).path(QStringLiteral("DetectorRenamed"),
                                                                     ModelStorageLocation::ModelRoot))
                        .exists());
            QVERIFY(!manager.renameModel(model_id, QStringLiteral("bad/name")));
            QVERIFY(manager.copyModel(model_id));
            QVERIFY(manager.waitForOperations());
            QCOMPARE(manager.rowCount(), 2);
            QCOMPARE(manager.modelAt(1).value(QStringLiteral("name")).toString(), QStringLiteral("DetectorRenamed Copy"));
            QVERIFY(QDir(ModelStorageService(fixture.rootPath()).path(QStringLiteral("DetectorRenamed Copy"),
                                                                     ModelStorageLocation::ModelRoot))
                        .exists());
        }

        ModelManager reloaded(static_cast<int>(evaluation::Method::Detection), &database, nullptr, &task_manager);
        QCOMPARE(reloaded.rowCount(), 2);
        QCOMPARE(reloaded.modelRecordForUuid(record.uuid)
                     .value(QStringLiteral("extra_data"))
                     .toMap()
                     .value(QStringLiteral("custom"))
                     .toString(),
                 QStringLiteral("value"));
        QVERIFY(reloaded.deleteModel(record.model_id));
        QCOMPARE(reloaded.rowCount(), 1);
        const qint64 copied_id = reloaded.modelAt(0).value(QStringLiteral("model_id")).toLongLong();
        QVERIFY(reloaded.deleteModel(copied_id));
        QCOMPARE(reloaded.rowCount(), 0);
        QVERIFY(!QDir(ModelStorageService(fixture.rootPath()).path(QStringLiteral("DetectorRenamed"),
                                                                  ModelStorageLocation::ModelRoot))
                    .exists());
        QVERIFY(!reloaded.updateModelExtraData(record.uuid, {{QStringLiteral("x"), 1}}, &error));
        QVERIFY(error.contains(QStringLiteral("不存在")));
    }

    void lifecycleFailureDoesNotRefreshModel()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        TaskManager task_manager;
        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager manager(static_cast<int>(evaluation::Method::Detection), &database, nullptr, &task_manager);
        QString error;
        const ModelManager::ModelRecordView record
            = manager.addModelRecord(QStringLiteral("Stable"), QStringLiteral("ultralytics"),
                                     QStringLiteral("YOLOv8"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        const ModelStorageService storage(fixture.rootPath());
        const QString            blocked_target = storage.modelRoot(QStringLiteral("BlockedTarget"));
        QVERIFY(QDir().mkpath(blocked_target));
        QVERIFY(!manager.renameModel(record.model_id, QStringLiteral("BlockedTarget")));
        QCOMPARE(manager.rowCount(), 1);
        QCOMPARE(manager.modelRecordForUuid(record.uuid).value(QStringLiteral("name")).toString(),
                 QStringLiteral("Stable"));
        QVERIFY(QDir(storage.modelRoot(QStringLiteral("Stable"))).exists());
        QVERIFY(QDir(blocked_target).removeRecursively());

        const QString stable_root = storage.modelRoot(QStringLiteral("Stable"));
        QVERIFY(QDir(stable_root).removeRecursively());
        QVERIFY(!manager.deleteModel(record.model_id));
        QCOMPARE(manager.rowCount(), 1);
        QVERIFY(manager.modelRecordForUuid(record.uuid).value(QStringLiteral("name")).toString()
                == QStringLiteral("Stable"));

        QVERIFY2(storage.ensureModelStorage(QStringLiteral("Stable"), &error), qPrintable(error));
        QVERIFY(manager.deleteModel(record.model_id));
        QCOMPARE(manager.rowCount(), 0);
    }

    void tensorBoardRunnerShutdownStopsLongRunningProcess()
    {
        TensorBoardRunner runner;

        TensorBoardLaunchSpec spec;
        spec.model_uuid = QStringLiteral("tensorboard-shutdown-test");
        spec.port       = TensorBoardRunner::availableLocalPort();
        spec.environment = QProcessEnvironment::systemEnvironment();
        QVERIFY(spec.port != 0);

#ifdef Q_OS_WIN
        spec.program   = QStandardPaths::findExecutable(QStringLiteral("powershell.exe"));
        spec.arguments = {QStringLiteral("-NoLogo"), QStringLiteral("-NoProfile"),
                          QStringLiteral("-NonInteractive"), QStringLiteral("-Command"),
                          QStringLiteral("Start-Sleep -Seconds 30")};
#else
        spec.program   = QStandardPaths::findExecutable(QStringLiteral("sh"));
        spec.arguments = {QStringLiteral("-c"), QStringLiteral("sleep 30")};
#endif
        QVERIFY2(!spec.program.isEmpty(), "无法找到测试用的进程解释器");

        QString error;
        QVERIFY2(runner.start(spec, &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(runner.isRunning(), 2000);

        QElapsedTimer shutdown_timer;
        shutdown_timer.start();
        runner.shutdown();

        QVERIFY(!runner.isRunning());
        QVERIFY2(shutdown_timer.elapsed() < 3000,
                 qPrintable(QStringLiteral("TensorBoard 进程关闭超时: %1 ms").arg(shutdown_timer.elapsed())));

        runner.shutdown();
        QVERIFY(!runner.start(spec, &error));
        QCOMPARE(error, QStringLiteral("TensorBoard 运行器正在关闭"));
    }
};

REGISTER_TEST(ModelManagerTest)

#include "test_ModelManager.moc"
