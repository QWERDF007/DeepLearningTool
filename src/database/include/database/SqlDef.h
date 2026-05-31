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
        CreateFeatureSearchSettings,
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
        {CreateFeatureSearchSettings,
         "CREATE TABLE IF NOT EXISTS feature_search_settings ("
         "id INTEGER PRIMARY KEY CHECK (id = 1),"
         "enabled INTEGER NOT NULL DEFAULT 1,"
         "model TEXT NOT NULL DEFAULT 'resnet18',"
         "model_path TEXT NOT NULL DEFAULT 'F:/models/resnet18.wts',"
         "feature_name TEXT NOT NULL DEFAULT 'layer4',"
         "rebuild_index INTEGER NOT NULL DEFAULT 0,"
         "top_k INTEGER NOT NULL DEFAULT 5,"
         "norm TEXT NOT NULL DEFAULT 'l2',"
         "preprocess_backend TEXT NOT NULL DEFAULT 'cpu',"
         "faiss_backend TEXT NOT NULL DEFAULT 'cpu',"
         "index_storage TEXT NOT NULL DEFAULT 'ram',"
         "disk_build_batch_size INTEGER NOT NULL DEFAULT 256,"
         "model_backend TEXT NOT NULL DEFAULT 'tensorrt',"
         "model_device TEXT NOT NULL DEFAULT 'gpu',"
         "index_directory TEXT NOT NULL DEFAULT '',"
         "custom_feature_names TEXT NOT NULL DEFAULT '{}')"},
        {CreateThumbnailSettings,
         "CREATE TABLE IF NOT EXISTS thumbnail_settings ("
         "id INTEGER PRIMARY KEY CHECK (id = 1),"
         "margin INTEGER NOT NULL DEFAULT 10,"
         "cache_size INTEGER NOT NULL DEFAULT 100,"
         "image_load_threads INTEGER NOT NULL DEFAULT 4,"
         "cell_scale REAL NOT NULL DEFAULT 1.0,"
         "cell_scale_from REAL NOT NULL DEFAULT 0.5,"
         "cell_scale_to REAL NOT NULL DEFAULT 4.0,"
         "cell_scale_step REAL NOT NULL DEFAULT 0.25,"
         "label_scale REAL NOT NULL DEFAULT 1.0,"
         "label_scale_from REAL NOT NULL DEFAULT 0.5,"
         "label_scale_to REAL NOT NULL DEFAULT 4.0,"
         "label_scale_step REAL NOT NULL DEFAULT 0.25,"
         "label_aspect_ratio REAL NOT NULL DEFAULT 1.0,"
         "label_aspect_ratio_from REAL NOT NULL DEFAULT 0.5,"
         "label_aspect_ratio_to REAL NOT NULL DEFAULT 2.0,"
         "label_aspect_ratio_step REAL NOT NULL DEFAULT 0.1,"
         "label_border_padding REAL NOT NULL DEFAULT 0.1,"
         "label_border_padding_from REAL NOT NULL DEFAULT 0.0,"
         "label_border_padding_to REAL NOT NULL DEFAULT 1.0,"
         "label_border_padding_step REAL NOT NULL DEFAULT 0.1)"},
        {CreateLabelDisplaySettings,
         "CREATE TABLE IF NOT EXISTS label_display_settings ("
         "id INTEGER PRIMARY KEY CHECK (id = 1),"
         "border_width INTEGER NOT NULL DEFAULT 2,"
         "fill_opacity INTEGER NOT NULL DEFAULT 30)"},
        {CreateImageEnhanceSettings,
         "CREATE TABLE IF NOT EXISTS image_enhance_settings ("
         "id INTEGER PRIMARY KEY CHECK (id = 1),"
         "brightness REAL NOT NULL DEFAULT 0.0,"
         "brightness_from REAL NOT NULL DEFAULT -1.0,"
         "brightness_to REAL NOT NULL DEFAULT 1.0,"
         "brightness_step REAL NOT NULL DEFAULT 0.1,"
         "contrast REAL NOT NULL DEFAULT 0.0,"
         "contrast_from REAL NOT NULL DEFAULT -1.0,"
         "contrast_to REAL NOT NULL DEFAULT 1.0,"
         "contrast_step REAL NOT NULL DEFAULT 0.1)"},
        {CreateUISettings,
         "CREATE TABLE IF NOT EXISTS ui_settings ("
         "id INTEGER PRIMARY KEY CHECK (id = 1),"
         "theme TEXT NOT NULL DEFAULT 'dark',"
         "language TEXT NOT NULL DEFAULT 'zh_CN')"},
        {CreateProjectSettings,
         "CREATE TABLE IF NOT EXISTS project_settings ("
         "id INTEGER PRIMARY KEY CHECK (id = 1),"
         "max_recent_projects INTEGER NOT NULL DEFAULT 10,"
         "auto_save_enabled INTEGER NOT NULL DEFAULT 1,"
         "auto_save_interval INTEGER NOT NULL DEFAULT 300)"},
    };

    // clang-format on
};

} // namespace dltool::database
