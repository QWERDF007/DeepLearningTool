#pragma once

#include "dltool/database/Export.h"

#include <sqlpp11/connection_pool.h>
#include <sqlpp11/sqlite3/connection.h>

#include <QString>
#include <functional>

struct sqlite3;

namespace dltool::database::detail {

enum class SchemaKind
{
    Project,
    RecentProjects,
    Model,
    Task,
    Settings,
};

using MigrationHook = std::function<bool(sqlite3 *db, SchemaKind kind, int from_version, int to_version, QString *err_msg)>;
DATABASE_API void setMigrationHookForTest(MigrationHook hook);

DATABASE_API bool ensureSchema(sqlite3 *db, SchemaKind kind, QString *err_msg = nullptr);

inline bool ensureSchema(sqlpp::pooled_connection<sqlpp::sqlite3::connection_base> &db, SchemaKind kind,
                         QString *err_msg = nullptr)
{
    return ensureSchema(db.native_handle(), kind, err_msg);
}

DATABASE_API bool ensureSettingsTable(sqlpp::pooled_connection<sqlpp::sqlite3::connection_base> &db,
                                      const QString &table_name, QString *err_msg = nullptr);

inline bool ensureProjectSchema(sqlpp::pooled_connection<sqlpp::sqlite3::connection_base> &db,
                                QString *err_msg = nullptr)
{
    return ensureSchema(db, SchemaKind::Project, err_msg);
}

inline bool ensureRecentProjectsSchema(sqlpp::pooled_connection<sqlpp::sqlite3::connection_base> &db,
                                       QString *err_msg = nullptr)
{
    return ensureSchema(db, SchemaKind::RecentProjects, err_msg);
}

inline bool ensureSettingsSchema(sqlpp::pooled_connection<sqlpp::sqlite3::connection_base> &db,
                                 QString *err_msg = nullptr)
{
    return ensureSchema(db, SchemaKind::Settings, err_msg);
}

} // namespace dltool::database::detail
