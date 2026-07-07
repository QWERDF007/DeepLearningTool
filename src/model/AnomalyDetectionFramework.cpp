#include "core/CoreDef.h"
#include "model/ModelManager.h"

namespace dltool::model { namespace {

using dltool::core::DeepLearningMethod;

ModelManager::FrameworkDefinition anomalibFramework()
{
    ModelManager::FrameworkDefinition framework;
    framework.name = QStringLiteral("anomalib");
    framework.root = QStringLiteral("python/open-edge-platform/anomalib");
    framework.train_script = QStringLiteral("train.py");
    framework.predict_script = QStringLiteral("predict.py");
    framework.python_paths = {
        QStringLiteral("src"),
        QStringLiteral("."),
        QStringLiteral("../../task"),
    };
    return framework;
}

DLT_REGISTER_FRAMEWORK(DeepLearningMethod::AnomalyDetection, Anomalib, anomalibFramework());

}} // namespace dltool::model
