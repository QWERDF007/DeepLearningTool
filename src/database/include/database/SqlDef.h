#pragma once

#include <string>
#include <unordered_map>

namespace dltool::database {

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
        CreateTagClasses,
        CreateTags,
        CreateModels,
        CreateTrainParams,
        CreateTestParams,
        CreateModelDatasets,
        CreateTestTasks,
        CreateTaskInfo,
        CreatePrediction,
    };

    // clang-format off

    inline static const std::unordered_map<int, std::string> SqlMap = {
        {CreateProject,
         "CREATE TABLE project (id INTEGER NOT NULL PRIMARY KEY, name TEXT, method INTEGER NOT NULL, path TEXT, description TEXT, "
         "image_base_path TEXT, ctime INTEGER NOT NULL, mtime INTEGER NOT NULL, version TEXT, extra_data BLOB)"},
        {CreateRecentProjects,
         "CREATE TABLE IF NOT EXISTS recent_projects (id INTEGER NOT NULL PRIMARY KEY, path TEXT, extra_data BLOB)"},
        {CreateDatasets,
         "CREATE TABLE datasets (id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, name TEXT, extra_data BLOB)"},
        {CreateImages,
         "CREATE TABLE images (id INTEGER NOT NULL PRIMARY KEY, dataset_id INTEGER NOT NULL REFERENCES datasets(id), path TEXT, extra_data BLOB)"},
        {CreateLabelClasses,
         "CREATE TABLE label_classes (id INTEGER NOT NULL PRIMARY KEY, name TEXT, color TEXT, shortcut TEXT, ordinal_index INTEGER, extra_data BLOB)"},
        {CreateLabels,
         "CREATE TABLE labels (id INTEGER NOT NULL PRIMARY KEY, image_id INTEGER NOT NULL REFERENCES images(id), "
         "label_class_id INTEGER NOT NULL REFERENCES label_classes, region_type INTEGER NOT NULL, region BLOB, ordinal_index INTEGER, extra_data BLOB)"},
        {CreateTagClasses,
         "CREATE TABLE tag_classes (id INTEGER NOT NULL PRIMARY KEY, name TEXT, extra_data BLOB)"},
        {CreateTags,
         "CREATE TABLE tags (id INTEGER NOT NULL PRIMARY KEY, image_id INTEGER REFERENCES images(id), "
         "label_id INTEGER REFERENCES labels(id), tag_ids BLOB NOT NULL, type INTEGER NOT NULL, extra_data BLOB, "
         "CHECK ((type = 0 AND image_id IS NOT NULL AND label_id IS NULL) OR "
         "(type = 1 AND image_id IS NULL AND label_id IS NOT NULL)), "
         "UNIQUE (image_id), UNIQUE (label_id))"},
        {CreateModels,
         "CREATE TABLE IF NOT EXISTS models (id INTEGER NOT NULL PRIMARY KEY, uuid TEXT NOT NULL UNIQUE, name TEXT, "
         "framework_name TEXT, model_architecture TEXT, ctime INTEGER NOT NULL, mtime INTEGER NOT NULL, extra_data BLOB)"},
        {CreateTrainParams,
         "CREATE TABLE IF NOT EXISTS train_params (\"group\" TEXT NOT NULL, name_en TEXT NOT NULL, value TEXT NOT NULL, "
         "type TEXT NOT NULL, PRIMARY KEY (\"group\", name_en))"},
        {CreateTestParams,
         "CREATE TABLE IF NOT EXISTS test_params (\"group\" TEXT NOT NULL, name_en TEXT NOT NULL, value TEXT NOT NULL, "
         "type TEXT NOT NULL, PRIMARY KEY (\"group\", name_en))"},
        {CreateModelDatasets,
         "CREATE TABLE IF NOT EXISTS datasets (type TEXT NOT NULL, dataset_id INTEGER NOT NULL, class_ids TEXT NOT NULL, "
         "PRIMARY KEY (type, dataset_id))"},
        {CreateTestTasks,
         "CREATE TABLE IF NOT EXISTS test_tasks (task_id TEXT NOT NULL PRIMARY KEY, name TEXT NOT NULL UNIQUE COLLATE "
         "NOCASE, ctime INTEGER NOT NULL, mtime INTEGER NOT NULL)"},
        {CreateTaskInfo,
         "CREATE TABLE IF NOT EXISTS task_info (task_id TEXT NOT NULL PRIMARY KEY, ctime INTEGER NOT NULL, "
         "mtime INTEGER NOT NULL)"},
        {CreatePrediction,
         "CREATE TABLE IF NOT EXISTS prediction (image_id INTEGER NOT NULL PRIMARY KEY, data TEXT NOT NULL)"},
    };

    // clang-format on
};

} // namespace dltool::database
