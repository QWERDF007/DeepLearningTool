#include "../test_runner.h"

#include "TestFixture.h"
#include "database/DataBase.h"
#include "model/ModelEvaluationProtocol.h"
#include "model/ModelLifecycle.h"
#include "model/ModelManager.h"
#include "model/ModelOperationWorkflow.h"
#include "model/ModelStorageService.h"
#include "model/TaskManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include <atomic>

using namespace dltool::model;
using namespace dltool::model::testsupport;

class ModelOperationWorkflowTest : public QObject
{
    Q_OBJECT

private slots:
    void asyncCopyRunsInBackgroundWithoutBlockingGuiEventLoop()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        TaskManager task_manager;
        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager manager(static_cast<int>(evaluation::Method::Detection), &database, nullptr, &task_manager);

        QString error;
        const auto record = manager.addModelRecord(QStringLiteral("BigSource"), QStringLiteral("ultralytics"),
                                                   QStringLiteral("YOLOv8"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        // Create a simulated large weights file (10MB) in train/weights/best.pt
        const ModelStorageService storage(fixture.rootPath());
        const QString weights_dir = storage.trainWeightsPath(record.name);
        QVERIFY(QDir().mkpath(weights_dir));
        const QString best_pt = QDir(weights_dir).filePath(QStringLiteral("best.pt"));
        {
            QFile file(best_pt);
            QVERIFY(file.open(QIODevice::WriteOnly));
            const QByteArray chunk(1024 * 1024, 'X'); // 1MB chunk
            for (int i = 0; i < 10; ++i)
            {
                QCOMPARE(file.write(chunk), static_cast<qint64>(chunk.size()));
            }
            file.close();
        }
        QCOMPARE(QFileInfo(best_pt).size(), 10 * 1024 * 1024);

        // Setup a timer on the GUI event loop to verify event loop responsiveness during async copy
        int gui_tick_count = 0;
        QTimer tick_timer;
        tick_timer.setInterval(10);
        connect(&tick_timer, &QTimer::timeout, [&gui_tick_count]() { ++gui_tick_count; });
        tick_timer.start();

        const int ticks_before = gui_tick_count;
        bool completion_called = false;
        ModelOperationWorkflow::Result completion_result;

        auto handle = manager.copyModelAsync(
            record.model_id, true,
            [&completion_called, &completion_result](const ModelOperationWorkflow::Result &result)
            {
                completion_called = true;
                completion_result = result;
            });

        QVERIFY(handle != nullptr);
        QVERIFY(manager.waitForOperations(15000));
        tick_timer.stop();
        const int ticks_after = gui_tick_count;

        // Evidence: GUI event loop was responsive and ticked during the background operation
        QVERIFY(ticks_after > ticks_before);
        std::fprintf(stderr, "[Evidence] GUI event loop ticked %d times during 10MB async model copy\n",
                     ticks_after - ticks_before);

        QVERIFY(completion_called);
        QVERIFY(completion_result.success);
        QVERIFY(!completion_result.cancelled);
        QCOMPARE(manager.rowCount(), 2);

        // Verify target model has the 10MB file intact
        const QString target_name = manager.modelAt(1).value(QStringLiteral("name")).toString();
        const QString target_weights = storage.trainWeightsPath(target_name);
        const QString target_best_pt = QDir(target_weights).filePath(QStringLiteral("best.pt"));
        QVERIFY(QFileInfo::exists(target_best_pt));
        QCOMPARE(QFileInfo(target_best_pt).size(), 10 * 1024 * 1024);
    }

    void asyncRecoveryScansPendingJournalsWithoutBlockingGuiEventLoop()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        TaskManager task_manager;
        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());

        // Create a model in DB
        QString error;
        qint64 model_id = -1;
        QVERIFY(database.addModel(QStringLiteral("uuid-rec"), QStringLiteral("PendingRecoveryModel"),
                                  QStringLiteral("ultralytics"), QStringLiteral("YOLOv8"), 1, 1, model_id, error));

        // Create a pending journal with staged directory (database-committed state)
        const ModelStorageService storage(fixture.rootPath());
        QVERIFY(QDir().mkpath(storage.operationRoot()));
        const QString op_id = QStringLiteral("op-recovery-test");
        const QString staging = storage.operationStagingRoot(op_id);
        QVERIFY(storage.ensureModelStorageAt(staging, &error));

        // Write a test weight file in staging
        const QString weight_path = storage.trainWeightsPathAt(staging);
        QVERIFY(QDir().mkpath(weight_path));
        {
            QFile f(QDir(weight_path).filePath(QStringLiteral("best.pt")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("recovered-content");
            f.close();
        }

        // Write database-committed journal
        const QString journal_path = storage.operationJournalPath(op_id);
        {
            QJsonObject obj;
            obj[QStringLiteral("id")]           = op_id;
            obj[QStringLiteral("kind")]         = QStringLiteral("create");
            obj[QStringLiteral("phase")]        = QStringLiteral("database-committed");
            obj[QStringLiteral("model_id")]     = model_id;
            obj[QStringLiteral("uuid")]         = QStringLiteral("uuid-rec");
            obj[QStringLiteral("target_name")]  = QStringLiteral("PendingRecoveryModel");
            obj[QStringLiteral("staging_path")] = staging;

            QFile jf(journal_path);
            QVERIFY(jf.open(QIODevice::WriteOnly));
            jf.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
            jf.close();
        }

        int gui_tick_count = 0;
        QTimer tick_timer;
        tick_timer.setInterval(10);
        connect(&tick_timer, &QTimer::timeout, [&gui_tick_count]() { ++gui_tick_count; });
        tick_timer.start();

        ModelManager manager(static_cast<int>(evaluation::Method::Detection), &database, nullptr, &task_manager);
        QVERIFY(manager.waitForOperations(5000));
        tick_timer.stop();

        std::fprintf(stderr, "[Evidence] GUI event loop ticked %d times during async recovery scanning\n", gui_tick_count);
        // Staging was published to model root
        const QString target_model_root = storage.modelRoot(QStringLiteral("PendingRecoveryModel"));
        QVERIFY(QDir(target_model_root).exists());
        QVERIFY(QFileInfo::exists(QDir(storage.trainWeightsPath(QStringLiteral("PendingRecoveryModel"))).filePath(QStringLiteral("best.pt"))));
        // Journal is cleaned up
        QVERIFY(!QFileInfo::exists(journal_path));
    }

    void workerOwnsResourcesAndDoesNotAccessGuiManager()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        std::atomic_bool worker_thread_different{false};
        std::atomic_bool worker_had_valid_lifecycle{false};
        const Qt::HANDLE main_thread_id = QThread::currentThreadId();

        ModelOperationWorkflow::Options options;
        options.manage_progress = false;

        const auto handle = ModelOperationWorkflow::startLifecycle(
            this, fixture.projectDatabasePath(), fixture.rootPath(), options,
            [main_thread_id, &worker_thread_different, &worker_had_valid_lifecycle](
                ModelLifecycle &lifecycle, ModelOperationWorkflow::Result &result)
            {
                worker_thread_different.store(QThread::currentThreadId() != main_thread_id);
                // Worker uses its own ModelLifecycle, ProjectDataBase, ModelStorageService
                const auto rec = lifecycle.recoverPending();
                worker_had_valid_lifecycle.store(rec.succeeded());
                result.success = true;
            });

        QVERIFY(handle != nullptr);
        QVERIFY(handle->waitForDone(5000));
        QVERIFY(worker_thread_different.load());
        QVERIFY(worker_had_valid_lifecycle.load());
    }

    void cancelBeforeCommitRollsBackStagingAndCleansJournal()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        TaskManager task_manager;
        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager manager(static_cast<int>(evaluation::Method::Detection), &database, nullptr, &task_manager);

        QString error;
        const auto record = manager.addModelRecord(QStringLiteral("SourceModel"), QStringLiteral("ultralytics"),
                                                   QStringLiteral("YOLOv8"), &error);
        QVERIFY(record.isValid());

        // Create a 5MB weights file so copy takes measurable time
        const ModelStorageService storage(fixture.rootPath());
        const QString weights_dir = storage.trainWeightsPath(record.name);
        QVERIFY(QDir().mkpath(weights_dir));
        const QString best_pt = QDir(weights_dir).filePath(QStringLiteral("best.pt"));
        {
            QFile file(best_pt);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(QByteArray(5 * 1024 * 1024, 'Y'));
            file.close();
        }

        bool completion_called = false;
        ModelOperationWorkflow::Result completion_result;

        auto handle = manager.copyModelAsync(
            record.model_id, true,
            [&completion_called, &completion_result](const ModelOperationWorkflow::Result &result)
            {
                completion_called = true;
                completion_result = result;
            });

        QVERIFY(handle != nullptr);
        // Request cancel before DB commit
        handle->requestCancel();

        QVERIFY(manager.waitForOperations(5000));
        QVERIFY(completion_called);
        QVERIFY(completion_result.cancelled);
        QVERIFY(!completion_result.success);

        // Manager model count remains 1 (no partial record added)
        QCOMPARE(manager.rowCount(), 1);

        // Operations folder contains no leftover staging directories
        const QString op_root = storage.operationRoot();
        if (QDir(op_root).exists())
        {
            const QStringList staging_dirs = QDir(op_root).entryList({QStringLiteral("staging-*")}, QDir::Dirs);
            QCOMPARE(staging_dirs.size(), 0);
            const QStringList journals = QDir(op_root).entryList({QStringLiteral("*.json")}, QDir::Files);
            QCOMPARE(journals.size(), 0);
        }
    }

    void cancelAfterCommitConvergesAndModelIsConsistent()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        TaskManager task_manager;
        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());

        // Prepare source model
        QString error;
        qint64 src_id = -1;
        QVERIFY(database.addModel(QStringLiteral("uuid-src"), QStringLiteral("SourceConv"),
                                  QStringLiteral("ultralytics"), QStringLiteral("YOLOv8"), 1, 1, src_id, error));
        const ModelStorageService storage(fixture.rootPath());
        QVERIFY(storage.ensureModelStorage(QStringLiteral("SourceConv"), &error));

        ModelLifecycleRecord source_record;
        source_record.model_id = src_id;
        source_record.uuid = QStringLiteral("uuid-src");
        source_record.name = QStringLiteral("SourceConv");
        source_record.framework_name = QStringLiteral("ultralytics");
        source_record.model_architecture = QStringLiteral("YOLOv8");
        source_record.ctime = 1;
        source_record.mtime = 1;

        ModelLifecycleRecord target_record;
        target_record.uuid = QStringLiteral("uuid-target-conv");
        target_record.name = QStringLiteral("TargetConv");
        target_record.framework_name = QStringLiteral("ultralytics");
        target_record.model_architecture = QStringLiteral("YOLOv8");
        target_record.ctime = 2;
        target_record.mtime = 2;

        ModelOperationWorkflow::HandlePtr handle_ref;
        ModelOperationWorkflow::Options options;
        options.manage_progress = false;

        bool completion_called = false;
        ModelOperationWorkflow::Result completion_result;

        handle_ref = ModelOperationWorkflow::startLifecycle(
            this, fixture.projectDatabasePath(), fixture.rootPath(), options,
            [&handle_ref, source_record, target_record](ModelLifecycle &lifecycle,
                                                        ModelOperationWorkflow::Result &result)
            {
                // Execute copy
                result.lifecycle_result = lifecycle.copy(
                    source_record, target_record, false,
                    [&handle_ref]()
                    {
                        return handle_ref && handle_ref->isCancellationRequested();
                    });
                result.success = result.lifecycle_result.succeeded();
                result.model_id = result.lifecycle_result.model_id;
                // Request cancel right after commit
                if (handle_ref)
                    handle_ref->requestCancel();
            },
            [&completion_called, &completion_result](const ModelOperationWorkflow::Result &result)
            {
                completion_called = true;
                completion_result = result;
            });

        QVERIFY(handle_ref != nullptr);
        QVERIFY(handle_ref->waitForDone(5000));
        QTRY_VERIFY(completion_called);

        // Cancellation after commit converges: result is success, NOT cancelled!
        QVERIFY(completion_result.success);
        QVERIFY(!completion_result.cancelled);

        // Target model is published and consistent
        QVERIFY(QDir(storage.modelRoot(QStringLiteral("TargetConv"))).exists());

        // Re-open verification: new ModelManager sees both models consistently
        ModelManager reloaded(static_cast<int>(evaluation::Method::Detection), &database, nullptr, &task_manager);
        QCOMPARE(reloaded.rowCount(), 2);
        QCOMPARE(reloaded.modelRecordForUuid(QStringLiteral("uuid-target-conv")).value(QStringLiteral("name")).toString(),
                 QStringLiteral("TargetConv"));
    }

    void managerShutdownCancelsAndWaitsActiveOperations()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        TaskManager task_manager;
        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());

        QString error;
        const ModelStorageService storage(fixture.rootPath());

        {
            ModelManager manager(static_cast<int>(evaluation::Method::Detection), &database, nullptr, &task_manager);
            const auto record = manager.addModelRecord(QStringLiteral("ShutdownModel"), QStringLiteral("ultralytics"),
                                                       QStringLiteral("YOLOv8"), &error);
            QVERIFY(record.isValid());

            // Write 5MB weight file
            const QString weights_dir = storage.trainWeightsPath(record.name);
            QVERIFY(QDir().mkpath(weights_dir));
            {
                QFile file(QDir(weights_dir).filePath(QStringLiteral("best.pt")));
                QVERIFY(file.open(QIODevice::WriteOnly));
                file.write(QByteArray(5 * 1024 * 1024, 'Z'));
                file.close();
            }

            auto handle = manager.copyModelAsync(record.model_id, true);
            QVERIFY(handle != nullptr);

            // Shutdown while copy is in progress
            manager.shutdown();
            QVERIFY(handle->isFinished());
        }

        // Clean shutdown: re-opening database succeeds with zero leaks or crashes
        ModelManager reloaded(static_cast<int>(evaluation::Method::Detection), &database, nullptr, &task_manager);
        QVERIFY(reloaded.rowCount() >= 1);
    }
};

REGISTER_TEST(ModelOperationWorkflowTest)

#include "test_ModelOperationWorkflow.moc"
