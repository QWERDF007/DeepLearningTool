#include "../test_runner.h"

#include "model/ModelLifecycle.h"
#include "model/ModelStorageService.h"

#include <QDir>
#include <QHash>
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
    int  move_calls{0};

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
        if (fail_publish && move_calls == 2)
        {
            if (error != nullptr)
                *error = QStringLiteral("注入的模型目录发布失败");
            return false;
        }
        return service.moveDirectory(source, target, error);
    }

    bool removeDirectory(const QString &root, QString *error) const override
    {
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
};

REGISTER_TEST(ModelLifecycleTest)

#include "test_ModelLifecycle.moc"
