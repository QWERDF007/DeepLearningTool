#include "database/DataBase.h"
#include "database/ModelDataBase.h"
#include "database/DatabaseSchema.h"

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

bool tableExistsInDb(const QString &path, const QString &table_name)
{
    sqlite3 *database = nullptr;
    if (sqlite3_open_v2(path.toUtf8().constData(), &database, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
    {
        if (database != nullptr)
            sqlite3_close(database);
        return false;
    }
    sqlite3_stmt *stmt = nullptr;
    bool exists = false;
    if (sqlite3_prepare_v2(database, "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?1", -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_text(stmt, 1, table_name.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        exists = (sqlite3_step(stmt) == SQLITE_ROW);
        sqlite3_finalize(stmt);
    }
    sqlite3_close(database);
    return exists;
}

QString queryString(const QString &path, const char *sql)
{
    sqlite3 *database = nullptr;
    if (sqlite3_open_v2(path.toUtf8().constData(), &database, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
    {
        if (database != nullptr)
            sqlite3_close(database);
        return {};
    }
    sqlite3_stmt *stmt = nullptr;
    QString result;
    if (sqlite3_prepare_v2(database, sql, -1, &stmt, nullptr) == SQLITE_OK)
    {
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const char *text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
            if (text != nullptr)
                result = QString::fromUtf8(text);
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(database);
    return result;
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

    void cleanup()
    {
        dltool::database::detail::setMigrationHookForTest(nullptr);
    }

    void schemaValidationRejectsViewInsteadOfTable()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("project.dlpro"));
        // Create project as a VIEW instead of a TABLE
        QVERIFY(executeSql(path, "PRAGMA user_version = 1; CREATE VIEW project AS SELECT 1 AS id, 'mock' AS name;"));

        QVariantMap info;
        QString     error;
        QVERIFY(!dltool::database::ProjectDataBase::getProjectInfo(path, info, error));
        QVERIFY(error.contains(QStringLiteral("schema"), Qt::CaseInsensitive));
        QVERIFY(error.contains(QStringLiteral("project")));
    }

    void schemaValidationRejectsMissingForeignKey()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("project.dlpro"));

        // Initialize a valid project
        {
            dltool::database::ProjectDataBase database(path);
            QString                           error;
            QVERIFY2(database.initProject(QStringLiteral("schema-test"), 0, path, {}, directory.path(), 1, 1, error),
                     qPrintable(error));
        }
        // Tamper images table to drop its foreign key to datasets
        QVERIFY(executeSql(path,
                           "DROP TABLE images; "
                           "CREATE TABLE images (id INTEGER NOT NULL PRIMARY KEY, dataset_id INTEGER NOT NULL, path TEXT, extra_data BLOB);"));

        QVariantMap info;
        QString     error;
        QVERIFY(!dltool::database::ProjectDataBase::getProjectInfo(path, info, error));
        QVERIFY(error.contains(QStringLiteral("images")));
        QVERIFY(error.contains(QStringLiteral("外键")));
    }

    void schemaValidationRejectsMissingUniqueConstraint()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("project.dlpro"));

        // Initialize a valid project
        {
            dltool::database::ProjectDataBase database(path);
            QString                           error;
            QVERIFY2(database.initProject(QStringLiteral("schema-test"), 0, path, {}, directory.path(), 1, 1, error),
                     qPrintable(error));
        }
        // Tamper models table to drop its UNIQUE constraint on uuid
        QVERIFY(executeSql(path,
                           "DROP TABLE models; "
                           "CREATE TABLE models (id INTEGER NOT NULL PRIMARY KEY, uuid TEXT NOT NULL, name TEXT, framework_name TEXT, "
                           "model_architecture TEXT, ctime INTEGER NOT NULL, mtime INTEGER NOT NULL, extra_data BLOB); "
                           "CREATE UNIQUE INDEX partial_model_uuid ON models(uuid) WHERE id > 100;"));

        QVariantMap info;
        QString     error;
        QVERIFY(!dltool::database::ProjectDataBase::getProjectInfo(path, info, error));
        QVERIFY(error.contains(QStringLiteral("models")));
        QVERIFY(error.contains(QStringLiteral("唯一")));
    }

    void schemaValidationRejectsMissingUniqueConstraintOnSettingsTable()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("settings.db"));

        // Create software_settings table without UNIQUE(name_en)
        QVERIFY(executeSql(path,
                           "PRAGMA user_version = 1; "
                           "CREATE TABLE software_settings (name_en TEXT NOT NULL, value TEXT NOT NULL, mtime INTEGER NOT NULL);"));

        dltool::database::SettingsDataBase database(path);
        QString                           error;
        const QVariantMap values = database.loadSettings(QStringLiteral("software_settings"), error);
        QVERIFY(values.isEmpty());
        QVERIFY(error.contains(QStringLiteral("software_settings")));
        QVERIFY(error.contains(QStringLiteral("唯一")));
    }

    void migrationFailureRollsBackStructureDataAndVersionViaProjectDataBase()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("project.dlpro"));

        // Create an unmigrated database at user_version = 0 with a pre-existing project row
        QVERIFY(executeSql(path,
                           "CREATE TABLE project (id INTEGER NOT NULL PRIMARY KEY, name TEXT, method INTEGER NOT NULL, "
                           "description TEXT, path TEXT, image_base_path TEXT, ctime INTEGER NOT NULL, mtime INTEGER NOT NULL, "
                           "version TEXT, extra_data BLOB); "
                           "INSERT INTO project (id, name, method, path, ctime, mtime) VALUES (1, 'initial_project', 0, '"
                           + path.toUtf8() + "', 100, 200);"));

        // Inject failure during migration
        dltool::database::detail::setMigrationHookForTest([](sqlite3 *db, dltool::database::detail::SchemaKind,
                                                             int, int, QString *err_msg) {
            // Tamper row during migration before failure
            sqlite3_exec(db, "UPDATE project SET name = 'dirty_project' WHERE id = 1", nullptr, nullptr, nullptr);
            if (err_msg != nullptr)
                *err_msg = QStringLiteral("注入迁移失败错误");
            return false;
        });

        // Trigger schema ensure via real ProjectDataBase interface
        {
            dltool::database::ProjectDataBase database(path);
            QString                           name;
            int                               method = 0;
            QString                           proj_path;
            QString                           desc;
            QString                           img_path;
            qint64                            ctime = 0;
            qint64                            mtime = 0;
            QString                           error;
            QVERIFY(!database.openProject(name, method, proj_path, desc, img_path, ctime, mtime, error));
            QVERIFY(error.contains(QStringLiteral("注入迁移失败错误")));
        }

        // Verify that rollback preserved original version 0, original data row, and created no new tables
        QCOMPARE(userVersion(path), 0);
        QCOMPARE(queryString(path, "SELECT name FROM project WHERE id = 1"), QStringLiteral("initial_project"));
        QVERIFY(!tableExistsInDb(path, QStringLiteral("datasets")));
        QVERIFY(!tableExistsInDb(path, QStringLiteral("images")));

        // Clear injection hook, now opening the real database succeeds and completes migration
        dltool::database::detail::setMigrationHookForTest(nullptr);
        {
            dltool::database::ProjectDataBase database(path);
            QString                           name;
            int                               method = 0;
            QString                           proj_path;
            QString                           desc;
            QString                           img_path;
            qint64                            ctime = 0;
            qint64                            mtime = 0;
            QString                           error;
            QVERIFY2(database.openProject(name, method, proj_path, desc, img_path, ctime, mtime, error), qPrintable(error));
            QCOMPARE(userVersion(path), 1);
            QCOMPARE(name, QStringLiteral("initial_project"));
            QVERIFY(tableExistsInDb(path, QStringLiteral("datasets")));
            QVERIFY(tableExistsInDb(path, QStringLiteral("images")));
        }
    }

    void schemaRejectsCaseSensitiveTestTaskNames()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("model.db"));
        QVariantMap params;
        QString error;
        {
            dltool::database::ModelDataBase database(path);
            QVERIFY2(database.readTrainParams(params, &error), qPrintable(error));
        }
        QVERIFY(executeSql(path,
                           "DROP TABLE test_tasks; CREATE TABLE test_tasks ("
                           "task_id TEXT NOT NULL PRIMARY KEY, name TEXT NOT NULL UNIQUE COLLATE BINARY, "
                           "ctime INTEGER NOT NULL, mtime INTEGER NOT NULL);"));
        dltool::database::ModelDataBase database(path);
        QVERIFY(!database.readTrainParams(params, &error));
    }

    void migrationFailureRollsBackViaModelDataBase()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("model.db"));

        // Create an unmigrated database at user_version = 0 with a pre-existing train_params row
        QVERIFY(executeSql(path,
                           "CREATE TABLE train_params (\"group\" TEXT NOT NULL, name_en TEXT NOT NULL, "
                           "value TEXT NOT NULL, type TEXT NOT NULL, PRIMARY KEY (\"group\", name_en)); "
                           "INSERT INTO train_params (\"group\", name_en, value, type) VALUES ('g1', 'epoch', '10', 'int');"));

        // Inject failure during migration
        dltool::database::detail::setMigrationHookForTest([](sqlite3 *db, dltool::database::detail::SchemaKind,
                                                             int, int, QString *err_msg) {
            sqlite3_exec(db, "UPDATE train_params SET value = '999' WHERE name_en = 'epoch'", nullptr, nullptr, nullptr);
            if (err_msg != nullptr)
                *err_msg = QStringLiteral("模型库注入迁移失败");
            return false;
        });

        dltool::database::ModelDataBase database(path);
        QVariantMap                    params;
        QString                        error;
        QVERIFY(!database.readTrainParams(params, &error));
        QVERIFY(error.contains(QStringLiteral("模型库注入迁移失败")));

        // Verify rollback
        QCOMPARE(userVersion(path), 0);
        QCOMPARE(queryString(path, "SELECT value FROM train_params WHERE name_en = 'epoch'"), QStringLiteral("10"));
        QVERIFY(!tableExistsInDb(path, QStringLiteral("test_tasks")));

        // Clear injection hook, migration succeeds
        dltool::database::detail::setMigrationHookForTest(nullptr);
        dltool::database::ModelDataBase database2(path);
        QVERIFY2(database2.readTrainParams(params, &error), qPrintable(error));
        QCOMPARE(userVersion(path), 1);
        QCOMPARE(queryString(path, "SELECT value FROM train_params WHERE name_en = 'epoch'"), QStringLiteral("10"));
        QVERIFY(tableExistsInDb(path, QStringLiteral("test_tasks")));
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    DatabaseSchemaTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_DatabaseSchema.moc"
