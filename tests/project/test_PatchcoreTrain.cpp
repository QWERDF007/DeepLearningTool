#include "PersistentProjectFixture.h"

#include "model/ModelStorageService.h"
#include "project/Projects.h"

#include <QFileInfo>
#include <QTest>

using namespace dltool::model::integration;

class PatchcoreTrainIntegrationTest final : public QObject
{
    Q_OBJECT

private slots:
    void trainsPatchcore()
    {
        PersistentProjectFixture fixture;
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        QString model_uuid;
        QString error;
        QVERIFY2(fixture.findPatchcoreModel(&model_uuid, &error), qPrintable(error));

        const int task_id = fixture.project()->modelTaskController()->startModelTask(
            model_uuid, dltool::model::ModelTaskType::Train);
        QVERIFY2(task_id > 0, qPrintable(QStringLiteral("无法启动 PatchCore 训练")));
        QVERIFY2(PersistentProjectFixture::waitForTask(fixture.project()->taskManager(), task_id, &error),
                 qPrintable(error));

        const auto record = fixture.project()->modelManager()->modelRecordViewForUuid(model_uuid);
        QVERIFY(record.isValid());
        const dltool::model::ModelStorageService storage(PersistentProjectFixture::projectRoot());
        QVERIFY2(QFileInfo(storage.trainWeightsPath(record.name) + QStringLiteral("/model.ckpt")).isFile(),
                 qPrintable(QStringLiteral("训练完成但未找到 model.ckpt")));
    }
};

QTEST_GUILESS_MAIN(PatchcoreTrainIntegrationTest)

#include "test_PatchcoreTrain.moc"
