#include "core/CoreDef.h"
#include "model/ModelRegistry.h"

namespace dltool::model { namespace {

using dltool::core::DeepLearningMethod;

FrameworkDefinition ultralyticsSegmentationFramework()
{
    FrameworkDefinition framework;
    framework.name           = QStringLiteral("ultralytics");
    framework.root           = QStringLiteral("python/ultralytics/ultralytics");
    framework.train_script   = QStringLiteral("train.py");
    framework.predict_script = QStringLiteral("predict.py");
    framework.python_paths   = {
        QStringLiteral("."),
        QStringLiteral(".."),
    };
    framework.weight_extensions = {QStringLiteral(".pt")};
    return framework;
}

DLT_REGISTER_FRAMEWORK(DeepLearningMethod::Segmentation, UltralyticsSegmentation, ultralyticsSegmentationFramework());

}} // namespace dltool::model
