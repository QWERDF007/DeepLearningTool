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
        CreateDatasets,
        CreateImages,
    };

    // clang-format off
    
    inline static const std::unordered_map<int, std::string> SqlMap = {
        {CreateProject,
         "CREATE TABLE project (id INTEGER NOT NULL PRIMARY KEY, name TEXT, method INTEGER NOT NULL, path TEXT, "
         "description TEXT, image_base_path TEXT, ctime INTEGER NOT NULL, mtime INTEGER NOT NULL, program_version TEXT, extra_data BLOB)"},
        {CreateRecentProjects,
         "CREATE TABLE IF NOT EXISTS recent_projects (id INTEGER NOT NULL PRIMARY KEY, path TEXT)"},
        {CreateDatasets, 
         "CREATE TABLE datasets (id INTEGER NOT NULL PRIMARY KEY, name TEXT, extra_data BLOB)"},
        {CreateImages,
         "CREATE TABLE images (id INTEGER NOT NULL PRIMARY KEY, dataset_id INTEGER NOT NULL REFERENCES datasets(id), path TEXT, extra_data BLOB)"},
    };

    // clang-format on
};

} // namespace dltool::data
