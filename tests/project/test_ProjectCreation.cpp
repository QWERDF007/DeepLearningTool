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

    void validatesProjectPaths()
    {
        auto *manager = dltool::project::ProjectManager::getInstance();
        QCOMPARE(manager->isProjectValid(PersistentProjectFixture::kMethod, QString(), true), QStringLiteral("项目路径为空"));
        QCOMPARE(manager->isProjectValid(PersistentProjectFixture::kMethod, QStringLiteral("relative/test.dlpro"), true), QStringLiteral("项目路径必须是绝对路径"));
        QCOMPARE(manager->isProjectValid(PersistentProjectFixture::kMethod, QStringLiteral("//192.168.2.87/share/new_project.dlpro"), true), QString());
    }
};

QTEST_GUILESS_MAIN(ProjectCreationIntegrationTest)

#include "test_ProjectCreation.moc"
