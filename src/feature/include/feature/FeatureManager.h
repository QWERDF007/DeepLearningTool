#pragma once

#include "dltool/feature/Export.h"
#include "feature/FewShotLearningController.h"
#include "feature/ImageSearchController.h"
#include "feature/RoiSearchController.h"
#include "feature/SmartAnnotationController.h"

#include <QObject>
#include <QtQml>

namespace dltool::model {
class TaskManager;
} // namespace dltool::model

namespace dltool::feature {

class FewShotLearningDataProvider;
class ImageSearchDataProvider;

class FEATURE_API FeatureManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(FeatureManager)
    QML_UNCREATABLE("Can not create FeatureManager directly!")

    Q_PROPERTY(dltool::feature::ImageSearchController *imageSearch READ imageSearch CONSTANT FINAL)
    Q_PROPERTY(dltool::feature::RoiSearchController *roiSearch READ roiSearch CONSTANT FINAL)
    Q_PROPERTY(dltool::feature::SmartAnnotationController *smartAnnotation READ smartAnnotation CONSTANT FINAL)
    Q_PROPERTY(dltool::feature::FewShotLearningController *fewShotLearning READ fewShotLearning CONSTANT FINAL)

public:
    explicit FeatureManager(ImageSearchDataProvider *image_search_provider,
                            FewShotLearningDataProvider *few_shot_provider,
                            QObject *parent = nullptr);

    ImageSearchController *imageSearch() const;
    RoiSearchController *roiSearch() const;
    SmartAnnotationController *smartAnnotation() const;
    FewShotLearningController *fewShotLearning() const;

    void setTaskManager(dltool::model::TaskManager *task_manager);

private:
    ImageSearchController *image_search_{nullptr};
    RoiSearchController *roi_search_{nullptr};
    SmartAnnotationController *smart_annotation_{nullptr};
    FewShotLearningController *few_shot_learning_{nullptr};
};

} // namespace dltool::feature
