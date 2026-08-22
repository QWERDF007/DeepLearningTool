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

class PatchcoreDeleteIntegrationTest final : public QObject
{
    Q_OBJECT

private slots:
    void deletesCopiedPatchcoreModel()
    {
        PersistentProjectFixture fixture;
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        auto *manager = fixture.project()->modelManager();
        QVERIFY(manager != nullptr);
        const auto source = modelByName(manager, PersistentProjectFixture::patchcoreModelName());
        QVERIFY2(source.isValid(), "PatchCore 源模型不存在，请先运行 model-creation");

        auto copied = modelByName(manager, PersistentProjectFixture::patchcoreModelCopyName());
        if (!copied.isValid())
        {
            QVERIFY(manager->copyModel(source.model_id, false));
            copied = modelByName(manager, PersistentProjectFixture::patchcoreModelCopyName());
        }
        QVERIFY(copied.isValid());

        const dltool::model::ModelStorageService storage(PersistentProjectFixture::projectRoot());
        const QString copied_root = storage.path(copied.name, dltool::model::ModelStorageLocation::ModelRoot);
        QVERIFY(QFileInfo(copied_root).isDir());
        QVERIFY(manager->deleteModel(copied.model_id));
        QVERIFY(!manager->modelRecordViewForUuid(copied.uuid).isValid());
        QVERIFY(!QFileInfo(copied_root).exists());
        QVERIFY(modelByName(manager, PersistentProjectFixture::patchcoreModelName()).isValid());
    }
};

QTEST_GUILESS_MAIN(PatchcoreDeleteIntegrationTest)

#include "test_PatchcoreDelete.moc"
