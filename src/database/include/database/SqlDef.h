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
        CreateFeatureSearchSettings,
        CreateRoiSearchSettings,
        CreateSmartAnnotationSettings,
        CreateThumbnailSettings,
        CreateLabelDisplaySettings,
        CreateImageEnhanceSettings,
        CreateUISettings,
        CreateProjectSettings,
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
        {CreateTagClasses,
         "CREATE TABLE tag_classes (id INTEGER NOT NULL PRIMARY KEY, name TEXT, extra_data BLOB)"},
        {CreateTags,
         "CREATE TABLE tags (id INTEGER NOT NULL PRIMARY KEY, image_id INTEGER NOT NULL REFERENCES images(id), tag_id INTEGER NOT NULL REFERENCES tag_classes(id), "
         "extra_data BLOB, UNIQUE (image_id, tag_id))"},
        {CreateModels,
         "CREATE TABLE IF NOT EXISTS models (id INTEGER NOT NULL PRIMARY KEY, name TEXT, network_structure TEXT, "
         "training_result TEXT, test_result TEXT, ctime INTEGER NOT NULL, mtime INTEGER NOT NULL, extra_data BLOB)"},
        {CreateFeatureSearchSettings,
         "CREATE TABLE IF NOT EXISTS feature_search_settings ("
         "id INTEGER PRIMARY KEY,"
         "key TEXT NOT NULL UNIQUE,"
         "value TEXT NOT NULL,"
         "mtime INTEGER NOT NULL)"},
        {CreateRoiSearchSettings,
         "CREATE TABLE IF NOT EXISTS roi_search_settings ("
         "id INTEGER PRIMARY KEY,"
         "key TEXT NOT NULL UNIQUE,"
         "value TEXT NOT NULL,"
         "mtime INTEGER NOT NULL)"},
        {CreateSmartAnnotationSettings,
         "CREATE TABLE IF NOT EXISTS smart_annotation_settings ("
         "id INTEGER PRIMARY KEY,"
         "key TEXT NOT NULL UNIQUE,"
         "value TEXT NOT NULL,"
         "mtime INTEGER NOT NULL)"},
        {CreateThumbnailSettings,
         "CREATE TABLE IF NOT EXISTS thumbnail_settings ("
         "id INTEGER PRIMARY KEY,"
         "key TEXT NOT NULL UNIQUE,"
         "value TEXT NOT NULL,"
         "mtime INTEGER NOT NULL)"},
        {CreateLabelDisplaySettings,
         "CREATE TABLE IF NOT EXISTS label_display_settings ("
         "id INTEGER PRIMARY KEY,"
         "key TEXT NOT NULL UNIQUE,"
         "value TEXT NOT NULL,"
         "mtime INTEGER NOT NULL)"},
        {CreateImageEnhanceSettings,
         "CREATE TABLE IF NOT EXISTS image_enhance_settings ("
         "id INTEGER PRIMARY KEY,"
         "key TEXT NOT NULL UNIQUE,"
         "value TEXT NOT NULL,"
         "mtime INTEGER NOT NULL)"},
        {CreateUISettings,
         "CREATE TABLE IF NOT EXISTS ui_settings ("
         "id INTEGER PRIMARY KEY,"
         "key TEXT NOT NULL UNIQUE,"
         "value TEXT NOT NULL,"
         "mtime INTEGER NOT NULL)"},
        {CreateProjectSettings,
         "CREATE TABLE IF NOT EXISTS project_settings ("
         "id INTEGER PRIMARY KEY,"
         "key TEXT NOT NULL UNIQUE,"
         "value TEXT NOT NULL,"
         "mtime INTEGER NOT NULL)"},
    };

    // clang-format on
};

} // namespace dltool::database
