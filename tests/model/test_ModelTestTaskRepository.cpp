#include "../test_runner.h"

#include "TestFixture.h"

#include "model/ModelStorageService.h"
#include "model/ModelEvaluationProtocol.h"
#include "model/ModelTestTaskRepository.h"

#include <QDir>
#include <QTest>

using namespace dltool::model;
using namespace dltool::model::testsupport;

class ModelTestTaskRepositoryTest : public QObject
{
    Q_OBJECT

private slots:
    void validatesWindowsSafeNames()
    {
        QVERIFY(ModelTestTaskRepository::validateTaskName(QStringLiteral("Task 1")).isEmpty());
        QVERIFY(!ModelTestTaskRepository::validateTaskName(QString()).isEmpty());
        QVERIFY(!ModelTestTaskRepository::validateTaskName(QStringLiteral("CON")).isEmpty());
        QVERIFY(!ModelTestTaskRepository::validateTaskName(QStringLiteral("CON.txt")).isEmpty());
        QVERIFY(!ModelTestTaskRepository::validateTaskName(QStringLiteral("LPT1")).isEmpty());
        QVERIFY(!ModelTestTaskRepository::validateTaskName(QStringLiteral("a/b")).isEmpty());
        QVERIFY(!ModelTestTaskRepository::validateTaskName(QStringLiteral("a\\b")).isEmpty());
        QVERIFY(!ModelTestTaskRepository::validateTaskName(QStringLiteral(".")).isEmpty());
        QVERIFY(!ModelTestTaskRepository::validateTaskName(QStringLiteral("name.")).isEmpty());
        QCOMPARE(ModelTestTaskRepository::directoryNameForTask(QStringLiteral("  Task 1  ")), QStringLiteral("Task 1"));
    }

    void createsLoadsRenamesAndRemovesTask()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 class_id = fixture.addClass(QStringLiteral("Cat"), QStringLiteral("normal"));
        ModelStorageService storage(fixture.rootPath());
        QString error;
        QVERIFY2(storage.ensureModelStorage(QStringLiteral("model"), &error), qPrintable(error));

        ModelTestTaskRepository repository(fixture.rootPath());
        repository.setProjectDatabasePath(fixture.projectDatabasePath());
        ModelDatasetSelection selection;
        selection.label_classes.insert({fixture.datasetId(), class_id});
        ModelTestTaskDefinition task;
        QVERIFY2(repository.createTask(QStringLiteral("model"), QStringLiteral("model-uuid"), QStringLiteral("First"),
                                       {{QStringLiteral("inference"),
                                         QVariantMap{{QStringLiteral("threshold"), 0.5}}}},
                                       selection, task, &error),
                 qPrintable(error));
        QVERIFY(task.isValid());
        QCOMPARE(repository.listTasks(QStringLiteral("model"), &error).size(), 1);
        ModelTestTaskDefinition loaded;
        QVERIFY2(repository.loadTask(QStringLiteral("model"), task.uuid, loaded, &error), qPrintable(error));
        QCOMPARE(loaded.name, QStringLiteral("First"));
        QCOMPARE(loaded.test_params.value(QStringLiteral("inference")).toMap().value(QStringLiteral("threshold")).toDouble(),
                 0.5);
        QVERIFY(loaded.dataset_selection.containsLabelClass(fixture.datasetId(), class_id));

        QVERIFY(!repository.createTask(QStringLiteral("model"), QStringLiteral("model-uuid"), QStringLiteral("first"),
                                       {}, {}, loaded, &error));
        QVERIFY(error.contains(QStringLiteral("已存在")));
        error.clear();
        QVERIFY(repository.renameTask(QStringLiteral("model"), task.uuid, QStringLiteral("Renamed"), &error));
        QVERIFY(QDir(storage.testTaskRoot(QStringLiteral("model"), QStringLiteral("Renamed"))).exists());
        QVERIFY(repository.removeTask(QStringLiteral("model"), task.uuid, &error));
        QCOMPARE(repository.listTasks(QStringLiteral("model"), &error).size(), 0);
    }

    void invalidAndMissingTaskOperationsLeaveStorageUntouched()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        ModelStorageService storage(fixture.rootPath());
        QString             error;
        QVERIFY(storage.ensureModelStorage(QStringLiteral("model"), &error));

        ModelTestTaskRepository repository(fixture.rootPath());
        repository.setProjectDatabasePath(fixture.projectDatabasePath());
        ModelTestTaskDefinition output;
        QVERIFY(!repository.createTask(QStringLiteral("model"), QStringLiteral("uuid"), QStringLiteral("."), {},
                                       {}, output, &error));
        QVERIFY(error.contains(QStringLiteral("无效")));
        QVERIFY(!repository.renameTask(QStringLiteral("model"), QStringLiteral("missing"), QStringLiteral("New"),
                                       &error));
        QVERIFY(!repository.removeTask(QStringLiteral("model"), QStringLiteral("missing"), &error));
        QVERIFY(!QDir(storage.testRoot(QStringLiteral("model"))).exists()
                || QDir(storage.testRoot(QStringLiteral("model"))).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
    }
};

REGISTER_TEST(ModelTestTaskRepositoryTest)

#include "test_ModelTestTaskRepository.moc"
