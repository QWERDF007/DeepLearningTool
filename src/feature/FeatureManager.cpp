#include "feature/FeatureManager.h"

#include "data/DataManager.h"
#include "feature/ImageClusterDataProvider.h"
#include "feature/ImageSearchDataProvider.h"
#include "feature/RoiClusterDataProvider.h"
#include "feature/RoiSearchDataProvider.h"
#include "settings/GlobalSettings.h"

namespace dltool::feature {

class FeatureManager::ImageSearchProvider final : public ImageSearchDataProvider
{
public:
    explicit ImageSearchProvider(dltool::data::DataManager *data_manager)
        : ImageSearchDataProvider(data_manager)
    {
    }

    std::vector<int64_t> selectedImageIds() const override
    {
        auto *manager = dataManager();
        return manager ? manager->selectedImageIds() : std::vector<int64_t>{};
    }

    void clearImageSearchResults() override
    {
        if (auto *manager = dataManager())
            manager->clearImageSearchResults();
    }

    void setImageSearchResults(const std::vector<int64_t> &image_ids, bool enable_filter) override
    {
        if (auto *manager = dataManager())
            manager->setImageSearchResults(image_ids, enable_filter);
    }
};

class FeatureManager::RoiSearchProvider final : public RoiSearchDataProvider
{
public:
    explicit RoiSearchProvider(dltool::data::DataManager *data_manager)
        : RoiSearchDataProvider(data_manager)
    {
    }

    void clearLabelSearchResults() override
    {
        if (auto *manager = dataManager())
            manager->clearLabelSearchResults();
    }

    void setLabelSearchResults(const std::vector<int64_t> &label_ids, bool enable_filter) override
    {
        if (auto *manager = dataManager())
            manager->setLabelSearchResults(label_ids, enable_filter);
    }
};

class FeatureManager::ImageClusterProvider final : public ImageClusterDataProvider
{
public:
    explicit ImageClusterProvider(dltool::data::DataManager *data_manager)
        : ImageClusterDataProvider(data_manager)
    {
    }
};

class FeatureManager::RoiClusterProvider final : public RoiClusterDataProvider
{
public:
    explicit RoiClusterProvider(dltool::data::DataManager *data_manager)
        : RoiClusterDataProvider(data_manager)
    {
    }
};

FeatureManager::FeatureManager(dltool::data::DataManager *data_manager,
                               dltool::model::ModelManager *model_manager,
                               dltool::model::ModelTaskController *model_task_controller,
                               dltool::model::TaskManager *task_manager,
                               QObject *parent)
    : QObject(parent)
    , image_search_provider_(std::make_unique<ImageSearchProvider>(data_manager))
    , roi_search_provider_(std::make_unique<RoiSearchProvider>(data_manager))
    , image_cluster_provider_(std::make_unique<ImageClusterProvider>(data_manager))
    , roi_cluster_provider_(std::make_unique<RoiClusterProvider>(data_manager))
{
    image_search_ = new ImageSearchController(image_search_provider_.get(), this);
    roi_search_ = new RoiSearchController(roi_search_provider_.get(), this);
    image_cluster_ = new ImageClusterController(image_cluster_provider_.get(), data_manager, this);
    roi_cluster_ = new RoiClusterController(roi_cluster_provider_.get(), data_manager, this);
    smart_annotation_ = new SmartAnnotationController(this);
    few_shot_learning_ = new FewShotLearningController(data_manager, model_manager, model_task_controller,
                                                       task_manager, this);

    if (auto *settings = dltool::settings::GlobalSettings::getInstance()->settingsGroup(
            dltool::settings::generated::AccessorKey::SmartAnnotation))
    {
        connect(settings, &dltool::settings::SettingsGroup::valueChanged, smart_annotation_,
                &SmartAnnotationController::clearCache);
    }
}

FeatureManager::~FeatureManager()
{
    delete few_shot_learning_;
    few_shot_learning_ = nullptr;
    delete smart_annotation_;
    smart_annotation_ = nullptr;
    delete image_cluster_;
    image_cluster_ = nullptr;
    delete roi_cluster_;
    roi_cluster_ = nullptr;
    delete roi_search_;
    roi_search_ = nullptr;
    delete image_search_;
    image_search_ = nullptr;
    image_cluster_provider_.reset();
    roi_cluster_provider_.reset();
    roi_search_provider_.reset();
    image_search_provider_.reset();
}

ImageSearchController *FeatureManager::imageSearch() const
{
    return image_search_;
}

RoiSearchController *FeatureManager::roiSearch() const
{
    return roi_search_;
}

ImageClusterController *FeatureManager::imageCluster() const
{
    return image_cluster_;
}

RoiClusterController *FeatureManager::roiCluster() const
{
    return roi_cluster_;
}

SmartAnnotationController *FeatureManager::smartAnnotation() const
{
    return smart_annotation_;
}

FewShotLearningController *FeatureManager::fewShotLearning() const
{
    return few_shot_learning_;
}

} // namespace dltool::feature
