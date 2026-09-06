#include "database/DataBase.h"
#include "database/ModelDataBase.h"

#include <QCoreApplication>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include <sqlite3.h>

namespace {

bool executeSql(const QString &path, const char *sql, QString *error = nullptr)
{
    sqlite3 *database = nullptr;
    if (sqlite3_open_v2(path.toUtf8().constData(), &database, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr)
        != SQLITE_OK)
    {
        if (error != nullptr)
            *error = database != nullptr ? QString::fromUtf8(sqlite3_errmsg(database)) : QStringLiteral("打开数据库失败");
        if (database != nullptr)
            sqlite3_close(database);
        return false;
    }

    char *sqlite_error = nullptr;
    const int result = sqlite3_exec(database, sql, nullptr, nullptr, &sqlite_error);
    if (result != SQLITE_OK && error != nullptr)
    {
        *error = sqlite_error != nullptr ? QString::fromUtf8(sqlite_error) : QString::fromUtf8(sqlite3_errmsg(database));
    }
    if (sqlite_error != nullptr)
        sqlite3_free(sqlite_error);
    sqlite3_close(database);
    return result == SQLITE_OK;
}

int userVersion(const QString &path)
{
    sqlite3 *database = nullptr;
    if (sqlite3_open_v2(path.toUtf8().constData(), &database, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
    {
        if (database != nullptr)
            sqlite3_close(database);
        return -1;
    }

    sqlite3_stmt *statement = nullptr;
    int           version   = -1;
    if (sqlite3_prepare_v2(database, "PRAGMA user_version", -1, &statement, nullptr) == SQLITE_OK
        && sqlite3_step(statement) == SQLITE_ROW)
    {
        version = sqlite3_column_int(statement, 0);
    }
    sqlite3_finalize(statement);
    sqlite3_close(database);
    return version;
}

QStringList tableColumns(const QString &path, const QString &table_name)
{
    sqlite3 *database = nullptr;
    if (sqlite3_open_v2(path.toUtf8().constData(), &database, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
    {
        if (database != nullptr)
            sqlite3_close(database);
        return {};
    }

    const QString query = QStringLiteral("PRAGMA table_info('%1')").arg(table_name);
    sqlite3_stmt *statement = nullptr;
    QStringList   columns;
    if (sqlite3_prepare_v2(database, query.toUtf8().constData(), -1, &statement, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(statement) == SQLITE_ROW)
        {
            const char *name = reinterpret_cast<const char *>(sqlite3_column_text(statement, 1));
            if (name != nullptr)
                columns.append(QString::fromUtf8(name));
        }
    }
    sqlite3_finalize(statement);
    sqlite3_close(database);
    return columns;
}

} // namespace

class DatabaseSchemaTest final : public QObject
{
    Q_OBJECT

private slots:
    void emptyModelDatabaseIsInitializedWithCurrentSchemaVersion()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("model.db"));

        dltool::database::ModelDataBase database(path);
        QVariantMap                    params;
        QString                        error;
        QVERIFY2(database.readTrainParams(params, &error), qPrintable(error));
        QVERIFY(params.isEmpty());
        QCOMPARE(userVersion(path), 1);
    }

    void unknownSchemaVersionIsRejected()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("model.db"));
        QVERIFY(executeSql(path, "PRAGMA user_version = 77"));

        dltool::database::ModelDataBase database(path);
        QVariantMap                    params;
        QString                        error;
        QVERIFY(!database.readTrainParams(params, &error));
        QVERIFY(error.contains(QStringLiteral("schema"), Qt::CaseInsensitive));
        QVERIFY(error.contains(QStringLiteral("77")));
        QCOMPARE(userVersion(path), 77);
    }

    void malformedSchemaMigrationRollsBack()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("model.db"));
        QVERIFY(executeSql(path,
                           "CREATE TABLE train_params (\"group\" TEXT NOT NULL, name_en TEXT NOT NULL, "
                           "value TEXT NOT NULL, PRIMARY KEY (\"group\", name_en))"));

        dltool::database::ModelDataBase database(path);
        QVariantMap                    params;
        QString                        error;
        QVERIFY(!database.readTrainParams(params, &error));
        QVERIFY(error.contains(QStringLiteral("train_params")));
        QCOMPARE(userVersion(path), 0);
    }

    void projectStaticReadersRejectMalformedSchema()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("project.dlpro"));

        {
            dltool::database::ProjectDataBase database(path);
            QString                           error;
            QVERIFY2(database.initProject(QStringLiteral("schema-test"), 0, path, {}, directory.path(), 1, 1, error),
                     qPrintable(error));
        }
        QVERIFY(executeSql(path, "PRAGMA user_version = 1; ALTER TABLE project RENAME COLUMN description TO legacy_description"));

        QVariantMap info;
        QString     error;
        QVERIFY(!dltool::database::ProjectDataBase::getProjectInfo(path, info, error));
        QVERIFY(error.contains(QStringLiteral("schema"), Qt::CaseInsensitive));
        QVERIFY(error.contains(QStringLiteral("description")));
    }

    void recentProjectsDatabaseUsesSameSchemaLifecycle()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("history.db"));

        dltool::database::RecentProjectsDataBase database(path);
        std::vector<QString>                     paths;
        QString                                  error;
        QCOMPARE(database.getProjects(paths, error), 0);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(userVersion(path), 1);
        QCOMPARE(tableColumns(path, QStringLiteral("recent_projects")),
                 QStringList({QStringLiteral("id"), QStringLiteral("path"), QStringLiteral("extra_data")}));
    }

    void projectDatabaseInitializationUsesSameSchemaLifecycle()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("project.dlpro"));

        dltool::database::ProjectDataBase database(path);
        QString                           error;
        QVERIFY2(database.initProject(QStringLiteral("schema-test"), 0, path, {}, directory.path(), 1, 1, error),
                 qPrintable(error));
        QCOMPARE(userVersion(path), 1);
    }

    void settingsDatabaseCreatesAndValidatesDynamicTables()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("settings.db"));

        dltool::database::SettingsDataBase database(path);
        QString                           error;
        QVERIFY2(database.saveSettings(QStringLiteral("software_settings"), {{QStringLiteral("language"), QStringLiteral("zh-CN")}}, error),
                 qPrintable(error));
        QCOMPARE(userVersion(path), 1);
        QCOMPARE(tableColumns(path, QStringLiteral("software_settings")),
                 QStringList({QStringLiteral("name_en"), QStringLiteral("value"), QStringLiteral("mtime")}));

        const QVariantMap values = database.loadSettings(QStringLiteral("software_settings"), error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(values.value(QStringLiteral("language")).toString(), QStringLiteral("zh-CN"));
    }

    void settingsDatabaseSupportsMultipleDynamicTables()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("settings.db"));

        dltool::database::SettingsDataBase database(path);
        QString                           error;
        QVERIFY2(database.saveSettings(QStringLiteral("ui_settings"), {{QStringLiteral("theme"), QStringLiteral("dark")}}, error),
                 qPrintable(error));
        QVERIFY2(database.saveSettings(QStringLiteral("software_settings"),
                                       {{QStringLiteral("language"), QStringLiteral("zh-CN")}}, error),
                 qPrintable(error));

        QCOMPARE(database.loadSettings(QStringLiteral("ui_settings"), error).value(QStringLiteral("theme")).toString(),
                 QStringLiteral("dark"));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(database.loadSettings(QStringLiteral("software_settings"), error)
                     .value(QStringLiteral("language"))
                     .toString(),
                 QStringLiteral("zh-CN"));
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }

    void malformedSettingsTableIsRejected()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("settings.db"));
        QVERIFY(executeSql(path, "CREATE TABLE ui_settings (name_en TEXT NOT NULL UNIQUE, value TEXT NOT NULL)"));

        dltool::database::SettingsDataBase database(path);
        QString                           error;
        const QVariantMap values = database.loadSettings(QStringLiteral("ui_settings"), error);
        QVERIFY(values.isEmpty());
        QVERIFY(error.contains(QStringLiteral("schema"), Qt::CaseInsensitive));
        QVERIFY(error.contains(QStringLiteral("mtime")));
    }

    void invalidSettingsTableNameIsRejected()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("settings.db"));

        dltool::database::SettingsDataBase database(path);
        QString                           error;
        const QVariantMap values = database.loadSettings(QStringLiteral("ui-settings"), error);
        QVERIFY(values.isEmpty());
        QVERIFY(error.contains(QStringLiteral("table name"), Qt::CaseInsensitive));
        QCOMPARE(userVersion(path), 0);
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    DatabaseSchemaTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_DatabaseSchema.moc"
