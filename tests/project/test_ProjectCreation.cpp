#include "PersistentProjectFixture.h"
#include "project/Projects.h"

#include <QFileInfo>
#include <QTest>

using namespace dltool::model::integration;

class ProjectCreationIntegrationTest final : public QObject
{
    Q_OBJECT

private slots:
    void createsPersistentAnomalyProject()
    {
        PersistentProjectFixture fixture(true);
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        QVERIFY(fixture.project() != nullptr);
        QVERIFY(fixture.dataManager() != nullptr);
        QCOMPARE(fixture.project()->name(), PersistentProjectFixture::projectName());
        QCOMPARE(fixture.project()->method(), PersistentProjectFixture::kMethod);
        const QFileInfo project_file(PersistentProjectFixture::projectDatabasePath());
        QVERIFY(project_file.isFile());
        QCOMPARE(project_file.suffix(), QStringLiteral("dlpro"));
    }
};

QTEST_GUILESS_MAIN(ProjectCreationIntegrationTest)

#include "test_ProjectCreation.moc"
