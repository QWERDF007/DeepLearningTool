#include "core/CoreDef.h"
#include "model/ModelRegistry.h"

namespace dltool::model { namespace {

using dltool::core::DeepLearningMethod;

FrameworkDefinition fsSam2Framework()
{
    FrameworkDefinition framework;
    framework.name = QStringLiteral("FS-SAM2");
    framework.root = QStringLiteral("python/fornib/FS-SAM2");
    framework.train_script = QStringLiteral("train.py");
    framework.predict_script = QStringLiteral("predict.py");
    framework.scripts.insert(QStringLiteral("box_to_mask"), QStringLiteral("box_to_mask.py"));
    framework.python_paths = {
        QStringLiteral("."),
        QStringLiteral("../../task"),
    };
    framework.visible_for_model_creation = false;
    return framework;
}

DLT_REGISTER_FRAMEWORK(DeepLearningMethod::Detection, FsSam2Detection, fsSam2Framework());
DLT_REGISTER_FRAMEWORK(DeepLearningMethod::Segmentation, FsSam2Segmentation, fsSam2Framework());
DLT_REGISTER_FRAMEWORK(DeepLearningMethod::AnomalyDetection, FsSam2Anomaly, fsSam2Framework());

}} // namespace dltool::model
