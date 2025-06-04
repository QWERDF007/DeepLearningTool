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
        CreateLabelClasses,
        CreateLabels,
        CreateTags,
    };

    // clang-format off
    
    inline static const std::unordered_map<int, std::string> SqlMap = {
        {CreateProject,
         "CREATE TABLE project (id INTEGER NOT NULL PRIMARY KEY, name TEXT, method INTEGER NOT NULL, path TEXT, description TEXT, "
         "image_base_path TEXT, ctime INTEGER NOT NULL, mtime INTEGER NOT NULL, version TEXT, extra_data BLOB)"},
        {CreateRecentProjects,
         "CREATE TABLE IF NOT EXISTS recent_projects (id INTEGER NOT NULL PRIMARY KEY, path TEXT, extra_data BLOB)"},
        {CreateDatasets, 
         "CREATE TABLE datasets (id INTEGER NOT NULL PRIMARY KEY, name TEXT, extra_data BLOB)"},
        {CreateImages,
         "CREATE TABLE images (id INTEGER NOT NULL PRIMARY KEY, dataset_id INTEGER NOT NULL REFERENCES datasets(id), path TEXT, extra_data BLOB)"},
        {CreateLabelClasses, 
         "CREATE TABLE label_classes (id INTEGER NOT NULL PRIMARY KEY, name TEXT, color TEXT, shortcut TEXT, ordinal_index INTEGER, extra_data BLOB)"},
        {CreateLabels,
         "CREATE TABLE labels (id INTEGER NOT NULL PRIMARY KEY, image_id INTEGER NOT NULL REFERENCES images(id), "
         "label_class_id INTEGER NOT NULL REFERENCES label_classes, region_type INTEGER NOT NULL, region BLOB, ordinal_index INTEGER, extra_data BLOB)"},
        {CreateTags,
         "CREATE TABLE tags (id INTEGER NOT NULL PRIMARY KEY, name TEXT, shortcut TEXT, extra_data BLOB)"},
    };

    // clang-format on
};

} // namespace dltool::data
