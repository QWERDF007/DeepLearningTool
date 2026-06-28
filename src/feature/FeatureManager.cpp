#include "feature/FeatureManager.h"

#include "feature/FewShotLearningDataProvider.h"
#include "feature/ImageSearchDataProvider.h"
#include "settings/GlobalSettings.h"

namespace dltool::feature {

FeatureManager::FeatureManager(ImageSearchDataProvider *image_search_provider,
                               FewShotLearningDataProvider *few_shot_provider,
                               QObject *parent)
    : QObject(parent)
{
    image_search_ = new ImageSearchController(image_search_provider, this);
    roi_search_ = new RoiSearchController(image_search_provider, this);
    smart_annotation_ = new SmartAnnotationController(this);
    few_shot_learning_ = new FewShotLearningController(few_shot_provider, this);

    if (auto *settings = dltool::settings::GlobalSettings::getInstance()->settingsGroup(
            dltool::settings::generated::AccessorKey::SmartAnnotation))
    {
        connect(settings, &dltool::settings::SettingsGroup::valueChanged, smart_annotation_,
                &SmartAnnotationController::clearCache);
    }
}

ImageSearchController *FeatureManager::imageSearch() const
{
    return image_search_;
}

RoiSearchController *FeatureManager::roiSearch() const
{
    return roi_search_;
}

SmartAnnotationController *FeatureManager::smartAnnotation() const
{
    return smart_annotation_;
}

FewShotLearningController *FeatureManager::fewShotLearning() const
{
    return few_shot_learning_;
}

void FeatureManager::setTaskManager(dltool::model::TaskManager *task_manager)
{
    if (few_shot_learning_ != nullptr)
        few_shot_learning_->setTaskManager(task_manager);
}

} // namespace dltool::feature
