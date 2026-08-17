#include "../test_runner.h"

#include "model/ModelStorageService.h"

#include <QDir>
#include <QTemporaryDir>
#include <QTest>

using namespace dltool::model;

class ModelStorageServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void pathLayout()
    {
        QTemporaryDir           temp;
        QVERIFY(temp.isValid());
        const ModelStorageService storage(temp.path());

        const QString model_root = storage.path(QStringLiteral("model-a"), ModelStorageLocation::ModelRoot);
        QVERIFY(model_root.endsWith(QStringLiteral("/models/model-a")));
        QVERIFY(model_root.startsWith(temp.path()));

        QVERIFY(storage.path(QStringLiteral(""), ModelStorageLocation::ModelRoot).isEmpty());
        QVERIFY(storage.path(QStringLiteral("../evil"), ModelStorageLocation::ModelRoot).isEmpty());
        QVERIFY(storage.path(QStringLiteral("..\\evil"), ModelStorageLocation::ModelRoot).isEmpty());
        const QString nested = storage.path(QStringLiteral("a/b"), ModelStorageLocation::ModelRoot);
        QVERIFY(!nested.isEmpty());
        QVERIFY(nested.startsWith(storage.path({}, ModelStorageLocation::ModelsRoot)));
    }

    void trainStorage()
    {
        QTemporaryDir           temp;
        QVERIFY(temp.isValid());
        const ModelStorageService storage(temp.path());

        QString error;
        QVERIFY(storage.ensureTrainStorage(QStringLiteral("model-a"), &error));
        QVERIFY(error.isEmpty());
        QVERIFY(QDir(storage.trainRoot(QStringLiteral("model-a"))).exists());
        QVERIFY(QDir(storage.trainWeightsPath(QStringLiteral("model-a"))).exists());
        QVERIFY(QDir(storage.trainLogsPath(QStringLiteral("model-a"))).exists());
        QVERIFY(QDir(storage.sharedDatasetPath(QStringLiteral("model-a"))).exists());
    }

    void testTaskStorage()
    {
        QTemporaryDir           temp;
        QVERIFY(temp.isValid());
        const ModelStorageService storage(temp.path());

        QString error;
        QVERIFY(storage.ensureTestTaskStorage(QStringLiteral("model-a"), QStringLiteral("task-1"), &error));
        QVERIFY(QDir(storage.testTaskRoot(QStringLiteral("model-a"), QStringLiteral("task-1"))).exists());
        QVERIFY(QDir(storage.testTaskPredictionPath(QStringLiteral("model-a"), QStringLiteral("task-1"))).exists());

        QVERIFY(storage.testTaskRoot(QStringLiteral("model-a"), QStringLiteral("../evil")).isEmpty());
        QVERIFY(storage.testTaskRoot(QStringLiteral("model-a"), QStringLiteral("a/b")).isEmpty());
        QVERIFY(!storage.ensureTestTaskStorage(QStringLiteral("model-a"), QStringLiteral("../evil"), &error));
        QVERIFY(!error.isEmpty());
    }

    void renameAndRemove()
    {
        QTemporaryDir           temp;
        QVERIFY(temp.isValid());
        const ModelStorageService storage(temp.path());

        QString error;
        QVERIFY(storage.ensureTrainStorage(QStringLiteral("old-name"), &error));

        QVERIFY(storage.renameModelStorage(QStringLiteral("old-name"), QStringLiteral("new-name"), &error));
        QVERIFY(QDir(storage.path(QStringLiteral("new-name"), ModelStorageLocation::ModelRoot)).exists());
        QVERIFY(!QDir(storage.path(QStringLiteral("old-name"), ModelStorageLocation::ModelRoot)).exists());

        QVERIFY(storage.removeModelStorage(QStringLiteral("new-name"), &error));
        QVERIFY(!QDir(storage.path(QStringLiteral("new-name"), ModelStorageLocation::ModelRoot)).exists());
    }
};

REGISTER_TEST(ModelStorageServiceTest)

#include "test_ModelStorageService.moc"
