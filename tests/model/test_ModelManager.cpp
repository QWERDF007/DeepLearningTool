#include "../test_runner.h"

#include "TestFixture.h"

#include "database/DataBase.h"
#include "model/ModelEvaluationProtocol.h"
#include "model/ModelManager.h"
#include "model/ModelStorageService.h"
#include "model/TaskManager.h"

#include <QDir>
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

        TaskManager::getInstance()->clearTasks();
        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        QString                  error;
        ModelManager::ModelRecordView record;
        {
            ModelManager manager(static_cast<int>(evaluation::Method::Detection), &database, nullptr);
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
            QCOMPARE(manager.rowCount(), 2);
            QCOMPARE(manager.modelAt(1).value(QStringLiteral("name")).toString(), QStringLiteral("DetectorRenamed Copy"));
            QVERIFY(QDir(ModelStorageService(fixture.rootPath()).path(QStringLiteral("DetectorRenamed Copy"),
                                                                     ModelStorageLocation::ModelRoot))
                        .exists());
        }

        ModelManager reloaded(static_cast<int>(evaluation::Method::Detection), &database, nullptr);
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
        TaskManager::getInstance()->clearTasks();
    }
};

REGISTER_TEST(ModelManagerTest)

#include "test_ModelManager.moc"
