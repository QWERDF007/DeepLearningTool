#pragma once

#include <string>
#include <unordered_map>

namespace dltool::data {

class SqlDef
{
public:
    enum SqlType
    {
        CreateProject,
        CreateRecentProjects,
    };

    inline static const std::unordered_map<int, std::string> SqlMap = {
        {       CreateProject,
         "CREATE TABLE project (id INTEGER NOT NULL PRIMARY KEY, name TEXT, method INTEGER NOT NULL, path TEXT, "
         "description TEXT, image_base_path TEXT, ctime INTEGER NOT NULL, mtime INTEGER NOT NULL, "
         "program_version TEXT, extra_data BLOB)"},
        {CreateRecentProjects,
         "CREATE TABLE IF NOT EXISTS recent_projects (id INTEGER NOT NULL PRIMARY KEY, path TEXT)"
         }
    };
};

} // namespace dltool::data
