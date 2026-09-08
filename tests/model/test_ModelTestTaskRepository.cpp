#include "../test_runner.h"

#include "TestFixture.h"

#include "model/ModelStorageService.h"
#include "model/ModelEvaluationProtocol.h"
#include "model/ModelTestTaskRepository.h"
#include "model/ModelManager.h"
#include "model/ModelTestTaskManager.h"
#include "model/TaskManager.h"
#include "database/DataBase.h"
#include "database/ModelDataBase.h"
#include "database/ModelTaskDataBase.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QUuid>

using namespace dltool::model;
using namespace dltool::model::testsupport;

namespace {

bool writeSimulatedJournal(const QString &path, const QJsonObject &obj)
{
    const QString dir = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(dir))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    file.close();
    return true;
}

} // namespace

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

    void parameterSaveDoesNotOverwriteCommittedDatasetSelection()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 class_id = fixture.addClass(QStringLiteral("Cat"), QStringLiteral("normal"));
        QVERIFY(class_id >= 0);

        ModelTestTaskRepository repository(fixture.rootPath());
        repository.setProjectDatabasePath(fixture.projectDatabasePath());
        ModelDatasetSelection selection;
        selection.label_classes.insert({fixture.datasetId(), class_id});
        ModelTestTaskDefinition task;
        QString error;
        QVERIFY2(repository.createTask(QStringLiteral("model"), QStringLiteral("model-uuid"), QStringLiteral("Task"),
                                       {{QStringLiteral("evaluation"),
                                         QVariantMap{{QStringLiteral("conf"), 0.5}}}},
                                       selection, task, &error),
                 qPrintable(error));

        ModelTestTaskDefinition parameters_only = task;
        parameters_only.test_params[QStringLiteral("evaluation")]
            = QVariantMap{{QStringLiteral("conf"), 0.8}};
        parameters_only.dataset_selection = {};
        QVERIFY2(repository.saveTask(QStringLiteral("model"), parameters_only, false, &error), qPrintable(error));

        ModelTestTaskDefinition loaded;
        QVERIFY2(repository.loadTask(QStringLiteral("model"), task.uuid, loaded, &error), qPrintable(error));
        QCOMPARE(loaded.test_params.value(QStringLiteral("evaluation")).toMap().value(QStringLiteral("conf")).toDouble(),
                 0.8);
        QVERIFY(loaded.dataset_selection.containsLabelClass(fixture.datasetId(), class_id));
    }

    void createInterruptedBeforeCommitCleansStaging()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        ModelStorageService storage(fixture.rootPath());
        QString error;
        QVERIFY(storage.ensureModelStorage(QStringLiteral("model"), &error));

        const QString op_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString staging_path = storage.testTaskOperationStagingRoot(QStringLiteral("model"), op_id);
        QVERIFY(storage.ensureTestTaskStorageAt(staging_path, &error));

        const QString dummy_file = QDir(staging_path).filePath(QStringLiteral("test.txt"));
        {
            QFile f(dummy_file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("staging data");
            f.close();
        }

        const QString journal_path = storage.testTaskOperationJournalPath(QStringLiteral("model"), op_id);
        QJsonObject journal;
        journal[QStringLiteral("id")] = op_id;
        journal[QStringLiteral("kind")] = QStringLiteral("create");
        journal[QStringLiteral("phase")] = QStringLiteral("staged");
        journal[QStringLiteral("model_name")] = QStringLiteral("model");
        journal[QStringLiteral("task_uuid")] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        journal[QStringLiteral("target_name")] = QStringLiteral("IncompleteCreate");
        journal[QStringLiteral("target_directory")] = QStringLiteral("IncompleteCreate");
        journal[QStringLiteral("staging_path")] = staging_path;
        QVERIFY(writeSimulatedJournal(journal_path, journal));

        ModelTestTaskRepository repository(fixture.rootPath());
        repository.setProjectDatabasePath(fixture.projectDatabasePath());

        QVERIFY(repository.recoverPending(QStringLiteral("model"), &error));
        QVERIFY(!QDir(staging_path).exists());
        QVERIFY(!QFileInfo::exists(journal_path));
        QCOMPARE(repository.listTasks(QStringLiteral("model")).size(), 0);
    }

    void createInterruptedAfterCommitPublishesTargetAndRestoresTask()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        ModelStorageService storage(fixture.rootPath());
        QString error;
        QVERIFY(storage.ensureModelStorage(QStringLiteral("model"), &error));

        const QString op_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString task_uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString staging_path = storage.testTaskOperationStagingRoot(QStringLiteral("model"), op_id);
        QVERIFY(storage.ensureTestTaskStorageAt(staging_path, &error));

        // Write task.db inside staging_path
        const QString staging_db_path = QDir(staging_path).filePath(QStringLiteral("task.db"));
        {
            dltool::database::ModelTaskDataBase task_db(staging_db_path);
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            QVERIFY(task_db.upsertTaskInfo({task_uuid, now, now}));
            QVERIFY(task_db.replaceTestParams({{QStringLiteral("evaluation"),
                                                QVariantMap{{QStringLiteral("conf"), 0.7}}}}));
        }

        // Write prediction file inside staging predictions
        const QString pred_file = QDir(storage.testTaskPredictionPath(QStringLiteral("model"),
                                                                      QStringLiteral("TaskAfterCommit")))
                                      .relativeFilePath(storage.testTaskPredictionPath(QStringLiteral("model"),
                                                                                       QStringLiteral("TaskAfterCommit")));
        const QString staging_pred = QDir(staging_path).filePath(QStringLiteral("predictions/pred1.json"));
        {
            QFile f(staging_pred);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("{\"score\":0.9}");
            f.close();
        }

        // Commit to model.db
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        dltool::database::ModelDataBase model_db(storage.modelDatabasePath(QStringLiteral("model")));
        QVERIFY(model_db.upsertTestTask({task_uuid, QStringLiteral("TaskAfterCommit"), now, now}));

        const QString journal_path = storage.testTaskOperationJournalPath(QStringLiteral("model"), op_id);
        QJsonObject journal;
        journal[QStringLiteral("id")] = op_id;
        journal[QStringLiteral("kind")] = QStringLiteral("create");
        journal[QStringLiteral("phase")] = QStringLiteral("database-committed");
        journal[QStringLiteral("model_name")] = QStringLiteral("model");
        journal[QStringLiteral("task_uuid")] = task_uuid;
        journal[QStringLiteral("target_name")] = QStringLiteral("TaskAfterCommit");
        journal[QStringLiteral("target_directory")] = QStringLiteral("TaskAfterCommit");
        journal[QStringLiteral("staging_path")] = staging_path;
        QVERIFY(writeSimulatedJournal(journal_path, journal));

        const QString target_root = storage.testTaskRoot(QStringLiteral("model"), QStringLiteral("TaskAfterCommit"));
        QVERIFY(!QDir(target_root).exists());

        ModelTestTaskRepository repository(fixture.rootPath());
        repository.setProjectDatabasePath(fixture.projectDatabasePath());

        // listTasks triggers recovery
        const auto tasks = repository.listTasks(QStringLiteral("model"), &error);
        QCOMPARE(tasks.size(), 1);
        QCOMPARE(tasks[0].name, QStringLiteral("TaskAfterCommit"));
        QVERIFY(QDir(target_root).exists());
        QVERIFY(QFileInfo::exists(QDir(target_root).filePath(QStringLiteral("predictions/pred1.json"))));
        QVERIFY(!QFileInfo::exists(journal_path));
        QVERIFY(!QDir(staging_path).exists());

        ModelTestTaskDefinition loaded;
        QVERIFY(repository.loadTask(QStringLiteral("model"), task_uuid, loaded));
        QCOMPARE(loaded.test_params.value(QStringLiteral("evaluation")).toMap().value(QStringLiteral("conf")).toDouble(),
                 0.7);
    }

    void renameInterruptedBeforeCommitRollsBackDirectoryPreservingPredictions()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        ModelStorageService storage(fixture.rootPath());
        QString error;
        QVERIFY(storage.ensureModelStorage(QStringLiteral("model"), &error));

        ModelTestTaskRepository repository(fixture.rootPath());
        repository.setProjectDatabasePath(fixture.projectDatabasePath());

        ModelTestTaskDefinition task;
        QVERIFY(repository.createTask(QStringLiteral("model"), QStringLiteral("model-uuid"), QStringLiteral("OriginalTask"),
                                      {}, {}, task, &error));

        // Write predictions to OriginalTask
        const QString orig_pred = storage.testTaskPredictionPath(QStringLiteral("model"), QStringLiteral("OriginalTask"));
        const QString pred_file = QDir(orig_pred).filePath(QStringLiteral("pred.json"));
        {
            QFile f(pred_file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("original predictions");
            f.close();
        }

        // Simulate interrupted rename: directory moved to RenamedTask, but model.db not yet updated
        const QString orig_root = storage.testTaskRoot(QStringLiteral("model"), QStringLiteral("OriginalTask"));
        const QString renamed_root = storage.testTaskRoot(QStringLiteral("model"), QStringLiteral("RenamedTask"));
        QVERIFY(QDir(storage.testRoot(QStringLiteral("model"))).rename(QStringLiteral("OriginalTask"), QStringLiteral("RenamedTask")));
        QVERIFY(!QDir(orig_root).exists());
        QVERIFY(QDir(renamed_root).exists());

        const QString op_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString journal_path = storage.testTaskOperationJournalPath(QStringLiteral("model"), op_id);
        QJsonObject journal;
        journal[QStringLiteral("id")] = op_id;
        journal[QStringLiteral("kind")] = QStringLiteral("rename");
        journal[QStringLiteral("phase")] = QStringLiteral("directory-moved");
        journal[QStringLiteral("model_name")] = QStringLiteral("model");
        journal[QStringLiteral("task_uuid")] = task.uuid;
        journal[QStringLiteral("source_name")] = QStringLiteral("OriginalTask");
        journal[QStringLiteral("source_directory")] = QStringLiteral("OriginalTask");
        journal[QStringLiteral("target_name")] = QStringLiteral("RenamedTask");
        journal[QStringLiteral("target_directory")] = QStringLiteral("RenamedTask");
        QVERIFY(writeSimulatedJournal(journal_path, journal));

        // Recovery rolls back to OriginalTask because database still has OriginalTask
        QVERIFY(repository.recoverPending(QStringLiteral("model"), &error));
        QVERIFY(QDir(orig_root).exists());
        QVERIFY(!QDir(renamed_root).exists());
        QVERIFY(QFileInfo::exists(pred_file));
        QVERIFY(!QFileInfo::exists(journal_path));

        ModelTestTaskDefinition loaded;
        QVERIFY(repository.loadTask(QStringLiteral("model"), task.uuid, loaded));
        QCOMPARE(loaded.name, QStringLiteral("OriginalTask"));
    }

    void renameInterruptedAfterCommitConvergesToNewDirectory()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        ModelStorageService storage(fixture.rootPath());
        QString error;
        QVERIFY(storage.ensureModelStorage(QStringLiteral("model"), &error));

        ModelTestTaskRepository repository(fixture.rootPath());
        repository.setProjectDatabasePath(fixture.projectDatabasePath());

        ModelTestTaskDefinition task;
        QVERIFY(repository.createTask(QStringLiteral("model"), QStringLiteral("model-uuid"), QStringLiteral("PreCommitTask"),
                                      {}, {}, task, &error));

        const QString orig_pred = storage.testTaskPredictionPath(QStringLiteral("model"), QStringLiteral("PreCommitTask"));
        const QString pred_file = QDir(orig_pred).filePath(QStringLiteral("pred.json"));
        {
            QFile f(pred_file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("predictions to preserve");
            f.close();
        }

        // Update database to PostCommitTask, but directory is still PreCommitTask
        dltool::database::ModelDataBase model_db(storage.modelDatabasePath(QStringLiteral("model")));
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        QVERIFY(model_db.upsertTestTask({task.uuid, QStringLiteral("PostCommitTask"), task.created_at, now}));

        const QString orig_root = storage.testTaskRoot(QStringLiteral("model"), QStringLiteral("PreCommitTask"));
        const QString target_root = storage.testTaskRoot(QStringLiteral("model"), QStringLiteral("PostCommitTask"));

        const QString op_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString journal_path = storage.testTaskOperationJournalPath(QStringLiteral("model"), op_id);
        QJsonObject journal;
        journal[QStringLiteral("id")] = op_id;
        journal[QStringLiteral("kind")] = QStringLiteral("rename");
        journal[QStringLiteral("phase")] = QStringLiteral("database-committed");
        journal[QStringLiteral("model_name")] = QStringLiteral("model");
        journal[QStringLiteral("task_uuid")] = task.uuid;
        journal[QStringLiteral("source_name")] = QStringLiteral("PreCommitTask");
        journal[QStringLiteral("source_directory")] = QStringLiteral("PreCommitTask");
        journal[QStringLiteral("target_name")] = QStringLiteral("PostCommitTask");
        journal[QStringLiteral("target_directory")] = QStringLiteral("PostCommitTask");
        QVERIFY(writeSimulatedJournal(journal_path, journal));

        // Recovery converges to PostCommitTask
        QVERIFY(repository.recoverPending(QStringLiteral("model"), &error));
        QVERIFY(!QDir(orig_root).exists());
        QVERIFY(QDir(target_root).exists());
        const QString target_pred_file = QDir(storage.testTaskPredictionPath(QStringLiteral("model"),
                                                                             QStringLiteral("PostCommitTask")))
                                             .filePath(QStringLiteral("pred.json"));
        QVERIFY(QFileInfo::exists(target_pred_file));
        QVERIFY(!QFileInfo::exists(journal_path));

        ModelTestTaskDefinition loaded;
        QVERIFY(repository.loadTask(QStringLiteral("model"), task.uuid, loaded));
        QCOMPARE(loaded.name, QStringLiteral("PostCommitTask"));
    }

    void renameTargetConflictDuringRecoveryPreservesBothTasksWithoutOverwriting()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        ModelStorageService storage(fixture.rootPath());
        QString error;
        QVERIFY(storage.ensureModelStorage(QStringLiteral("model"), &error));

        ModelTestTaskRepository repository(fixture.rootPath());
        repository.setProjectDatabasePath(fixture.projectDatabasePath());

        // Create TaskA
        ModelTestTaskDefinition task_a;
        QVERIFY(repository.createTask(QStringLiteral("model"), QStringLiteral("model-uuid"), QStringLiteral("TaskA"),
                                      {}, {}, task_a, &error));
        const QString pred_a = QDir(storage.testTaskPredictionPath(QStringLiteral("model"), QStringLiteral("TaskA")))
                                   .filePath(QStringLiteral("pred_a.json"));
        {
            QFile f(pred_a);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("task A predictions");
            f.close();
        }

        // Simulate rename TaskA -> TaskB interrupted before commit (dir moved to TaskB, db has TaskA)
        const QString root_a = storage.testTaskRoot(QStringLiteral("model"), QStringLiteral("TaskA"));
        const QString root_b = storage.testTaskRoot(QStringLiteral("model"), QStringLiteral("TaskB"));
        QVERIFY(QDir(storage.testRoot(QStringLiteral("model"))).rename(QStringLiteral("TaskA"), QStringLiteral("TaskB")));

        // Now create a conflicting TaskA on disk with a DIFFERENT task UUID
        const QString conflict_uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(storage.ensureTestTaskStorage(QStringLiteral("model"), QStringLiteral("TaskA")));
        {
            dltool::database::ModelTaskDataBase conflict_db(storage.testTaskDatabasePath(QStringLiteral("model"), QStringLiteral("TaskA")));
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            QVERIFY(conflict_db.upsertTaskInfo({conflict_uuid, now, now}));
        }
        const QString pred_conflict = QDir(storage.testTaskPredictionPath(QStringLiteral("model"), QStringLiteral("TaskA")))
                                          .filePath(QStringLiteral("conflict.json"));
        {
            QFile f(pred_conflict);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("conflicting predictions");
            f.close();
        }

        const QString op_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString journal_path = storage.testTaskOperationJournalPath(QStringLiteral("model"), op_id);
        QJsonObject journal;
        journal[QStringLiteral("id")] = op_id;
        journal[QStringLiteral("kind")] = QStringLiteral("rename");
        journal[QStringLiteral("phase")] = QStringLiteral("directory-moved");
        journal[QStringLiteral("model_name")] = QStringLiteral("model");
        journal[QStringLiteral("task_uuid")] = task_a.uuid;
        journal[QStringLiteral("source_name")] = QStringLiteral("TaskA");
        journal[QStringLiteral("source_directory")] = QStringLiteral("TaskA");
        journal[QStringLiteral("target_name")] = QStringLiteral("TaskB");
        journal[QStringLiteral("target_directory")] = QStringLiteral("TaskB");
        QVERIFY(writeSimulatedJournal(journal_path, journal));

        // Recovery detects conflict at TaskA: MUST NOT overwrite TaskA, MUST keep TaskB and retain journal
        QVERIFY(!repository.recoverPending(QStringLiteral("model"), &error));
        QVERIFY(error.contains(QStringLiteral("冲突")) || error.contains(QStringLiteral("已存在")));
        QVERIFY(QDir(root_b).exists());
        QVERIFY(QDir(root_a).exists());
        QVERIFY(QFileInfo::exists(pred_conflict));
        QVERIFY(QFileInfo::exists(QDir(storage.testTaskPredictionPath(QStringLiteral("model"), QStringLiteral("TaskB"))).filePath(QStringLiteral("pred_a.json"))));
        QVERIFY(QFileInfo::exists(journal_path)); // Journal retained for recovery evidence
    }

    void removeInterruptedBeforeCommitRestoresTaskAndPredictions()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        ModelStorageService storage(fixture.rootPath());
        QString error;
        QVERIFY(storage.ensureModelStorage(QStringLiteral("model"), &error));

        ModelTestTaskRepository repository(fixture.rootPath());
        repository.setProjectDatabasePath(fixture.projectDatabasePath());

        ModelTestTaskDefinition task;
        QVERIFY(repository.createTask(QStringLiteral("model"), QStringLiteral("model-uuid"), QStringLiteral("WillDelete"),
                                      {}, {}, task, &error));

        const QString pred_file = QDir(storage.testTaskPredictionPath(QStringLiteral("model"), QStringLiteral("WillDelete")))
                                      .filePath(QStringLiteral("pred.json"));
        {
            QFile f(pred_file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("important predictions");
            f.close();
        }

        // Simulate move to quarantine before database commit
        const QString op_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString quarantine_path = storage.testTaskOperationQuarantineRoot(QStringLiteral("model"), op_id);
        const QString task_root = storage.testTaskRoot(QStringLiteral("model"), QStringLiteral("WillDelete"));
        QVERIFY(storage.moveDirectory(task_root, quarantine_path));
        QVERIFY(!QDir(task_root).exists());

        const QString journal_path = storage.testTaskOperationJournalPath(QStringLiteral("model"), op_id);
        QJsonObject journal;
        journal[QStringLiteral("id")] = op_id;
        journal[QStringLiteral("kind")] = QStringLiteral("remove");
        journal[QStringLiteral("phase")] = QStringLiteral("quarantined");
        journal[QStringLiteral("model_name")] = QStringLiteral("model");
        journal[QStringLiteral("task_uuid")] = task.uuid;
        journal[QStringLiteral("source_name")] = QStringLiteral("WillDelete");
        journal[QStringLiteral("source_directory")] = QStringLiteral("WillDelete");
        journal[QStringLiteral("quarantine_path")] = quarantine_path;
        QVERIFY(writeSimulatedJournal(journal_path, journal));

        // Recovery should restore from quarantine back to task_root since database still has the record
        QVERIFY(repository.recoverPending(QStringLiteral("model"), &error));
        QVERIFY(QDir(task_root).exists());
        QVERIFY(!QDir(quarantine_path).exists());
        QVERIFY(QFileInfo::exists(pred_file));
        QVERIFY(!QFileInfo::exists(journal_path));

        const auto tasks = repository.listTasks(QStringLiteral("model"));
        QCOMPARE(tasks.size(), 1);
        QCOMPARE(tasks[0].name, QStringLiteral("WillDelete"));
    }

    void removeInterruptedAfterCommitCleansQuarantineAndRetriesOnLock()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        ModelStorageService storage(fixture.rootPath());
        QString error;
        QVERIFY(storage.ensureModelStorage(QStringLiteral("model"), &error));

        ModelTestTaskRepository repository(fixture.rootPath());
        repository.setProjectDatabasePath(fixture.projectDatabasePath());

        const QString op_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString task_uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString quarantine_path = storage.testTaskOperationQuarantineRoot(QStringLiteral("model"), op_id);
        QVERIFY(storage.ensureTestTaskStorageAt(quarantine_path, &error));

        const QString locked_file_path = QDir(quarantine_path).filePath(QStringLiteral("locked.txt"));
        QFile locked_file(locked_file_path);
        QVERIFY(locked_file.open(QIODevice::ReadWrite));
        locked_file.write("locked content");
        locked_file.flush();

        const QString journal_path = storage.testTaskOperationJournalPath(QStringLiteral("model"), op_id);
        QJsonObject journal;
        journal[QStringLiteral("id")] = op_id;
        journal[QStringLiteral("kind")] = QStringLiteral("remove");
        journal[QStringLiteral("phase")] = QStringLiteral("database-committed");
        journal[QStringLiteral("model_name")] = QStringLiteral("model");
        journal[QStringLiteral("task_uuid")] = task_uuid;
        journal[QStringLiteral("source_name")] = QStringLiteral("CommittedRemove");
        journal[QStringLiteral("source_directory")] = QStringLiteral("CommittedRemove");
        journal[QStringLiteral("quarantine_path")] = quarantine_path;
        QVERIFY(writeSimulatedJournal(journal_path, journal));

        // With locked file, quarantine deletion should fail, keeping journal for retry
        const bool first_try = repository.recoverPending(QStringLiteral("model"), &error);
        QVERIFY(!first_try);
        QVERIFY(QDir(quarantine_path).exists());
        QVERIFY(QFileInfo::exists(journal_path));

        // Unlock file
        locked_file.close();

        // Second try: should clean up quarantine completely and delete journal
        QVERIFY(repository.recoverPending(QStringLiteral("model"), &error));
        QVERIFY(!QDir(quarantine_path).exists());
        QVERIFY(!QFileInfo::exists(journal_path));
        QCOMPARE(repository.listTasks(QStringLiteral("model")).size(), 0);
    }

    void testTaskManagerPublicEntryRecoversAndExposesPredictionsOnReopen()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 cat = fixture.addClass(QStringLiteral("Cat"), QStringLiteral("normal"));
        const qint64 img = fixture.addImage(QStringLiteral("cat_sample"));
        QVERIFY(cat >= 0 && img >= 0);
        QVERIFY(fixture.addDetectionLabel(img, cat, 0, 0, 10, 10) >= 0);
        QVERIFY(fixture.writeImageList());

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager model_manager(static_cast<int>(evaluation::Method::Detection), &database, nullptr);
        QString error;
        const auto record = model_manager.addModelRecord(QStringLiteral("EvalModel"), QStringLiteral("ultralytics"),
                                                         QStringLiteral("YOLOv8"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        ModelStorageService storage(fixture.rootPath());
        TaskManager task_manager;
        {
            ModelTestTaskManager manager(fixture.rootPath(), &model_manager, nullptr, &task_manager);
            manager.setModelUuid(record.uuid);
            QCOMPARE(manager.count(), 1);
            QCOMPARE(manager.currentTaskName(), QStringLiteral("测试 1"));

            // Write prediction into manager's task directory
            const QString task_file_list = storage.testTaskFileListPath(QStringLiteral("EvalModel"), QStringLiteral("测试 1"));
            QVERIFY(QFile::copy(fixture.fileListPath(), task_file_list));

            const QString task_db_path = storage.testTaskDatabasePath(QStringLiteral("EvalModel"), QStringLiteral("测试 1"));
            {
                dltool::database::ModelTaskDataBase task_db(task_db_path);
                QVERIFY(task_db.upsertPrediction({img, detectionPrediction(static_cast<int>(cat), QStringLiteral("Cat"), 0.95, 0, 0, 10, 10)}));
            }

            const QString pred_dir = storage.testTaskPredictionPath(QStringLiteral("EvalModel"), QStringLiteral("测试 1"));
            QVERIFY(QDir().mkpath(pred_dir));
            const QString pred_file = QDir(pred_dir).filePath(QStringLiteral("%1.json").arg(img));
            {
                QFile f(pred_file);
                QVERIFY(f.open(QIODevice::WriteOnly));
                f.write(QJsonDocument::fromVariant(detectionPrediction(static_cast<int>(cat), QStringLiteral("Cat"), 0.95, 0, 0, 10, 10)).toJson());
                f.close();
            }
        }

        // Simulate interrupted rename: directory moved to "测试 1 重命名", but database not updated
        const QString orig_root = storage.testTaskRoot(QStringLiteral("EvalModel"), QStringLiteral("测试 1"));
        const QString new_root = storage.testTaskRoot(QStringLiteral("EvalModel"), QStringLiteral("测试 1 重命名"));
        QVERIFY(storage.moveDirectory(orig_root, new_root));

        const QString op_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString journal_path = storage.testTaskOperationJournalPath(QStringLiteral("EvalModel"), op_id);
        const QString task_uuid = ModelTestTaskRepository(fixture.rootPath()).listTasks(QStringLiteral("EvalModel"))[0].uuid;
        QJsonObject journal;
        journal[QStringLiteral("id")] = op_id;
        journal[QStringLiteral("kind")] = QStringLiteral("rename");
        journal[QStringLiteral("phase")] = QStringLiteral("directory-moved");
        journal[QStringLiteral("model_name")] = QStringLiteral("EvalModel");
        journal[QStringLiteral("task_uuid")] = task_uuid;
        journal[QStringLiteral("source_name")] = QStringLiteral("测试 1");
        journal[QStringLiteral("source_directory")] = QStringLiteral("测试 1");
        journal[QStringLiteral("target_name")] = QStringLiteral("测试 1 重命名");
        journal[QStringLiteral("target_directory")] = QStringLiteral("测试 1 重命名");
        QVERIFY(writeSimulatedJournal(journal_path, journal));

        // Reopen ModelTestTaskManager on that model: setModelUuid triggers reload -> listTasks -> recoverPending
        ModelTestTaskManager reopened(fixture.rootPath(), &model_manager, nullptr, &task_manager);
        reopened.setModelUuid(record.uuid);

        QCOMPARE(reopened.count(), 1);
        QCOMPARE(reopened.currentTaskName(), QStringLiteral("测试 1"));
        QCOMPARE(reopened.currentTaskDirectory(), QStringLiteral("测试 1"));
        QVERIFY(QDir(orig_root).exists());
        QVERIFY(!QDir(new_root).exists());
        QVERIFY(!QFileInfo::exists(journal_path));

        // Verify prediction is accessible to evaluation
        auto *evaluation = reopened.currentEvaluation();
        QVERIFY(evaluation != nullptr);
        QVERIFY(reopened.commitCurrentDatasetSelection());
        reopened.flush();
        QVERIFY(evaluation->hasPredictionResults());
    }
};

REGISTER_TEST(ModelTestTaskRepositoryTest)

#include "test_ModelTestTaskRepository.moc"
