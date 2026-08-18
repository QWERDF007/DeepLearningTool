#include "PersistentProjectFixture.h"

#include "data/DataManager.h"
#include "model/ModelStorageService.h"
#include "project/Projects.h"

#include <QFileInfo>
#include <QTest>

using namespace dltool::model::integration;

class PatchcoreModelIntegrationTest final : public QObject
{
    Q_OBJECT

private slots:
    void createsAndConfiguresPatchcoreModel()
    {
        PersistentProjectFixture fixture;
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        QString error;
        const qint64 dataset_id = fixture.dataManager()->getDatasetId(PersistentProjectFixture::datasetName());
        QVERIFY2(dataset_id >= 0,
                 qPrintable(QStringLiteral("数据集不存在，请先运行 data-creation: %1")
                                .arg(PersistentProjectFixture::datasetName())));

        QString model_uuid;
        QString task_uuid;
        QVERIFY2(fixture.configurePatchcore(&model_uuid, dataset_id, &task_uuid, &error), qPrintable(error));
        QVERIFY(!model_uuid.isEmpty());
        QVERIFY(!task_uuid.isEmpty());

        const auto record = fixture.project()->modelManager()->modelRecordViewForUuid(model_uuid);
        QVERIFY(record.isValid());
        QCOMPARE(record.name, PersistentProjectFixture::patchcoreModelName());
        QCOMPARE(record.framework_name, QStringLiteral("anomalib"));
        QCOMPARE(record.model_architecture, QStringLiteral("patchcore"));

        const dltool::model::ModelStorageService storage(PersistentProjectFixture::projectRoot());
        QVERIFY(QFileInfo(storage.modelDatabasePath(record.name)).isFile());
        QVERIFY(QFileInfo(storage.trainRoot(record.name)).isDir());
        QVERIFY(QFileInfo(storage.testTaskRoot(record.name, PersistentProjectFixture::patchcoreTestName())).isDir());
    }
};

QTEST_GUILESS_MAIN(PatchcoreModelIntegrationTest)

#include "test_PatchcoreModel.moc"
