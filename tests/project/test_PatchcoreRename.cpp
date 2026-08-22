#include "PersistentProjectFixture.h"

#include "model/ModelStorageService.h"
#include "project/Projects.h"

#include <QFileInfo>
#include <QTest>

using namespace dltool::model::integration;

namespace {

dltool::model::ModelManager::ModelRecordView modelByName(dltool::model::ModelManager *manager,
                                                          const QString &name)
{
    if (manager == nullptr)
        return {};
    for (int row = 0; row < manager->rowCount(); ++row)
    {
        const auto record = manager->modelAt(row);
        if (record.value(QStringLiteral("name")).toString() == name)
            return manager->modelRecordViewForUuid(record.value(QStringLiteral("uuid")).toString());
    }
    return {};
}

} // namespace

class PatchcoreRenameIntegrationTest final : public QObject
{
    Q_OBJECT

private slots:
    void renamesPatchcoreModelAndRestoresName()
    {
        PersistentProjectFixture fixture;
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        auto *manager = fixture.project()->modelManager();
        QVERIFY(manager != nullptr);
        const auto source = modelByName(manager, PersistentProjectFixture::patchcoreModelName());
        QVERIFY2(source.isValid(), "PatchCore 模型不存在，请先运行 model-creation");

        const QString old_name = source.name;
        const QString new_name = PersistentProjectFixture::patchcoreModelRenameName();
        QVERIFY(manager->renameModel(source.model_id, new_name));

        const auto renamed = manager->modelRecordViewForUuid(source.uuid);
        QVERIFY(renamed.isValid());
        QCOMPARE(renamed.name, new_name);
        const dltool::model::ModelStorageService storage(PersistentProjectFixture::projectRoot());
        QVERIFY(QFileInfo(storage.path(new_name, dltool::model::ModelStorageLocation::ModelRoot)).isDir());
        QVERIFY(!QFileInfo(storage.path(old_name, dltool::model::ModelStorageLocation::ModelRoot)).exists());

        QVERIFY(manager->renameModel(source.model_id, old_name));
        const auto restored = manager->modelRecordViewForUuid(source.uuid);
        QVERIFY(restored.isValid());
        QCOMPARE(restored.name, old_name);
        QVERIFY(QFileInfo(storage.path(old_name, dltool::model::ModelStorageLocation::ModelRoot)).isDir());
        QVERIFY(!QFileInfo(storage.path(new_name, dltool::model::ModelStorageLocation::ModelRoot)).exists());
    }
};

QTEST_GUILESS_MAIN(PatchcoreRenameIntegrationTest)

#include "test_PatchcoreRename.moc"
