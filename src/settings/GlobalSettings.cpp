#include "settings/GlobalSettings.h"

#include "database/DataBase.h"

#include <spdlog/spdlog.h>

#include <QMetaType>
#include <QTimer>
#include <QVariant>

namespace dltool::settings {

namespace {

QString settingsDatabasePath()
{
    return dltool::database::DataBase::applicationDatabasePath(QStringLiteral("settings.db"));
}

bool variantBool(const QVariant &value)
{
    if (value.userType() == QMetaType::QString)
    {
        const QString text = value.toString().trimmed().toLower();
        return text == QStringLiteral("true") || text == QStringLiteral("1") || text == QStringLiteral("yes")
               || text == QStringLiteral("on");
    }
    return value.toBool();
}

bool applyImageSearchValue(ImageSearchSettings *settings, const QString &name, const QVariant &value)
{
    if (settings == nullptr)
        return false;

    if (name == QStringLiteral("enabled"))
        settings->setEnabled(variantBool(value));
    else if (name == QStringLiteral("model"))
        settings->setModel(value.toString());
    else if (name == QStringLiteral("model_path"))
        settings->setModelPath(value.toString());
    else if (name == QStringLiteral("feature_name"))
        settings->setFeatureName(value.toString());
    else if (name == QStringLiteral("rebuild_index"))
        settings->setRebuildIndex(variantBool(value));
    else if (name == QStringLiteral("top_k"))
        settings->setTopK(value.toInt());
    else if (name == QStringLiteral("norm"))
        settings->setNorm(value.toString());
    else if (name == QStringLiteral("preprocess_backend"))
        settings->setPreprocessBackend(value.toString());
    else if (name == QStringLiteral("faiss_backend"))
        settings->setFaissBackend(value.toString());
    else if (name == QStringLiteral("index_storage"))
        settings->setIndexStorage(value.toString());
    else if (name == QStringLiteral("disk_build_batch_size"))
        settings->setDiskBuildBatchSize(value.toInt());
    else if (name == QStringLiteral("model_batch_size"))
        settings->setModelBatchSize(value.toInt());
    else if (name == QStringLiteral("model_backend"))
        settings->setModelBackend(value.toString());
    else if (name == QStringLiteral("model_device"))
        settings->setModelDevice(value.toString());
    else if (name == QStringLiteral("index_directory"))
        settings->setIndexDirectory(value.toString());
    else
        return false;

    return true;
}

} // namespace

GlobalSettings::GlobalSettings(QObject *parent)
    : QObject(parent)
    , project_settings_(new ProjectSettings(this))
    , data_settings_(new DataSettings(this))
    , advanced_settings_(new AdvancedSettings(this))
    , ui_settings_(new UISettings(this))
    , settings_catalog_(new SettingsCatalog(this))
    , settings_database_(new dltool::database::SettingsDataBase(settingsDatabasePath(), this))
    , save_timer_(new QTimer(this))
    , auto_save_enabled_(true)
{
    // 配置延迟保存定时器
    save_timer_->setSingleShot(true);
    save_timer_->setInterval(1000); // 1秒延迟
    connect(save_timer_, &QTimer::timeout, this, &GlobalSettings::save);

    // 自动加载设置
    try
    {
        load();
        spdlog::info("Settings loaded successfully from: {}", settingsDatabasePath().toUtf8().constData());
    }
    catch (const std::exception &e)
    {
        spdlog::error("Failed to load settings: {}. Using default values.", e.what());
        // 使用默认值
        reset();
    }
    catch (...)
    {
        spdlog::error("Unknown error occurred while loading settings. Using default values.");
        // 使用默认值
        reset();
    }

    // 连接自动保存信号
    connectAutoSave();
}

GlobalSettings::~GlobalSettings()
{
    // 在应用退出时保存设置
    if (auto_save_enabled_)
    {
        try
        {
            save();
            spdlog::info("Settings saved on application exit");
        }
        catch (const std::exception &e)
        {
            spdlog::error("Failed to save settings on exit: {}", e.what());
        }
        catch (...)
        {
            spdlog::error("Unknown error while saving settings on exit");
        }
    }

    // 设置数据库和子设置对象会被 Qt 的父子关系自动删除
}

void GlobalSettings::load()
{
    if (!settings_database_)
    {
        spdlog::error("Cannot load settings: database object is null");
        return;
    }

    try
    {
        // 调用所有子设置的 load 方法
        settings_catalog_->syncAndLoad(settings_database_);
        project_settings_->load(settings_database_);
        data_settings_->load(settings_database_);
        advanced_settings_->load(settings_database_);
        ui_settings_->load(settings_database_);
        syncCatalogFromTyped();

        spdlog::info("All settings loaded successfully");
    }
    catch (const std::exception &e)
    {
        spdlog::error("Exception while loading settings: {}. Using default values.", e.what());
        reset();
    }
    catch (...)
    {
        spdlog::error("Unknown exception while loading settings. Using default values.");
        reset();
    }
}

void GlobalSettings::save()
{
    if (!settings_database_)
    {
        spdlog::error("Cannot save settings: database object is null");
        return;
    }

    try
    {
        // 调用所有子设置的 save 方法
        syncCatalogFromTyped();
        settings_catalog_->save(settings_database_);

        project_settings_->save(settings_database_);
        data_settings_->save(settings_database_);
        advanced_settings_->save(settings_database_);
        ui_settings_->save(settings_database_);

        spdlog::info("Settings saved successfully to: {}", settingsDatabasePath().toUtf8().constData());
    }
    catch (const std::exception &e)
    {
        spdlog::error("Exception while saving settings: {}", e.what());
    }
    catch (...)
    {
        spdlog::error("Unknown exception while saving settings");
    }
}

void GlobalSettings::reset()
{
    // 调用所有子设置的 reset 方法
    project_settings_->reset();
    data_settings_->reset();
    advanced_settings_->reset();
    ui_settings_->reset();
    settings_catalog_->reset();
    syncCatalogFromTyped();
}

void GlobalSettings::setAutoSaveEnabled(bool enabled)
{
    auto_save_enabled_ = enabled;
    spdlog::info("Auto-save {}", enabled ? "enabled" : "disabled");
}

bool GlobalSettings::autoSaveEnabled() const
{
    return auto_save_enabled_;
}

bool GlobalSettings::setCatalogValue(const QString &group_key, const QString &name, const QVariant &value)
{
    SettingsFieldModel *model = settings_catalog_ != nullptr ? settings_catalog_->group(group_key) : nullptr;
    return model != nullptr && model->setValueForName(name, value);
}

void GlobalSettings::scheduleSave()
{
    if (!auto_save_enabled_)
    {
        return;
    }

    // 重启定时器（延迟保存）
    save_timer_->start();
}

void GlobalSettings::connectAutoSave()
{
    // 连接 ProjectSettings 的所有信号
    connect(settings_catalog_, &SettingsCatalog::fieldValueChanged, this,
            [this](const QString &group_key, const QString &name, const QVariant &value)
            {
                if (syncing_catalog_)
                    return;
                syncTypedFromCatalog(group_key, name, value);
                scheduleSave();
            });

    connect(project_settings_, &ProjectSettings::maxRecentProjectsChanged, this, &GlobalSettings::scheduleSave);
    connect(project_settings_, &ProjectSettings::autoSaveIntervalChanged, this, &GlobalSettings::scheduleSave);
    connect(project_settings_, &ProjectSettings::autoSaveEnabledChanged, this, &GlobalSettings::scheduleSave);

    // 连接 DataSettings 的所有信号
    connect(data_settings_, &DataSettings::thumbnailMarginChanged, this, &GlobalSettings::scheduleSave);
    connect(data_settings_, &DataSettings::thumbnailCacheSizeChanged, this, &GlobalSettings::scheduleSave);
    connect(data_settings_, &DataSettings::imageLoadThreadsChanged, this, &GlobalSettings::scheduleSave);
    connect(data_settings_, &DataSettings::labelBorderWidthChanged, this, &GlobalSettings::scheduleSave);
    connect(data_settings_, &DataSettings::labelFillOpacityChanged, this, &GlobalSettings::scheduleSave);
    connect(data_settings_, &DataSettings::imageCellScaleChanged, this, &GlobalSettings::scheduleSave);
    connect(data_settings_, &DataSettings::imageCellScaleFromChanged, this, &GlobalSettings::scheduleSave);
    connect(data_settings_, &DataSettings::imageCellScaleToChanged, this, &GlobalSettings::scheduleSave);
    connect(data_settings_, &DataSettings::imageCellScaleStepSizeChanged, this, &GlobalSettings::scheduleSave);
    auto *image_search = advanced_settings_->imageSearch();
    connect(image_search, &ImageSearchSettings::enabledChanged, this, &GlobalSettings::scheduleSave);
    connect(image_search, &ImageSearchSettings::modelChanged, this, &GlobalSettings::scheduleSave);
    connect(image_search, &ImageSearchSettings::modelPathChanged, this, &GlobalSettings::scheduleSave);
    connect(image_search, &ImageSearchSettings::featureNameChanged, this, &GlobalSettings::scheduleSave);
    connect(image_search, &ImageSearchSettings::rebuildIndexChanged, this, &GlobalSettings::scheduleSave);
    connect(image_search, &ImageSearchSettings::topKChanged, this, &GlobalSettings::scheduleSave);
    connect(image_search, &ImageSearchSettings::normChanged, this, &GlobalSettings::scheduleSave);
    connect(image_search, &ImageSearchSettings::preprocessBackendChanged, this, &GlobalSettings::scheduleSave);
    connect(image_search, &ImageSearchSettings::faissBackendChanged, this, &GlobalSettings::scheduleSave);
    connect(image_search, &ImageSearchSettings::indexStorageChanged, this, &GlobalSettings::scheduleSave);
    connect(image_search, &ImageSearchSettings::diskBuildBatchSizeChanged, this, &GlobalSettings::scheduleSave);
    connect(image_search, &ImageSearchSettings::modelBatchSizeChanged, this, &GlobalSettings::scheduleSave);
    connect(image_search, &ImageSearchSettings::modelBackendChanged, this, &GlobalSettings::scheduleSave);
    connect(image_search, &ImageSearchSettings::modelDeviceChanged, this, &GlobalSettings::scheduleSave);
    connect(image_search, &ImageSearchSettings::indexDirectoryChanged, this, &GlobalSettings::scheduleSave);

    auto *roi_search = advanced_settings_->roiSearch();
    connect(roi_search, &RoiSearchSettings::enabledChanged, this, &GlobalSettings::scheduleSave);
    connect(roi_search, &RoiSearchSettings::modelChanged, this, &GlobalSettings::scheduleSave);
    connect(roi_search, &RoiSearchSettings::modelPathChanged, this, &GlobalSettings::scheduleSave);
    connect(roi_search, &RoiSearchSettings::featureNameChanged, this, &GlobalSettings::scheduleSave);
    connect(roi_search, &RoiSearchSettings::rebuildIndexChanged, this, &GlobalSettings::scheduleSave);
    connect(roi_search, &RoiSearchSettings::topKChanged, this, &GlobalSettings::scheduleSave);
    connect(roi_search, &RoiSearchSettings::normChanged, this, &GlobalSettings::scheduleSave);
    connect(roi_search, &RoiSearchSettings::preprocessBackendChanged, this, &GlobalSettings::scheduleSave);
    connect(roi_search, &RoiSearchSettings::faissBackendChanged, this, &GlobalSettings::scheduleSave);
    connect(roi_search, &RoiSearchSettings::indexStorageChanged, this, &GlobalSettings::scheduleSave);
    connect(roi_search, &RoiSearchSettings::diskBuildBatchSizeChanged, this, &GlobalSettings::scheduleSave);
    connect(roi_search, &RoiSearchSettings::modelBatchSizeChanged, this, &GlobalSettings::scheduleSave);
    connect(roi_search, &RoiSearchSettings::modelBackendChanged, this, &GlobalSettings::scheduleSave);
    connect(roi_search, &RoiSearchSettings::modelDeviceChanged, this, &GlobalSettings::scheduleSave);
    connect(roi_search, &RoiSearchSettings::indexDirectoryChanged, this, &GlobalSettings::scheduleSave);
    connect(roi_search, &RoiSearchSettings::pooledHeightChanged, this, &GlobalSettings::scheduleSave);
    connect(roi_search, &RoiSearchSettings::pooledWidthChanged, this, &GlobalSettings::scheduleSave);
    connect(roi_search, &RoiSearchSettings::samplingRatioChanged, this, &GlobalSettings::scheduleSave);
    connect(roi_search, &RoiSearchSettings::alignedChanged, this, &GlobalSettings::scheduleSave);
    connect(roi_search, &RoiSearchSettings::usePcaChanged, this, &GlobalSettings::scheduleSave);
    connect(roi_search, &RoiSearchSettings::pcaDimChanged, this, &GlobalSettings::scheduleSave);

    auto *smart_annotation = advanced_settings_->smartAnnotation();
    connect(smart_annotation, &SmartAnnotationSettings::enabledChanged, this, &GlobalSettings::scheduleSave);
    connect(smart_annotation, &SmartAnnotationSettings::modelChanged, this, &GlobalSettings::scheduleSave);
    connect(smart_annotation, &SmartAnnotationSettings::modelPathChanged, this, &GlobalSettings::scheduleSave);
    connect(smart_annotation, &SmartAnnotationSettings::modelBackendChanged, this, &GlobalSettings::scheduleSave);
    connect(smart_annotation, &SmartAnnotationSettings::modelDeviceChanged, this, &GlobalSettings::scheduleSave);
    connect(smart_annotation, &SmartAnnotationSettings::maskThresholdChanged, this, &GlobalSettings::scheduleSave);
    connect(smart_annotation, &SmartAnnotationSettings::polygonSimplifyEpsilonChanged, this,
            &GlobalSettings::scheduleSave);
    connect(smart_annotation, &SmartAnnotationSettings::maskAlphaChanged, this, &GlobalSettings::scheduleSave);
    connect(smart_annotation, &SmartAnnotationSettings::refreshIntervalChanged, this, &GlobalSettings::scheduleSave);

    // 连接 UISettings 的所有信号
    connect(ui_settings_, &UISettings::imageBrightnessChanged, this, &GlobalSettings::scheduleSave);
    connect(ui_settings_, &UISettings::imageContrastChanged, this, &GlobalSettings::scheduleSave);
    connect(ui_settings_, &UISettings::themeChanged, this, &GlobalSettings::scheduleSave);
    connect(ui_settings_, &UISettings::languageChanged, this, &GlobalSettings::scheduleSave);

    spdlog::info("Auto-save connections established");
}

void GlobalSettings::syncCatalogFromTyped()
{
    if (settings_catalog_ == nullptr)
        return;

    const bool previous_syncing = syncing_catalog_;
    syncing_catalog_           = true;

    setCatalogField(QStringLiteral("ProjectSettings"), QStringLiteral("max_recent_projects"),
                    project_settings_->maxRecentProjects());
    setCatalogField(QStringLiteral("ProjectSettings"), QStringLiteral("auto_save_interval"),
                    project_settings_->autoSaveInterval());
    setCatalogField(QStringLiteral("ProjectSettings"), QStringLiteral("auto_save_enabled"),
                    project_settings_->autoSaveEnabled());

    setCatalogField(QStringLiteral("DataSettings"), QStringLiteral("margin"), data_settings_->thumbnailMargin());
    setCatalogField(QStringLiteral("DataSettings"), QStringLiteral("cache_size"), data_settings_->thumbnailCacheSize());
    setCatalogField(QStringLiteral("DataSettings"), QStringLiteral("image_load_threads"),
                    data_settings_->imageLoadThreads());
    setCatalogField(QStringLiteral("DataSettings"), QStringLiteral("cell_scale"), data_settings_->imageCellScale());
    setCatalogField(QStringLiteral("DataSettings"), QStringLiteral("cell_scale_from"),
                    data_settings_->imageCellScaleFrom());
    setCatalogField(QStringLiteral("DataSettings"), QStringLiteral("cell_scale_to"), data_settings_->imageCellScaleTo());
    setCatalogField(QStringLiteral("DataSettings"), QStringLiteral("cell_scale_step"),
                    data_settings_->imageCellScaleStepSize());
    setCatalogField(QStringLiteral("DataSettings"), QStringLiteral("label_scale"),
                    data_settings_->labelThumbnailScale());
    setCatalogField(QStringLiteral("DataSettings"), QStringLiteral("label_aspect_ratio"),
                    data_settings_->labelThumbnailAspectRatio());
    setCatalogField(QStringLiteral("DataSettings"), QStringLiteral("label_border_padding"),
                    data_settings_->labelThumbnailBorderPadding());
    setCatalogField(QStringLiteral("DataSettings"), QStringLiteral("border_width"), data_settings_->labelBorderWidth());
    setCatalogField(QStringLiteral("DataSettings"), QStringLiteral("fill_opacity"), data_settings_->labelFillOpacity());

    auto *image_search = advanced_settings_->imageSearch();
    setCatalogField(QStringLiteral("ImageSearchSettings"), QStringLiteral("enabled"), image_search->enabled());
    setCatalogField(QStringLiteral("ImageSearchSettings"), QStringLiteral("model"), image_search->model());
    setCatalogField(QStringLiteral("ImageSearchSettings"), QStringLiteral("model_path"), image_search->modelPath());
    setCatalogField(QStringLiteral("ImageSearchSettings"), QStringLiteral("feature_name"), image_search->featureName());
    setCatalogField(QStringLiteral("ImageSearchSettings"), QStringLiteral("rebuild_index"),
                    image_search->rebuildIndex());
    setCatalogField(QStringLiteral("ImageSearchSettings"), QStringLiteral("top_k"), image_search->topK());
    setCatalogField(QStringLiteral("ImageSearchSettings"), QStringLiteral("norm"), image_search->norm());
    setCatalogField(QStringLiteral("ImageSearchSettings"), QStringLiteral("preprocess_backend"),
                    image_search->preprocessBackend());
    setCatalogField(QStringLiteral("ImageSearchSettings"), QStringLiteral("faiss_backend"),
                    image_search->faissBackend());
    setCatalogField(QStringLiteral("ImageSearchSettings"), QStringLiteral("index_storage"),
                    image_search->indexStorage());
    setCatalogField(QStringLiteral("ImageSearchSettings"), QStringLiteral("disk_build_batch_size"),
                    image_search->diskBuildBatchSize());
    setCatalogField(QStringLiteral("ImageSearchSettings"), QStringLiteral("model_batch_size"),
                    image_search->modelBatchSize());
    setCatalogField(QStringLiteral("ImageSearchSettings"), QStringLiteral("model_backend"),
                    image_search->modelBackend());
    setCatalogField(QStringLiteral("ImageSearchSettings"), QStringLiteral("model_device"),
                    image_search->modelDevice());
    setCatalogField(QStringLiteral("ImageSearchSettings"), QStringLiteral("index_directory"),
                    image_search->indexDirectory());

    auto *roi_search = advanced_settings_->roiSearch();
    setCatalogField(QStringLiteral("RoiSearchSettings"), QStringLiteral("enabled"), roi_search->enabled());
    setCatalogField(QStringLiteral("RoiSearchSettings"), QStringLiteral("model"), roi_search->model());
    setCatalogField(QStringLiteral("RoiSearchSettings"), QStringLiteral("model_path"), roi_search->modelPath());
    setCatalogField(QStringLiteral("RoiSearchSettings"), QStringLiteral("feature_name"), roi_search->featureName());
    setCatalogField(QStringLiteral("RoiSearchSettings"), QStringLiteral("rebuild_index"), roi_search->rebuildIndex());
    setCatalogField(QStringLiteral("RoiSearchSettings"), QStringLiteral("top_k"), roi_search->topK());
    setCatalogField(QStringLiteral("RoiSearchSettings"), QStringLiteral("norm"), roi_search->norm());
    setCatalogField(QStringLiteral("RoiSearchSettings"), QStringLiteral("preprocess_backend"),
                    roi_search->preprocessBackend());
    setCatalogField(QStringLiteral("RoiSearchSettings"), QStringLiteral("faiss_backend"), roi_search->faissBackend());
    setCatalogField(QStringLiteral("RoiSearchSettings"), QStringLiteral("index_storage"), roi_search->indexStorage());
    setCatalogField(QStringLiteral("RoiSearchSettings"), QStringLiteral("disk_build_batch_size"),
                    roi_search->diskBuildBatchSize());
    setCatalogField(QStringLiteral("RoiSearchSettings"), QStringLiteral("model_batch_size"),
                    roi_search->modelBatchSize());
    setCatalogField(QStringLiteral("RoiSearchSettings"), QStringLiteral("model_backend"), roi_search->modelBackend());
    setCatalogField(QStringLiteral("RoiSearchSettings"), QStringLiteral("model_device"), roi_search->modelDevice());
    setCatalogField(QStringLiteral("RoiSearchSettings"), QStringLiteral("index_directory"),
                    roi_search->indexDirectory());
    setCatalogField(QStringLiteral("RoiSearchSettings"), QStringLiteral("pooled_height"), roi_search->pooledHeight());
    setCatalogField(QStringLiteral("RoiSearchSettings"), QStringLiteral("pooled_width"), roi_search->pooledWidth());
    setCatalogField(QStringLiteral("RoiSearchSettings"), QStringLiteral("sampling_ratio"),
                    roi_search->samplingRatio());
    setCatalogField(QStringLiteral("RoiSearchSettings"), QStringLiteral("aligned"), roi_search->aligned());
    setCatalogField(QStringLiteral("RoiSearchSettings"), QStringLiteral("use_pca"), roi_search->usePca());
    setCatalogField(QStringLiteral("RoiSearchSettings"), QStringLiteral("pca_dim"), roi_search->pcaDim());

    auto *smart_annotation = advanced_settings_->smartAnnotation();
    setCatalogField(QStringLiteral("SmartAnnotationSettings"), QStringLiteral("enabled"),
                    smart_annotation->enabled());
    setCatalogField(QStringLiteral("SmartAnnotationSettings"), QStringLiteral("model"), smart_annotation->model());
    setCatalogField(QStringLiteral("SmartAnnotationSettings"), QStringLiteral("model_path"),
                    smart_annotation->modelPath());
    setCatalogField(QStringLiteral("SmartAnnotationSettings"), QStringLiteral("model_backend"),
                    smart_annotation->modelBackend());
    setCatalogField(QStringLiteral("SmartAnnotationSettings"), QStringLiteral("model_device"),
                    smart_annotation->modelDevice());
    setCatalogField(QStringLiteral("SmartAnnotationSettings"), QStringLiteral("mask_threshold"),
                    smart_annotation->maskThreshold());
    setCatalogField(QStringLiteral("SmartAnnotationSettings"), QStringLiteral("polygon_simplify_epsilon"),
                    smart_annotation->polygonSimplifyEpsilon());
    setCatalogField(QStringLiteral("SmartAnnotationSettings"), QStringLiteral("mask_alpha"),
                    smart_annotation->maskAlpha());
    setCatalogField(QStringLiteral("SmartAnnotationSettings"), QStringLiteral("refresh_interval"),
                    smart_annotation->refreshInterval());

    setCatalogField(QStringLiteral("UISettings"), QStringLiteral("brightness"), ui_settings_->imageBrightness());
    setCatalogField(QStringLiteral("UISettings"), QStringLiteral("contrast"), ui_settings_->imageContrast());
    setCatalogField(QStringLiteral("UISettings"), QStringLiteral("theme"), ui_settings_->theme());
    setCatalogField(QStringLiteral("UISettings"), QStringLiteral("language"), ui_settings_->language());

    syncing_catalog_ = previous_syncing;
}

void GlobalSettings::syncTypedFromCatalog(const QString &group_key, const QString &name, const QVariant &value)
{
    if (syncing_catalog_)
        return;

    if (group_key == QStringLiteral("ProjectSettings"))
    {
        if (name == QStringLiteral("max_recent_projects"))
            project_settings_->setMaxRecentProjects(value.toInt());
        else if (name == QStringLiteral("auto_save_interval"))
            project_settings_->setAutoSaveInterval(value.toInt());
        else if (name == QStringLiteral("auto_save_enabled"))
            project_settings_->setAutoSaveEnabled(variantBool(value));
    }
    else if (group_key == QStringLiteral("DataSettings"))
    {
        if (name == QStringLiteral("margin"))
            data_settings_->setThumbnailMargin(value.toInt());
        else if (name == QStringLiteral("cache_size"))
            data_settings_->setThumbnailCacheSize(value.toInt());
        else if (name == QStringLiteral("image_load_threads"))
            data_settings_->setImageLoadThreads(value.toInt());
        else if (name == QStringLiteral("cell_scale"))
            data_settings_->setImageCellScale(value.toDouble());
        else if (name == QStringLiteral("cell_scale_from"))
            data_settings_->setImageCellScaleFrom(value.toDouble());
        else if (name == QStringLiteral("cell_scale_to"))
            data_settings_->setImageCellScaleTo(value.toDouble());
        else if (name == QStringLiteral("cell_scale_step"))
            data_settings_->setImageCellScaleStepSize(value.toDouble());
        else if (name == QStringLiteral("label_scale"))
            data_settings_->setLabelThumbnailScale(value.toDouble());
        else if (name == QStringLiteral("label_aspect_ratio"))
            data_settings_->setLabelThumbnailAspectRatio(value.toDouble());
        else if (name == QStringLiteral("label_border_padding"))
            data_settings_->setLabelThumbnailBorderPadding(value.toDouble());
        else if (name == QStringLiteral("border_width"))
            data_settings_->setLabelBorderWidth(value.toInt());
        else if (name == QStringLiteral("fill_opacity"))
            data_settings_->setLabelFillOpacity(value.toInt());
    }
    else if (group_key == QStringLiteral("ImageSearchSettings"))
    {
        applyImageSearchValue(advanced_settings_->imageSearch(), name, value);
    }
    else if (group_key == QStringLiteral("RoiSearchSettings"))
    {
        if (applyImageSearchValue(advanced_settings_->roiSearch(), name, value))
            return;

        auto *roi_search = advanced_settings_->roiSearch();
        if (name == QStringLiteral("pooled_height"))
            roi_search->setPooledHeight(value.toInt());
        else if (name == QStringLiteral("pooled_width"))
            roi_search->setPooledWidth(value.toInt());
        else if (name == QStringLiteral("sampling_ratio"))
            roi_search->setSamplingRatio(value.toInt());
        else if (name == QStringLiteral("aligned"))
            roi_search->setAligned(variantBool(value));
        else if (name == QStringLiteral("use_pca"))
            roi_search->setUsePca(variantBool(value));
        else if (name == QStringLiteral("pca_dim"))
            roi_search->setPcaDim(value.toInt());
    }
    else if (group_key == QStringLiteral("SmartAnnotationSettings"))
    {
        auto *smart_annotation = advanced_settings_->smartAnnotation();
        if (name == QStringLiteral("enabled"))
            smart_annotation->setEnabled(variantBool(value));
        else if (name == QStringLiteral("model"))
            smart_annotation->setModel(value.toString());
        else if (name == QStringLiteral("model_path"))
            smart_annotation->setModelPath(value.toString());
        else if (name == QStringLiteral("model_backend"))
            smart_annotation->setModelBackend(value.toString());
        else if (name == QStringLiteral("model_device"))
            smart_annotation->setModelDevice(value.toString());
        else if (name == QStringLiteral("mask_threshold"))
            smart_annotation->setMaskThreshold(value.toDouble());
        else if (name == QStringLiteral("polygon_simplify_epsilon"))
            smart_annotation->setPolygonSimplifyEpsilon(value.toDouble());
        else if (name == QStringLiteral("mask_alpha"))
            smart_annotation->setMaskAlpha(value.toDouble());
        else if (name == QStringLiteral("refresh_interval"))
            smart_annotation->setRefreshInterval(value.toInt());
    }
    else if (group_key == QStringLiteral("UISettings"))
    {
        if (name == QStringLiteral("brightness"))
            ui_settings_->setImageBrightness(value.toDouble());
        else if (name == QStringLiteral("contrast"))
            ui_settings_->setImageContrast(value.toDouble());
        else if (name == QStringLiteral("theme"))
            ui_settings_->setTheme(value.toString());
        else if (name == QStringLiteral("language"))
            ui_settings_->setLanguage(value.toString());
    }
}

void GlobalSettings::setCatalogField(const QString &group_key, const QString &name, const QVariant &value)
{
    SettingsFieldModel *model = settings_catalog_->group(group_key);
    if (model != nullptr)
        model->setValueForName(name, value);
}

} // namespace dltool::settings
