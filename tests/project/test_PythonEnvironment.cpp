#include "PersistentProjectFixture.h"

#include "settings/GlobalSettings.h"

#include <QDir>
#include <QFileInfo>
#include <QTest>

using namespace dltool::model::integration;

class PythonEnvironmentIntegrationTest final : public QObject
{
    Q_OBJECT

private slots:
    void setsAndRestoresConfiguredPythonEnvironment()
    {
        PythonEnvironmentScope scope;
        QVERIFY2(scope.isValid(), qPrintable(scope.error()));
        QVERIFY(QFileInfo(scope.path()).isDir());

        auto *settings = dltool::settings::GlobalSettings::getInstance();
        QVERIFY(settings != nullptr);
        QCOMPARE(dltool::settings::GlobalSettings::pythonEnvironmentPath(), scope.path());

        const QVariant original = settings->valueForField(dltool::settings::generated::field::Software::PythonEnvPath);
        const QString alternate = QDir::cleanPath(QDir::tempPath() + QStringLiteral("/dltool-python-env-test"));
        QVERIFY(settings->setFieldValue(dltool::settings::generated::field::Software::PythonEnvPath, alternate));
        QCOMPARE(dltool::settings::GlobalSettings::pythonEnvironmentPath(), alternate);
        QVERIFY(settings->setFieldValue(dltool::settings::generated::field::Software::PythonEnvPath, original));
        QCOMPARE(dltool::settings::GlobalSettings::pythonEnvironmentPath(), original.toString().trimmed());
    }
};

QTEST_GUILESS_MAIN(PythonEnvironmentIntegrationTest)

#include "test_PythonEnvironment.moc"
