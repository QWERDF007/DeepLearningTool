#include "DatabaseSchema.h"

#include <QFile>
#include <QRegularExpression>
#include <sqlite3.h>

#include <initializer_list>
#include <vector>

namespace dltool::database::detail {

namespace {

constexpr int kCurrentSchemaVersion = 1;

struct ColumnSpec
{
    const char *name;
    const char *type;
    int         not_null;
    int         primary_key;
};

struct TableSpec
{
    QString                   name;
    std::vector<ColumnSpec>   columns;
};

bool setError(QString *err_msg, const QString &message)
{
    if (err_msg != nullptr)
        *err_msg = message;
    return false;
}

QString sqliteMessage(sqlite3 *db, const QString &fallback)
{
    if (db == nullptr)
        return fallback;
    const char *message = sqlite3_errmsg(db);
    return message != nullptr && *message != '\0' ? QString::fromUtf8(message) : fallback;
}

bool exec(sqlite3 *db, const char *sql, QString *err_msg)
{
    char *sqlite_error = nullptr;
    const int result = sqlite3_exec(db, sql, nullptr, nullptr, &sqlite_error);
    if (result == SQLITE_OK)
        return true;

    const QString message = sqlite_error != nullptr ? QString::fromUtf8(sqlite_error)
                                                     : sqliteMessage(db, QStringLiteral("SQLite 执行失败"));
    if (sqlite_error != nullptr)
        sqlite3_free(sqlite_error);
    return setError(err_msg, message);
}

bool readUserVersion(sqlite3 *db, int &version, QString *err_msg)
{
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &statement, nullptr) != SQLITE_OK)
        return setError(err_msg, sqliteMessage(db, QStringLiteral("读取数据库 schema 版本失败")));

    const int step_result = sqlite3_step(statement);
    if (step_result != SQLITE_ROW)
    {
        sqlite3_finalize(statement);
        return setError(err_msg, sqliteMessage(db, QStringLiteral("读取数据库 schema 版本失败")));
    }

    version = sqlite3_column_int(statement, 0);
    sqlite3_finalize(statement);
    return true;
}

bool isValidTableName(const QString &table_name)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    return pattern.match(table_name).hasMatch();
}

bool tableExists(sqlite3 *db, const QString &table_name, bool &exists, QString *err_msg)
{
    sqlite3_stmt *statement = nullptr;
    constexpr char query[] = "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?1 LIMIT 1";
    if (sqlite3_prepare_v2(db, query, -1, &statement, nullptr) != SQLITE_OK)
        return setError(err_msg, sqliteMessage(db, QStringLiteral("查询数据库表失败")));

    const QByteArray table_name_utf8 = table_name.toUtf8();
    sqlite3_bind_text(statement, 1, table_name_utf8.constData(), -1, SQLITE_TRANSIENT);
    const int step_result = sqlite3_step(statement);
    if (step_result != SQLITE_ROW && step_result != SQLITE_DONE)
    {
        sqlite3_finalize(statement);
        return setError(err_msg, sqliteMessage(db, QStringLiteral("查询数据库表失败")));
    }
    exists = step_result == SQLITE_ROW;
    sqlite3_finalize(statement);
    return true;
}

bool executeResource(sqlite3 *db, const char *resource_name, QString *err_msg)
{
    const QString resource_path = QStringLiteral(":/dltool/database/schema/%1").arg(QString::fromLatin1(resource_name));
    QFile       file(resource_path);
    if (!file.open(QIODevice::ReadOnly))
        return setError(err_msg, QStringLiteral("读取 schema 资源失败: %1").arg(resource_path));

    const QByteArray sql = file.readAll();
    if (sql.trimmed().isEmpty())
        return setError(err_msg, QStringLiteral("schema 资源为空: %1").arg(resource_path));
    return exec(db, sql.constData(), err_msg);
}

bool executeSettingsTableResource(sqlite3 *db, const QString &table_name, QString *err_msg)
{
    constexpr char resource_name[] = "create_settings_table_template.sql";
    const QString resource_path
        = QStringLiteral(":/dltool/database/schema/%1").arg(QString::fromLatin1(resource_name));
    QFile file(resource_path);
    if (!file.open(QIODevice::ReadOnly))
        return setError(err_msg, QStringLiteral("读取 schema 资源失败: %1").arg(resource_path));

    QByteArray sql = file.readAll();
    if (sql.trimmed().isEmpty())
        return setError(err_msg, QStringLiteral("schema 资源为空: %1").arg(resource_path));

    const QByteArray table_name_utf8 = table_name.toUtf8();
    sql.replace(QByteArrayLiteral("{table_name}"), table_name_utf8);
    if (sql.contains(QByteArrayLiteral("{table_name}")))
        return setError(err_msg, QStringLiteral("schema 资源缺少有效的表名占位符: %1").arg(resource_path));
    return exec(db, sql.constData(), err_msg);
}

std::vector<TableSpec> tablesFor(SchemaKind kind)
{
    const auto column = [](const char *name, const char *type, int not_null, int primary_key)
    { return ColumnSpec{name, type, not_null, primary_key}; };
    const auto table = [](const char *name, std::initializer_list<ColumnSpec> columns)
    { return TableSpec{QString::fromLatin1(name), std::vector<ColumnSpec>(columns)}; };

    switch (kind)
    {
    case SchemaKind::Project:
        return {
            table("project", {column("id", "INTEGER", 1, 1), column("name", "TEXT", 0, 0),
                               column("method", "INTEGER", 1, 0), column("description", "TEXT", 0, 0),
                               column("path", "TEXT", 0, 0), column("image_base_path", "TEXT", 0, 0),
                               column("ctime", "INTEGER", 1, 0), column("mtime", "INTEGER", 1, 0),
                               column("version", "TEXT", 0, 0), column("extra_data", "BLOB", 0, 0)}),
            table("datasets", {column("id", "INTEGER", 1, 1), column("name", "TEXT", 0, 0),
                                column("extra_data", "BLOB", 0, 0)}),
            table("images", {column("id", "INTEGER", 1, 1), column("dataset_id", "INTEGER", 1, 0),
                              column("path", "TEXT", 0, 0), column("extra_data", "BLOB", 0, 0)}),
            table("label_classes", {column("id", "INTEGER", 1, 1), column("name", "TEXT", 0, 0),
                                     column("color", "TEXT", 0, 0), column("shortcut", "TEXT", 0, 0),
                                     column("ordinal_index", "INTEGER", 0, 0), column("extra_data", "BLOB", 0, 0)}),
            table("labels", {column("id", "INTEGER", 1, 1), column("image_id", "INTEGER", 1, 0),
                              column("label_class_id", "INTEGER", 1, 0), column("region_type", "INTEGER", 1, 0),
                              column("region", "BLOB", 0, 0), column("ordinal_index", "INTEGER", 0, 0),
                              column("extra_data", "BLOB", 0, 0)}),
            table("tag_classes", {column("id", "INTEGER", 1, 1), column("name", "TEXT", 0, 0),
                                   column("extra_data", "BLOB", 0, 0)}),
            table("tags", {column("id", "INTEGER", 1, 1), column("image_id", "INTEGER", 0, 0),
                            column("label_id", "INTEGER", 0, 0), column("tag_ids", "BLOB", 1, 0),
                            column("type", "INTEGER", 1, 0), column("extra_data", "BLOB", 0, 0)}),
            table("models", {column("id", "INTEGER", 1, 1), column("uuid", "TEXT", 1, 0),
                              column("name", "TEXT", 0, 0), column("framework_name", "TEXT", 0, 0),
                              column("model_architecture", "TEXT", 0, 0), column("ctime", "INTEGER", 1, 0),
                              column("mtime", "INTEGER", 1, 0), column("extra_data", "BLOB", 0, 0)}),
        };
    case SchemaKind::RecentProjects:
        return {table("recent_projects", {column("id", "INTEGER", 1, 1), column("path", "TEXT", 0, 0),
                                           column("extra_data", "BLOB", 0, 0)})};
    case SchemaKind::Model:
        return {
            table("train_params", {column("group", "TEXT", 1, 1), column("name_en", "TEXT", 1, 2),
                                    column("value", "TEXT", 1, 0), column("type", "TEXT", 1, 0)}),
            table("datasets", {column("type", "TEXT", 1, 1), column("dataset_id", "INTEGER", 1, 2),
                                column("class_ids", "TEXT", 1, 0)}),
            table("test_tasks", {column("task_id", "TEXT", 1, 1), column("name", "TEXT", 1, 0),
                                  column("ctime", "INTEGER", 1, 0), column("mtime", "INTEGER", 1, 0)}),
        };
    case SchemaKind::Task:
        return {
            table("task_info", {column("task_id", "TEXT", 1, 1), column("ctime", "INTEGER", 1, 0),
                                 column("mtime", "INTEGER", 1, 0)}),
            table("test_params", {column("group", "TEXT", 1, 1), column("name_en", "TEXT", 1, 2),
                                   column("value", "TEXT", 1, 0), column("type", "TEXT", 1, 0)}),
            table("datasets", {column("type", "TEXT", 1, 1), column("dataset_id", "INTEGER", 1, 2),
                                column("class_ids", "TEXT", 1, 0)}),
            table("prediction", {column("image_id", "INTEGER", 1, 1), column("data", "TEXT", 1, 0)}),
        };
    case SchemaKind::Settings:
        return {};
    }
    return {};
}

TableSpec settingsTableSpec(const QString &table_name)
{
    const auto column = [](const char *name, const char *type, int not_null, int primary_key)
    { return ColumnSpec{name, type, not_null, primary_key}; };
    return TableSpec{table_name,
                     {column("name_en", "TEXT", 1, 0), column("value", "TEXT", 1, 0),
                      column("mtime", "INTEGER", 1, 0)}};
}

struct ResourceSpec
{
    const char *resource_name;
    const char *table_name;
};

std::vector<ResourceSpec> resourcesFor(SchemaKind kind)
{
    switch (kind)
    {
    case SchemaKind::Project:
        return {{"create_project.sql", "project"}, {"create_datasets.sql", "datasets"},
                {"create_images.sql", "images"}, {"create_label_classes.sql", "label_classes"},
                {"create_labels.sql", "labels"}, {"create_tag_classes.sql", "tag_classes"},
                {"create_tags.sql", "tags"}, {"create_models.sql", "models"}};
    case SchemaKind::RecentProjects:
        return {{"create_recent_projects.sql", "recent_projects"}};
    case SchemaKind::Model:
        return {{"create_train_params.sql", "train_params"}, {"create_model_datasets.sql", "datasets"},
                {"create_test_tasks.sql", "test_tasks"}};
    case SchemaKind::Task:
        return {{"create_task_info.sql", "task_info"}, {"create_test_params.sql", "test_params"},
                {"create_model_datasets.sql", "datasets"}, {"create_prediction.sql", "prediction"}};
    case SchemaKind::Settings:
        return {};
    }
    return {};
}

bool validateTable(sqlite3 *db, const TableSpec &spec, QString *err_msg)
{
    const QString query = QStringLiteral("PRAGMA table_info('%1')").arg(spec.name);
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db, query.toUtf8().constData(), -1, &statement, nullptr) != SQLITE_OK)
        return setError(err_msg, sqliteMessage(db, QStringLiteral("读取表结构失败: %1").arg(spec.name)));

    std::size_t index = 0;
    while (sqlite3_step(statement) == SQLITE_ROW)
    {
        if (index >= spec.columns.size())
        {
            sqlite3_finalize(statement);
            return setError(err_msg, QStringLiteral("schema 表 %1 包含未知字段").arg(spec.name));
        }

        const ColumnSpec &expected = spec.columns[index];
        const char *actual_name = reinterpret_cast<const char *>(sqlite3_column_text(statement, 1));
        const char *actual_type = reinterpret_cast<const char *>(sqlite3_column_text(statement, 2));
        const QString name = actual_name != nullptr ? QString::fromUtf8(actual_name) : QString();
        const QString type = actual_type != nullptr ? QString::fromUtf8(actual_type).trimmed().toUpper() : QString();
        if (name != QString::fromLatin1(expected.name) || type != QString::fromLatin1(expected.type)
            || sqlite3_column_int(statement, 3) != expected.not_null
            || sqlite3_column_int(statement, 5) != expected.primary_key)
        {
            sqlite3_finalize(statement);
            return setError(err_msg, QStringLiteral("schema 表 %1 字段 %2 结构不匹配")
                                          .arg(spec.name, QString::fromLatin1(expected.name)));
        }
        ++index;
    }
    const int finalize_result = sqlite3_finalize(statement);
    if (finalize_result != SQLITE_OK)
        return setError(err_msg, sqliteMessage(db, QStringLiteral("读取表结构失败: %1").arg(spec.name)));
    if (index != spec.columns.size())
        return setError(err_msg, QStringLiteral("schema 表 %1 缺少字段 %2")
                                      .arg(spec.name, QString::fromLatin1(spec.columns[index].name)));
    return true;
}

bool validateSchema(sqlite3 *db, SchemaKind kind, QString *err_msg)
{
    for (const TableSpec &table : tablesFor(kind))
    {
        bool exists = false;
        if (!tableExists(db, table.name, exists, err_msg))
            return false;
        if (!exists)
            return setError(err_msg, QStringLiteral("schema 缺少表: %1").arg(table.name));
        if (!validateTable(db, table, err_msg))
            return false;
    }
    return true;
}

bool setUserVersion(sqlite3 *db, int version, QString *err_msg)
{
    const QByteArray query = QByteArrayLiteral("PRAGMA user_version = ") + QByteArray::number(version);
    return exec(db, query.constData(), err_msg);
}

} // namespace

bool ensureSchema(sqlite3 *native_db, SchemaKind kind, QString *err_msg)
{
    if (native_db == nullptr)
        return setError(err_msg, QStringLiteral("SQLite 连接为空"));

    int version = 0;
    if (!readUserVersion(native_db, version, err_msg))
        return false;
    if (version < 0 || version > kCurrentSchemaVersion)
        return setError(err_msg, QStringLiteral("不支持的数据库 schema 版本: %1").arg(version));

    if (version == kCurrentSchemaVersion)
        return validateSchema(native_db, kind, err_msg);

    if (!exec(native_db, "BEGIN IMMEDIATE", err_msg))
        return false;

    bool committed = false;
    const auto rollback = [&]
    {
        if (!committed)
            exec(native_db, "ROLLBACK", nullptr);
    };

    for (const ResourceSpec &resource : resourcesFor(kind))
    {
        bool exists = false;
        if (!tableExists(native_db, QString::fromLatin1(resource.table_name), exists, err_msg))
        {
            rollback();
            return false;
        }
        if (!exists && !executeResource(native_db, resource.resource_name, err_msg))
        {
            rollback();
            return false;
        }
    }

    if (!validateSchema(native_db, kind, err_msg))
    {
        rollback();
        return false;
    }
    if (!setUserVersion(native_db, kCurrentSchemaVersion, err_msg))
    {
        rollback();
        return false;
    }
    if (!exec(native_db, "COMMIT", err_msg))
    {
        rollback();
        return false;
    }
    committed = true;
    return true;
}

bool ensureSettingsTable(sqlpp::pooled_connection<sqlpp::sqlite3::connection_base> &db, const QString &table_name,
                         QString *err_msg)
{
    if (!isValidTableName(table_name))
        return setError(err_msg, QStringLiteral("invalid settings table name: %1").arg(table_name));

    sqlite3 *native_db = db.native_handle();
    if (native_db == nullptr)
        return setError(err_msg, QStringLiteral("SQLite 连接为空"));
    if (!ensureSchema(native_db, SchemaKind::Settings, err_msg))
        return false;

    if (!exec(native_db, "BEGIN IMMEDIATE", err_msg))
        return false;

    bool committed = false;
    const auto rollback = [&]
    {
        if (!committed)
            exec(native_db, "ROLLBACK", nullptr);
    };

    bool exists = false;
    if (!tableExists(native_db, table_name, exists, err_msg))
    {
        rollback();
        return false;
    }
    if (!exists && !executeSettingsTableResource(native_db, table_name, err_msg))
    {
        rollback();
        return false;
    }

    if (!validateTable(native_db, settingsTableSpec(table_name), err_msg))
    {
        rollback();
        return false;
    }
    if (!exec(native_db, "COMMIT", err_msg))
    {
        rollback();
        return false;
    }
    committed = true;
    return true;
}

} // namespace dltool::database::detail
