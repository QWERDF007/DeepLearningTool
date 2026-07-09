#pragma once

#include <string>

namespace dltool::database::ddl {

inline std::string createSettingsTableSql(const std::string &table_name)
{
    return "CREATE TABLE IF NOT EXISTS " + table_name
         + " ("
           "name_en TEXT NOT NULL UNIQUE,"
           "value TEXT NOT NULL,"
           "mtime INTEGER NOT NULL)";
}

} // namespace dltool::database::ddl
