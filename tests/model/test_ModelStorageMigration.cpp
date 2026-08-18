#include "../test_runner.h"

#include "model/ModelStorageMigration.h"

#include <QTest>

using namespace dltool::model;

class ModelStorageMigrationTest : public QObject
{
    Q_OBJECT

private slots:
    void migrationIsExplicitNoOp()
    {
        const ModelStorageMigrationResult result
            = migrateModelStorage(QStringLiteral("F:/tmp/project"), QStringLiteral("model"), QStringLiteral("uuid"));
        QVERIFY(!result.migrated);
        QVERIFY(!result.legacy_test_created);
        QVERIFY(result.error.isEmpty());
    }
};

REGISTER_TEST(ModelStorageMigrationTest)

#include "test_ModelStorageMigration.moc"
