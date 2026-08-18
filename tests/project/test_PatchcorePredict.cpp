#include "PersistentProjectFixture.h"

#include "model/ModelStorageService.h"
#include "project/Projects.h"

#include <QDir>
#include <QFileInfo>
#include <QTest>

using namespace dltool::model::integration;

class PatchcorePredictIntegrationTest final : public QObject
{
    Q_OBJECT

private slots:
    void predictsWithTrainedPatchcore()
    {
        PersistentProjectFixture fixture;
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        QString model_uuid;
        QString task_uuid;
        QString error;
        QVERIFY2(fixture.findPatchcoreModel(&model_uuid, &error), qPrintable(error));
        QVERIFY2(fixture.findPatchcoreTask(model_uuid, &task_uuid, &error), qPrintable(error));

        const auto record = fixture.project()->modelManager()->modelRecordViewForUuid(model_uuid);
        QVERIFY(record.isValid());
        const dltool::model::ModelStorageService storage(PersistentProjectFixture::projectRoot());
        const QString checkpoint = storage.trainWeightsPath(record.name) + QStringLiteral("/model.ckpt");
        QVERIFY2(QFileInfo(checkpoint).isFile(), qPrintable(QStringLiteral("缺少训练权重，请先运行训练测试")));

        const int task_id = fixture.project()->modelTaskController()->startModelTestTask(model_uuid, task_uuid);
        QVERIFY2(task_id > 0, qPrintable(QStringLiteral("无法启动 PatchCore 预测")));
        QVERIFY2(PersistentProjectFixture::waitForTask(fixture.project()->taskManager(), task_id, &error),
                 qPrintable(error));

        const QString prediction_dir = storage.testTaskPredictionPath(record.name, PersistentProjectFixture::patchcoreTestName());
        const QStringList predictions = QDir(prediction_dir).entryList({QStringLiteral("*.tiff")}, QDir::Files);
        QCOMPARE(predictions.size(), 14);
        QVERIFY(QFileInfo(storage.testTaskDatabasePath(record.name,
                                                       PersistentProjectFixture::patchcoreTestName()))
                    .isFile());
    }
};

QTEST_GUILESS_MAIN(PatchcorePredictIntegrationTest)

#include "test_PatchcorePredict.moc"
