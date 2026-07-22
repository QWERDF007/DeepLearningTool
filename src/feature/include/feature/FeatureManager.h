#pragma once

#include "dltool/feature/Export.h"
#include "feature/FewShotLearningController.h"
#include "feature/ImageClusterController.h"
#include "feature/ImageSearchController.h"
#include "feature/RoiClusterController.h"
#include "feature/RoiSearchController.h"
#include "feature/SmartAnnotationController.h"

#include <QObject>
#include <QtQml>
#include <memory>

namespace dltool::data {
class DataManager;
} // namespace dltool::data

namespace dltool::model {
class ModelManager;
class ModelTaskController;
class TaskManager;
} // namespace dltool::model

namespace dltool::feature {

class FEATURE_API FeatureManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(FeatureManager)
    QML_UNCREATABLE("Can not create FeatureManager directly!")

    Q_PROPERTY(dltool::feature::ImageSearchController *imageSearch READ imageSearch CONSTANT FINAL)
    Q_PROPERTY(dltool::feature::RoiSearchController *roiSearch READ roiSearch CONSTANT FINAL)
    Q_PROPERTY(dltool::feature::ImageClusterController *imageCluster READ imageCluster CONSTANT FINAL)
    Q_PROPERTY(dltool::feature::RoiClusterController *roiCluster READ roiCluster CONSTANT FINAL)
    Q_PROPERTY(dltool::feature::SmartAnnotationController *smartAnnotation READ smartAnnotation CONSTANT FINAL)
    Q_PROPERTY(dltool::feature::FewShotLearningController *fewShotLearning READ fewShotLearning CONSTANT FINAL)

public:
    explicit FeatureManager(dltool::data::DataManager *data_manager,
                            dltool::model::ModelManager *model_manager,
                            dltool::model::ModelTaskController *model_task_controller,
                            dltool::model::TaskManager *task_manager,
                            QObject *parent = nullptr);
    ~FeatureManager() override;

    ImageSearchController *imageSearch() const;
    RoiSearchController *roiSearch() const;
    ImageClusterController *imageCluster() const;
    RoiClusterController   *roiCluster() const;
    SmartAnnotationController *smartAnnotation() const;
    FewShotLearningController *fewShotLearning() const;

private:
    class ImageSearchProvider;
    class RoiSearchProvider;
    class ImageClusterProvider;
    class RoiClusterProvider;

    std::unique_ptr<ImageSearchProvider> image_search_provider_;
    std::unique_ptr<RoiSearchProvider> roi_search_provider_;
    std::unique_ptr<ImageClusterProvider> image_cluster_provider_;
    std::unique_ptr<RoiClusterProvider>   roi_cluster_provider_;

    ImageSearchController *image_search_{nullptr};
    RoiSearchController *roi_search_{nullptr};
    ImageClusterController *image_cluster_{nullptr};
    RoiClusterController   *roi_cluster_{nullptr};
    SmartAnnotationController *smart_annotation_{nullptr};
    FewShotLearningController *few_shot_learning_{nullptr};
};

} // namespace dltool::feature
