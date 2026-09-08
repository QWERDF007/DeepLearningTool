#include "../test_runner.h"

#include "TestFixture.h"
#include "database/DataBase.h"
#include "database/ModelDataBase.h"
#include "model/ModelLifecycle.h"
#include "model/ModelStorageService.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace dltool::model;

namespace {

ModelLifecycleRecord makeRecord(const QString &name)
{
    ModelLifecycleRecord record;
    record.uuid                = QStringLiteral("uuid-%1").arg(name);
    record.name                = name;
    record.framework_name      = QStringLiteral("ultralytics");
    record.model_architecture  = QStringLiteral("YOLOv8");
    record.ctime               = 1;
    record.mtime               = 1;
    return record;
}

class MemoryModelRecordStore final : public IModelRecordStore
{
public:
    bool fail_add{false};
    bool fail_rename{false};
    bool fail_delete{false};

    bool addModel(const ModelLifecycleRecord &record, qint64 &model_id, QString &error) override
    {
        if (fail_add)
        {
            error = QStringLiteral("注入的模型记录创建失败");
            return false;
        }
        ModelLifecycleRecord copy = record;
        copy.model_id             = next_id++;
        records.insert(copy.model_id, copy);
        model_id = copy.model_id;
        return true;
    }

    bool updateModelName(const qint64 model_id, const QString &name, const qint64 mtime,
                         QString &error) override
    {
        if (fail_rename)
        {
            error = QStringLiteral("注入的模型重命名失败");
            return false;
        }
        auto found = records.find(model_id);
        if (found == records.end())
        {
            error = QStringLiteral("模型记录不存在");
            return false;
        }
        found->name  = name;
        found->mtime = mtime;
        return true;
    }

    bool deleteModel(const qint64 model_id, QString &error) override
    {
        if (fail_delete)
        {
            error = QStringLiteral("注入的模型删除失败");
            return false;
        }
        if (!records.remove(model_id))
        {
            error = QStringLiteral("模型记录不存在");
            return false;
        }
        return true;
    }

    bool findModel(const qint64 model_id, ModelLifecycleRecord &record, bool &exists, QString &) const override
    {
        const auto found = records.constFind(model_id);
        exists           = found != records.cend();
        if (exists)
            record = found.value();
        return true;
    }

    bool findModelByUuid(const QString &uuid, ModelLifecycleRecord &record, bool &exists, QString &) const override
    {
        for (auto it = records.cbegin(); it != records.cend(); ++it)
        {
            if (it.value().uuid == uuid)
            {
                record = it.value();
                exists = true;
                return true;
            }
        }
        exists = false;
        return true;
    }

    QHash<qint64, ModelLifecycleRecord> records;

private:
    qint64 next_id{1};
};

class TestStorageAdapter final : public IModelStorageAdapter
{
public:
    explicit TestStorageAdapter(const QString &root)
        : service(root)
    {
    }

    bool fail_publish{false};
    bool fail_cleanup{false};
    int  move_calls{0};
    int  fail_publish_call_index{-1};

    QString modelsRootPath() const override { return service.modelsRootPath(); }
    QString modelRoot(const QString &name) const override { return service.modelRoot(name); }
    QString modelDatabasePath(const QString &name) const override { return service.modelDatabasePath(name); }
    QString modelDatabasePathAt(const QString &root) const override { return service.modelDatabasePathAt(root); }
    QString trainWeightsPathAt(const QString &root) const override { return service.trainWeightsPathAt(root); }
    QString operationRoot() const override { return service.operationRoot(); }
    QString operationStagingRoot(const QString &operation_id) const override
    {
        return service.operationStagingRoot(operation_id);
    }
    QString operationQuarantineRoot(const QString &operation_id) const override
    {
        return service.operationQuarantineRoot(operation_id);
    }
    QString operationJournalPath(const QString &operation_id) const override
    {
        return service.operationJournalPath(operation_id);
    }

    bool ensureModelStorageAt(const QString &root, QString *error) const override
    {
        return service.ensureModelStorageAt(root, error);
    }

    bool copyDirectoryContents(const QString &source, const QString &target, QString *error) const override
    {
        return service.copyDirectoryContents(source, target, error);
    }

    bool moveDirectory(const QString &source, const QString &target, QString *error) override
    {
        ++move_calls;
        if (fail_publish && (fail_publish_call_index < 0 || move_calls == fail_publish_call_index))
        {
            if (error != nullptr)
                *error = QStringLiteral("注入的模型目录发布失败");
            return false;
        }
        return service.moveDirectory(source, target, error);
    }

    bool removeDirectory(const QString &root, QString *error) const override
    {
        if (fail_cleanup)
        {
            if (error != nullptr)
                *error = QStringLiteral("注入的模型目录清理失败");
            return false;
        }
        return service.removeDirectory(root, error);
    }

    ModelStorageService service;
};

} // namespace

class ModelLifecycleTest final : public QObject
{
    Q_OBJECT

private slots:
    void createRollsBackStagingWhenDatabaseInsertFails()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        MemoryModelRecordStore records;
        records.fail_add = true;
        TestStorageAdapter storage(directory.path());
        ModelLifecycle lifecycle(records, storage);

        const ModelLifecycleResult result = lifecycle.create(makeRecord(QStringLiteral("create-failure")));
        QVERIFY(!result.succeeded());
        QVERIFY(!result.recoveryRequired());
        QVERIFY(!QDir(storage.modelRoot(QStringLiteral("create-failure"))).exists());
        QVERIFY(QDir(storage.operationRoot()).entryList({QStringLiteral("*.json")}, QDir::Files).isEmpty());
    }

    void renameRestoresDirectoryWhenDatabaseUpdateFails()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        MemoryModelRecordStore records;
        const ModelLifecycleRecord source = makeRecord(QStringLiteral("old-name"));
        const qint64                 model_id = 1;
        ModelLifecycleRecord          stored   = source;
        stored.model_id                        = model_id;
        records.records.insert(model_id, stored);
        TestStorageAdapter storage(directory.path());
        QString            error;
        QVERIFY(storage.service.ensureModelStorage(source.name, &error));
        records.fail_rename = true;
        ModelLifecycle lifecycle(records, storage);

        const ModelLifecycleResult result = lifecycle.rename(model_id, source.name, QStringLiteral("new-name"));
        QVERIFY(!result.succeeded());
        QVERIFY(!result.recoveryRequired());
        QVERIFY(QDir(storage.modelRoot(source.name)).exists());
        QVERIFY(!QDir(storage.modelRoot(QStringLiteral("new-name"))).exists());
    }

    void deleteRestoresDirectoryWhenDatabaseDeleteFails()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        MemoryModelRecordStore records;
        const ModelLifecycleRecord source = makeRecord(QStringLiteral("delete-failure"));
        const qint64                 model_id = 1;
        ModelLifecycleRecord          stored   = source;
        stored.model_id                        = model_id;
        records.records.insert(model_id, stored);
        TestStorageAdapter storage(directory.path());
        QString            error;
        QVERIFY(storage.service.ensureModelStorage(source.name, &error));
        records.fail_delete = true;
        ModelLifecycle lifecycle(records, storage);

        const ModelLifecycleResult result = lifecycle.remove(model_id, source.name);
        QVERIFY(!result.succeeded());
        QVERIFY(!result.recoveryRequired());
        QVERIFY(QDir(storage.modelRoot(source.name)).exists());
        QVERIFY(QDir(storage.operationRoot()).entryList({QStringLiteral("*.json")}, QDir::Files).isEmpty());
    }

    void copyRollsBackStagingWhenDatabaseInsertFails()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        MemoryModelRecordStore records;
        const ModelLifecycleRecord source = makeRecord(QStringLiteral("source"));
        ModelLifecycleRecord          stored = source;
        stored.model_id                    = 1;
        records.records.insert(stored.model_id, stored);
        records.fail_add = true;
        TestStorageAdapter storage(directory.path());
        QString            error;
        QVERIFY(storage.service.ensureModelStorage(source.name, &error));
        ModelLifecycle lifecycle(records, storage);

        ModelLifecycleRecord target = makeRecord(QStringLiteral("copy"));
        target.uuid                   = QStringLiteral("uuid-copy");
        const ModelLifecycleResult result = lifecycle.copy(source, target, false);
        QVERIFY(!result.succeeded());
        QVERIFY(!result.recoveryRequired());
        QVERIFY(!QDir(storage.modelRoot(target.name)).exists());
    }

    void recoveryPublishesRenameAfterDirectoryPublishFailure()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        MemoryModelRecordStore records;
        const ModelLifecycleRecord source = makeRecord(QStringLiteral("recover-old"));
        const qint64                 model_id = 1;
        ModelLifecycleRecord          stored   = source;
        stored.model_id                        = model_id;
        records.records.insert(model_id, stored);
        TestStorageAdapter storage(directory.path());
        QString            error;
        QVERIFY(storage.service.ensureModelStorage(source.name, &error));
        storage.fail_publish = true;
        storage.fail_publish_call_index = 2;
        ModelLifecycle lifecycle(records, storage);

        const ModelLifecycleResult result = lifecycle.rename(model_id, source.name, QStringLiteral("recover-new"));
        QVERIFY(!result.succeeded());
        QVERIFY(result.recoveryRequired());
        QVERIFY(!QDir(storage.modelRoot(source.name)).exists());
        QVERIFY(!QDir(storage.modelRoot(QStringLiteral("recover-new"))).exists());

        storage.fail_publish = false;
        const ModelLifecycleResult recovery = lifecycle.recoverPending();
        QVERIFY2(recovery.succeeded(), qPrintable(recovery.error));
        QVERIFY(QDir(storage.modelRoot(QStringLiteral("recover-new"))).exists());
        QVERIFY(!QDir(storage.modelRoot(source.name)).exists());
        QCOMPARE(records.records.value(model_id).name, QStringLiteral("recover-new"));
    }

    void recoversModelByPersistedUuidWhenJournalModelIdNotUpdated()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        MemoryModelRecordStore records;

        ModelLifecycleRecord stored = makeRecord(QStringLiteral("created-interrupted"));
        stored.model_id             = 42;
        records.records.insert(stored.model_id, stored);

        TestStorageAdapter storage(directory.path());
        const QString op_id        = QStringLiteral("op-uuid-crash");
        const QString staging_root = storage.operationStagingRoot(op_id);
        QString       error;
        QVERIFY(storage.ensureModelStorageAt(staging_root, &error));

        const QString journal_path = storage.operationJournalPath(op_id);
        QVERIFY(QDir().mkpath(storage.operationRoot()));
        QFile journal_file(journal_path);
        QVERIFY(journal_file.open(QIODevice::WriteOnly));
        journal_file.write(QJsonDocument(QJsonObject{
            {QStringLiteral("id"), op_id},
            {QStringLiteral("kind"), QStringLiteral("create")},
            {QStringLiteral("phase"), QStringLiteral("staged")},
            {QStringLiteral("model_id"), -1},
            {QStringLiteral("uuid"), stored.uuid},
            {QStringLiteral("target_name"), stored.name},
            {QStringLiteral("staging_path"), staging_root},
        }).toJson(QJsonDocument::Compact));
        journal_file.close();

        ModelLifecycle lifecycle(records, storage);
        QVERIFY(!QDir(storage.modelRoot(stored.name)).exists());
        QVERIFY(QDir(staging_root).exists());

        const ModelLifecycleResult recovery = lifecycle.recoverPending();
        QVERIFY2(recovery.succeeded(), qPrintable(recovery.error));

        QVERIFY(QDir(storage.modelRoot(stored.name)).exists());
        QVERIFY(!QDir(staging_root).exists());
        QVERIFY(!QFile::exists(journal_path));
        QVERIFY(records.records.contains(stored.model_id));
        QCOMPARE(records.records.value(stored.model_id).uuid, stored.uuid);
    }

    void retainsJournalWhenCleanupFailsAndRepeatedRecoveryDoesNotDuplicateOrLoseData()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        MemoryModelRecordStore records;

        ModelLifecycleRecord stored = makeRecord(QStringLiteral("cleanup-fail-model"));
        stored.model_id             = 99;
        records.records.insert(stored.model_id, stored);

        TestStorageAdapter storage(directory.path());
        QString            error;
        QVERIFY(storage.service.ensureModelStorage(stored.name, &error));

        const QString op_id        = QStringLiteral("op-cleanup-fail");
        const QString staging_root = storage.operationStagingRoot(op_id);
        QVERIFY(storage.ensureModelStorageAt(staging_root, &error));

        const QString journal_path = storage.operationJournalPath(op_id);
        QVERIFY(QDir().mkpath(storage.operationRoot()));
        QFile journal_file(journal_path);
        QVERIFY(journal_file.open(QIODevice::WriteOnly));
        journal_file.write(QJsonDocument(QJsonObject{
            {QStringLiteral("id"), op_id},
            {QStringLiteral("kind"), QStringLiteral("copy")},
            {QStringLiteral("phase"), QStringLiteral("published")},
            {QStringLiteral("model_id"), stored.model_id},
            {QStringLiteral("uuid"), stored.uuid},
            {QStringLiteral("target_name"), stored.name},
            {QStringLiteral("staging_path"), staging_root},
        }).toJson(QJsonDocument::Compact));
        journal_file.close();

        // 1. Inject cleanup failure
        storage.fail_cleanup = true;
        ModelLifecycle lifecycle(records, storage);

        const ModelLifecycleResult recovery1 = lifecycle.recoverPending();
        QVERIFY(!recovery1.succeeded());
        QVERIFY(recovery1.recoveryRequired());

        // Credential (journal) MUST be retained
        QVERIFY(QFile::exists(journal_path));
        // Target model directory must not be lost
        QVERIFY(QDir(storage.modelRoot(stored.name)).exists());
        // Record must not be lost or duplicated
        QCOMPARE(records.records.size(), 1);

        // 2. Clear cleanup failure and repeat recovery
        storage.fail_cleanup = false;
        const ModelLifecycleResult recovery2 = lifecycle.recoverPending();
        QVERIFY2(recovery2.succeeded(), qPrintable(recovery2.error));

        // Now journal is cleaned up, staging is gone, data intact, no duplicate records
        QVERIFY(!QFile::exists(journal_path));
        QVERIFY(!QDir(staging_root).exists());
        QVERIFY(QDir(storage.modelRoot(stored.name)).exists());
        QCOMPARE(records.records.size(), 1);
        QCOMPARE(records.records.value(stored.model_id).name, stored.name);
    }

    void recoversInterruptedModelCopyAndVerifiesDirectoryRecordAndCopyScope()
    {
        testsupport::EvaluationFixture fixture(0);
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ProjectModelRecordStore           records(&database);
        TestStorageAdapter                storage(fixture.rootPath());

        // Create source model
        ModelLifecycleRecord source = makeRecord(QStringLiteral("source-model"));
        qint64               source_id = -1;
        QString              err;
        QVERIFY(records.addModel(source, source_id, err));
        source.model_id = source_id;

        // Write train params, datasets, and weights into source model
        QVERIFY(storage.service.ensureModelStorage(source.name, &err));
        {
            dltool::database::ModelDataBase source_db(storage.modelDatabasePath(source.name));
            const QVariantMap params{{QStringLiteral("train"),
                                      QVariantMap{{QStringLiteral("epochs"), 50}, {QStringLiteral("batch"), 16}}}};
            QVERIFY(source_db.replaceTrainParams(params, &err));
            QList<dltool::database::DatasetSelectionRecord> selections{
                {QStringLiteral("train"), 1, {10, 20}}
            };
            QVERIFY(source_db.replaceDatasets(selections, &err));
        }
        const QString source_weights_dir = storage.trainWeightsPathAt(storage.modelRoot(source.name));
        QVERIFY(QDir().mkpath(source_weights_dir));
        QFile source_weight_file(QDir(source_weights_dir).filePath(QStringLiteral("best.pt")));
        QVERIFY(source_weight_file.open(QIODevice::WriteOnly));
        source_weight_file.write("weights-payload-12345");
        source_weight_file.close();

        // Target to copy
        ModelLifecycleRecord target = makeRecord(QStringLiteral("copied-model"));
        target.uuid                 = QStringLiteral("uuid-target-copied");

        // Inject publish failure during copy
        storage.fail_publish            = true;
        storage.fail_publish_call_index = 1;
        ModelLifecycle lifecycle(records, storage);

        const ModelLifecycleResult copy_result = lifecycle.copy(source, target, true /* copy_train_weights */);
        QVERIFY2(!copy_result.succeeded(), qPrintable(copy_result.error));
        QVERIFY2(copy_result.recoveryRequired(), qPrintable(copy_result.error));

        // Target directory does not exist yet (publish failed)
        QVERIFY(!QDir(storage.modelRoot(target.name)).exists());

        // Now "reopen" - failure injection cleared, run recovery
        storage.fail_publish = false;
        const ModelLifecycleResult recovery = lifecycle.recoverPending();
        QVERIFY2(recovery.succeeded(), qPrintable(recovery.error));

        // 1. 核对目录
        const QString target_root = storage.modelRoot(target.name);
        QVERIFY(QDir(target_root).exists());
        QVERIFY(QDir(storage.operationRoot()).entryList({QStringLiteral("*.json")}, QDir::Files).isEmpty());

        // 2. 核对记录
        ModelLifecycleRecord recovered_target;
        bool                 exists = false;
        QVERIFY(records.findModelByUuid(target.uuid, recovered_target, exists, err));
        QVERIFY(exists);
        QCOMPARE(recovered_target.name, target.name);
        QCOMPARE(recovered_target.framework_name, source.framework_name);
        QCOMPARE(recovered_target.model_architecture, source.model_architecture);

        // 3. 核对复制范围 (train_params, datasets, weights)
        {
            dltool::database::ModelDataBase target_db(storage.modelDatabasePath(target.name));
            QVariantMap target_params;
            QVERIFY(target_db.readTrainParams(target_params, &err));
            const QVariantMap train_group = target_params.value(QStringLiteral("train")).toMap();
            QCOMPARE(train_group.value(QStringLiteral("epochs")).toInt(), 50);
            QCOMPARE(train_group.value(QStringLiteral("batch")).toInt(), 16);

            QList<dltool::database::DatasetSelectionRecord> target_selections;
            QVERIFY(target_db.readDatasets(target_selections, &err));
            QCOMPARE(target_selections.size(), 1);
            QCOMPARE(target_selections.front().dataset_id, 1);
            QCOMPARE(target_selections.front().class_ids, (QList<qint64>{10, 20}));
        }
        const QString target_weight_path = QDir(storage.trainWeightsPathAt(target_root)).filePath(QStringLiteral("best.pt"));
        QVERIFY(QFile::exists(target_weight_path));
        QFile target_weight_file(target_weight_path);
        QVERIFY(target_weight_file.open(QIODevice::ReadOnly));
        QCOMPARE(target_weight_file.readAll(), QByteArray("weights-payload-12345"));
    }

    void recoversInterruptedModelCopyWithNoWeightsAndVerifiesCopyScope()
    {
        testsupport::EvaluationFixture fixture(0);
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ProjectModelRecordStore           records(&database);
        TestStorageAdapter                storage(fixture.rootPath());

        ModelLifecycleRecord source = makeRecord(QStringLiteral("source-model-noweights"));
        qint64               source_id = -1;
        QString              err;
        QVERIFY(records.addModel(source, source_id, err));
        source.model_id = source_id;

        QVERIFY(storage.service.ensureModelStorage(source.name, &err));
        {
            dltool::database::ModelDataBase source_db(storage.modelDatabasePath(source.name));
            const QVariantMap params{{QStringLiteral("train"),
                                      QVariantMap{{QStringLiteral("epochs"), 20}}}};
            QVERIFY(source_db.replaceTrainParams(params, &err));
        }
        const QString source_weights_dir = storage.trainWeightsPathAt(storage.modelRoot(source.name));
        QVERIFY(QDir().mkpath(source_weights_dir));
        QFile source_weight_file(QDir(source_weights_dir).filePath(QStringLiteral("best.pt")));
        QVERIFY(source_weight_file.open(QIODevice::WriteOnly));
        source_weight_file.write("weights-payload-noweights");
        source_weight_file.close();

        ModelLifecycleRecord target = makeRecord(QStringLiteral("copied-model-noweights"));
        target.uuid                 = QStringLiteral("uuid-target-noweights");

        storage.fail_publish            = true;
        storage.fail_publish_call_index = 1;
        ModelLifecycle lifecycle(records, storage);

        const ModelLifecycleResult copy_result = lifecycle.copy(source, target, false /* copy_train_weights = false */);
        QVERIFY2(!copy_result.succeeded(), qPrintable(copy_result.error));
        QVERIFY2(copy_result.recoveryRequired(), qPrintable(copy_result.error));

        storage.fail_publish = false;
        const ModelLifecycleResult recovery = lifecycle.recoverPending();
        QVERIFY2(recovery.succeeded(), qPrintable(recovery.error));

        // 1. 核对目录
        const QString target_root = storage.modelRoot(target.name);
        QVERIFY(QDir(target_root).exists());

        // 2. 核对记录
        ModelLifecycleRecord recovered_target;
        bool                 exists = false;
        QVERIFY(records.findModelByUuid(target.uuid, recovered_target, exists, err));
        QVERIFY(exists);

        // 3. 核对复制范围 (train_params copied, weights NOT copied)
        {
            dltool::database::ModelDataBase target_db(storage.modelDatabasePath(target.name));
            QVariantMap target_params;
            QVERIFY(target_db.readTrainParams(target_params, &err));
            const QVariantMap train_group = target_params.value(QStringLiteral("train")).toMap();
            QCOMPARE(train_group.value(QStringLiteral("epochs")).toInt(), 20);
        }
        const QString target_weight_path = QDir(storage.trainWeightsPathAt(target_root)).filePath(QStringLiteral("best.pt"));
        QVERIFY(!QFile::exists(target_weight_path));
    }
};

REGISTER_TEST(ModelLifecycleTest)

#include "test_ModelLifecycle.moc"
