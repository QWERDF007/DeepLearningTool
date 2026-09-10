#include "database/DatabaseSchema.h"

#include <QFile>
#include <QRegularExpression>
#include <QStringList>
#include <sqlite3.h>

#include <algorithm>
#include <initializer_list>
#include <map>
#include <mutex>
#include <vector>

namespace dltool::database::detail {

namespace {

constexpr int kCurrentSchemaVersion = 1;

struct ColumnSpec
{
    QString name;
    QString type;
    int     not_null{0};
    int     primary_key{0};
};

struct ForeignKeySpec
{
    QString from_column;
    QString target_table;
    QString target_column;
};

struct UniqueConstraintSpec
{
    std::vector<QString> columns;
    std::vector<QString> collations;
};

struct IndexSpec
{
    QString              name;
    bool                 unique{false};
    std::vector<QString> columns;
};

struct TableSpec
{
    QString                           name;
    QString                           type{QStringLiteral("table")};
    std::vector<ColumnSpec>           columns;
    std::vector<ForeignKeySpec>       foreign_keys;
    std::vector<UniqueConstraintSpec> unique_constraints;
    std::vector<IndexSpec>            indexes;
};

struct ResourceSpec
{
    const char *resource_name;
    const char *table_name;
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

TableSpec deriveTableSpec(sqlite3 *db, const QString &table_name)
{
    TableSpec spec;
    spec.name = table_name;
    spec.type = QStringLiteral("table");

    // Table type from sqlite_master
    {
        sqlite3_stmt *stmt = nullptr;
        constexpr char type_query[] = "SELECT type FROM sqlite_master WHERE name = ?1 LIMIT 1";
        if (sqlite3_prepare_v2(db, type_query, -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_text(stmt, 1, table_name.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW)
            {
                const char *t = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
                if (t != nullptr)
                    spec.type = QString::fromUtf8(t);
            }
            sqlite3_finalize(stmt);
        }
    }

    // Columns from PRAGMA table_info
    {
        const QString query = QStringLiteral("PRAGMA table_info('%1')").arg(table_name);
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, query.toUtf8().constData(), -1, &stmt, nullptr) == SQLITE_OK)
        {
            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                const char *col_name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
                const char *col_type = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
                ColumnSpec col;
                col.name = col_name != nullptr ? QString::fromUtf8(col_name) : QString();
                col.type = col_type != nullptr ? QString::fromUtf8(col_type).trimmed().toUpper() : QString();
                col.not_null = sqlite3_column_int(stmt, 3);
                col.primary_key = sqlite3_column_int(stmt, 5);
                spec.columns.push_back(std::move(col));
            }
            sqlite3_finalize(stmt);
        }
    }

    // Foreign keys from PRAGMA foreign_key_list
    {
        const QString query = QStringLiteral("PRAGMA foreign_key_list('%1')").arg(table_name);
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, query.toUtf8().constData(), -1, &stmt, nullptr) == SQLITE_OK)
        {
            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                const char *tgt_tbl = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
                const char *from_c = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
                const char *to_c = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
                ForeignKeySpec fk;
                fk.target_table = tgt_tbl != nullptr ? QString::fromUtf8(tgt_tbl) : QString();
                fk.from_column = from_c != nullptr ? QString::fromUtf8(from_c) : QString();
                fk.target_column = to_c != nullptr ? QString::fromUtf8(to_c) : QString();
                spec.foreign_keys.push_back(std::move(fk));
            }
            sqlite3_finalize(stmt);
        }
    }

    // Unique constraints and indexes from PRAGMA index_list & PRAGMA index_info
    {
        const QString query = QStringLiteral("PRAGMA index_list('%1')").arg(table_name);
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, query.toUtf8().constData(), -1, &stmt, nullptr) == SQLITE_OK)
        {
            struct IdxMeta { QString name; int unique; QString origin; };
            std::vector<IdxMeta> idx_metas;
            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                const char *idx_n = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
                int is_uniq = sqlite3_column_int(stmt, 2);
                const char *orig = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
                idx_metas.push_back({
                    idx_n != nullptr ? QString::fromUtf8(idx_n) : QString(),
                    is_uniq,
                    orig != nullptr ? QString::fromUtf8(orig) : QString()
                });
            }
            sqlite3_finalize(stmt);

            for (const auto &meta : idx_metas)
            {
                const QString info_query = QStringLiteral("PRAGMA index_xinfo('%1')").arg(meta.name);
                sqlite3_stmt *info_stmt = nullptr;
                std::vector<QString> cols;
                std::vector<QString> collations;
                if (sqlite3_prepare_v2(db, info_query.toUtf8().constData(), -1, &info_stmt, nullptr) == SQLITE_OK)
                {
                    while (sqlite3_step(info_stmt) == SQLITE_ROW)
                    {
                        if (sqlite3_column_int(info_stmt, 5) == 0)
                            continue;
                        const char *cn = reinterpret_cast<const char *>(sqlite3_column_text(info_stmt, 2));
                        if (cn != nullptr)
                        {
                            cols.push_back(QString::fromUtf8(cn));
                            collations.push_back(QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(info_stmt, 4))).toUpper());
                        }
                    }
                    sqlite3_finalize(info_stmt);
                }

                if (meta.unique != 0 && meta.origin != QStringLiteral("pk"))
                {
                    spec.unique_constraints.push_back(UniqueConstraintSpec{cols, collations});
                }
                if (meta.origin == QStringLiteral("c"))
                {
                    spec.indexes.push_back(IndexSpec{meta.name, meta.unique != 0, cols});
                }
            }
        }
    }

    return spec;
}

std::vector<TableSpec> deriveCanonicalTablesFor(SchemaKind kind)
{
    if (kind == SchemaKind::Settings)
        return {};

    sqlite3 *mem_db = nullptr;
    if (sqlite3_open_v2(":memory:", &mem_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK)
    {
        if (mem_db != nullptr)
            sqlite3_close(mem_db);
        return {};
    }

    QString err;
    for (const ResourceSpec &resource : resourcesFor(kind))
    {
        if (!executeResource(mem_db, resource.resource_name, &err))
        {
            sqlite3_close(mem_db);
            return {};
        }
    }

    std::vector<TableSpec> specs;
    for (const ResourceSpec &resource : resourcesFor(kind))
    {
        TableSpec spec = deriveTableSpec(mem_db, QString::fromLatin1(resource.table_name));
        if (spec.columns.empty())
        {
            sqlite3_close(mem_db);
            return {};
        }
        specs.push_back(std::move(spec));
    }

    sqlite3_close(mem_db);
    return specs;
}

const std::vector<TableSpec> &tablesFor(SchemaKind kind)
{
    static std::map<SchemaKind, std::vector<TableSpec>> s_cache;
    static std::mutex s_mutex;
    std::lock_guard<std::mutex> lock(s_mutex);
    auto it = s_cache.find(kind);
    if (it != s_cache.end())
        return it->second;
    s_cache[kind] = deriveCanonicalTablesFor(kind);
    return s_cache[kind];
}

TableSpec settingsTableSpec(const QString &table_name)
{
    static TableSpec s_template_spec = []() {
        sqlite3 *mem_db = nullptr;
        if (sqlite3_open_v2(":memory:", &mem_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK)
            return TableSpec{};
        QString err;
        if (!executeSettingsTableResource(mem_db, QStringLiteral("__template__"), &err))
        {
            sqlite3_close(mem_db);
            return TableSpec{};
        }
        TableSpec spec = deriveTableSpec(mem_db, QStringLiteral("__template__"));
        sqlite3_close(mem_db);
        return spec;
    }();

    TableSpec spec = s_template_spec;
    spec.name = table_name;
    return spec;
}

bool validateTable(sqlite3 *db, const TableSpec &spec, QString *err_msg)
{
    sqlite3_stmt *statement = nullptr;
    constexpr char type_query[] = "SELECT type FROM sqlite_master WHERE name = ?1 LIMIT 1";
    if (sqlite3_prepare_v2(db, type_query, -1, &statement, nullptr) != SQLITE_OK)
        return setError(err_msg, sqliteMessage(db, QStringLiteral("查询数据库表失败")));

    const QByteArray name_utf8 = spec.name.toUtf8();
    sqlite3_bind_text(statement, 1, name_utf8.constData(), -1, SQLITE_TRANSIENT);
    const int step_result = sqlite3_step(statement);
    if (step_result != SQLITE_ROW)
    {
        sqlite3_finalize(statement);
        return setError(err_msg, QStringLiteral("schema 缺少表: %1").arg(spec.name));
    }
    const char *actual_type_str = reinterpret_cast<const char *>(sqlite3_column_text(statement, 0));
    const QString actual_type = actual_type_str != nullptr ? QString::fromUtf8(actual_type_str) : QString();
    sqlite3_finalize(statement);
    if (actual_type != spec.type)
    {
        return setError(err_msg, QStringLiteral("schema 对象 %1 类型不匹配，期望 %2，实际为 %3")
                                      .arg(spec.name, spec.type, actual_type));
    }

    const QString query = QStringLiteral("PRAGMA table_info('%1')").arg(spec.name);
    statement = nullptr;
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
        const char *actual_type_c = reinterpret_cast<const char *>(sqlite3_column_text(statement, 2));
        const QString name = actual_name != nullptr ? QString::fromUtf8(actual_name) : QString();
        const QString type = actual_type_c != nullptr ? QString::fromUtf8(actual_type_c).trimmed().toUpper() : QString();
        if (name != expected.name || type != expected.type
            || sqlite3_column_int(statement, 3) != expected.not_null
            || sqlite3_column_int(statement, 5) != expected.primary_key)
        {
            sqlite3_finalize(statement);
            return setError(err_msg, QStringLiteral("schema 表 %1 字段 %2 结构不匹配")
                                          .arg(spec.name, expected.name));
        }
        ++index;
    }
    const int finalize_result = sqlite3_finalize(statement);
    if (finalize_result != SQLITE_OK)
        return setError(err_msg, sqliteMessage(db, QStringLiteral("读取表结构失败: %1").arg(spec.name)));
    if (index != spec.columns.size())
        return setError(err_msg, QStringLiteral("schema 表 %1 缺少字段 %2")
                                      .arg(spec.name, spec.columns[index].name));

    if (!spec.foreign_keys.empty())
    {
        const QString fk_query = QStringLiteral("PRAGMA foreign_key_list('%1')").arg(spec.name);
        statement = nullptr;
        if (sqlite3_prepare_v2(db, fk_query.toUtf8().constData(), -1, &statement, nullptr) != SQLITE_OK)
            return setError(err_msg, sqliteMessage(db, QStringLiteral("读取表外键失败: %1").arg(spec.name)));

        std::vector<ForeignKeySpec> actual_fks;
        while (sqlite3_step(statement) == SQLITE_ROW)
        {
            const char *tgt_tbl = reinterpret_cast<const char *>(sqlite3_column_text(statement, 2));
            const char *from_c = reinterpret_cast<const char *>(sqlite3_column_text(statement, 3));
            const char *to_c = reinterpret_cast<const char *>(sqlite3_column_text(statement, 4));
            ForeignKeySpec fk;
            fk.target_table = tgt_tbl != nullptr ? QString::fromUtf8(tgt_tbl) : QString();
            fk.from_column = from_c != nullptr ? QString::fromUtf8(from_c) : QString();
            fk.target_column = to_c != nullptr ? QString::fromUtf8(to_c) : QString();
            actual_fks.push_back(std::move(fk));
        }
        sqlite3_finalize(statement);

        for (const ForeignKeySpec &expected_fk : spec.foreign_keys)
        {
            const auto it = std::find_if(actual_fks.begin(), actual_fks.end(), [&](const ForeignKeySpec &actual) {
                if (actual.from_column != expected_fk.from_column)
                    return false;
                if (actual.target_table.compare(expected_fk.target_table, Qt::CaseInsensitive) != 0)
                    return false;
                if (expected_fk.target_column.isEmpty() || actual.target_column.isEmpty())
                    return true;
                return actual.target_column.compare(expected_fk.target_column, Qt::CaseInsensitive) == 0;
            });
            if (it == actual_fks.end())
            {
                return setError(err_msg, QStringLiteral("schema 表 %1 缺少外键约束: %2 -> %3")
                                              .arg(spec.name, expected_fk.from_column, expected_fk.target_table));
            }
        }
    }

    if (!spec.unique_constraints.empty())
    {
        const QString idx_query = QStringLiteral("PRAGMA index_list('%1')").arg(spec.name);
        statement = nullptr;
        if (sqlite3_prepare_v2(db, idx_query.toUtf8().constData(), -1, &statement, nullptr) != SQLITE_OK)
            return setError(err_msg, sqliteMessage(db, QStringLiteral("读取表索引失败: %1").arg(spec.name)));

        struct IdxMeta { QString name; int unique; QString origin; };
        std::vector<IdxMeta> idx_metas;
        while (sqlite3_step(statement) == SQLITE_ROW)
        {
            // A partial index does not enforce uniqueness for every table row.
            if (sqlite3_column_int(statement, 4) != 0)
                continue;
            const char *idx_n = reinterpret_cast<const char *>(sqlite3_column_text(statement, 1));
            int is_uniq = sqlite3_column_int(statement, 2);
            const char *orig = reinterpret_cast<const char *>(sqlite3_column_text(statement, 3));
            idx_metas.push_back({
                idx_n != nullptr ? QString::fromUtf8(idx_n) : QString(),
                is_uniq,
                orig != nullptr ? QString::fromUtf8(orig) : QString()
            });
        }
        sqlite3_finalize(statement);

        std::vector<UniqueConstraintSpec> actual_unique_constraints;
        for (const auto &meta : idx_metas)
        {
            if (meta.unique != 0 && meta.origin != QStringLiteral("pk"))
            {
                const QString info_query = QStringLiteral("PRAGMA index_xinfo('%1')").arg(meta.name);
                sqlite3_stmt *info_stmt = nullptr;
                std::vector<QString> cols;
                std::vector<QString> collations;
                if (sqlite3_prepare_v2(db, info_query.toUtf8().constData(), -1, &info_stmt, nullptr) == SQLITE_OK)
                {
                    while (sqlite3_step(info_stmt) == SQLITE_ROW)
                    {
                        if (sqlite3_column_int(info_stmt, 5) == 0)
                            continue;
                        const char *cn = reinterpret_cast<const char *>(sqlite3_column_text(info_stmt, 2));
                        if (cn != nullptr)
                        {
                            cols.push_back(QString::fromUtf8(cn));
                            collations.push_back(QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(info_stmt, 4))).toUpper());
                        }
                    }
                    sqlite3_finalize(info_stmt);
                }
                actual_unique_constraints.push_back({std::move(cols), std::move(collations)});
            }
        }

        for (const UniqueConstraintSpec &expected_uq : spec.unique_constraints)
        {
            const auto it = std::find_if(actual_unique_constraints.begin(), actual_unique_constraints.end(),
                                         [&](const UniqueConstraintSpec &actual) {
                                             const auto &actual_cols = actual.columns;
                                             if (actual.collations != expected_uq.collations)
                                                 return false;
                                             if (actual_cols.size() != expected_uq.columns.size())
                                                 return false;
                                             for (std::size_t i = 0; i < expected_uq.columns.size(); ++i)
                                             {
                                                 if (actual_cols[i].compare(expected_uq.columns[i], Qt::CaseInsensitive) != 0)
                                                     return false;
                                             }
                                             return true;
                                         });
            if (it == actual_unique_constraints.end())
            {
                QStringList col_list;
                for (const auto &c : expected_uq.columns)
                    col_list.append(c);
                return setError(err_msg, QStringLiteral("schema 表 %1 缺少唯一约束: %2")
                                              .arg(spec.name, col_list.join(QStringLiteral(", "))));
            }
        }
    }

    if (!spec.indexes.empty())
    {
        const QString idx_query = QStringLiteral("PRAGMA index_list('%1')").arg(spec.name);
        statement = nullptr;
        if (sqlite3_prepare_v2(db, idx_query.toUtf8().constData(), -1, &statement, nullptr) != SQLITE_OK)
            return setError(err_msg, sqliteMessage(db, QStringLiteral("读取表索引失败: %1").arg(spec.name)));

        std::vector<QString> actual_idx_names;
        while (sqlite3_step(statement) == SQLITE_ROW)
        {
            const char *idx_n = reinterpret_cast<const char *>(sqlite3_column_text(statement, 1));
            if (idx_n != nullptr)
                actual_idx_names.push_back(QString::fromUtf8(idx_n));
        }
        sqlite3_finalize(statement);

        for (const IndexSpec &expected_idx : spec.indexes)
        {
            const auto it = std::find_if(actual_idx_names.begin(), actual_idx_names.end(),
                                         [&](const QString &n) {
                                             return n.compare(expected_idx.name, Qt::CaseInsensitive) == 0;
                                         });
            if (it == actual_idx_names.end())
            {
                return setError(err_msg, QStringLiteral("schema 表 %1 缺少索引: %2")
                                              .arg(spec.name, expected_idx.name));
            }
        }
    }

    return true;
}

bool validateSchema(sqlite3 *db, SchemaKind kind, QString *err_msg)
{
    const auto &tables = tablesFor(kind);
    if (kind != SchemaKind::Settings && tables.empty())
        return setError(err_msg, QStringLiteral("无法派生有效的 schema 正本: %1").arg(static_cast<int>(kind)));
    for (const TableSpec &table : tables)
    {
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

static MigrationHook s_migration_hook = nullptr;

void setMigrationHookForTest(MigrationHook hook)
{
    s_migration_hook = std::move(hook);
}

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

    if (s_migration_hook != nullptr)
    {
        if (!s_migration_hook(native_db, kind, version, kCurrentSchemaVersion, err_msg))
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
