#include "core/CoreDef.h"
#include "model/ModelRegistry.h"

namespace dltool::model { namespace {

using dltool::core::DeepLearningMethod;

/**
 * @brief 构建 anomalib 框架定义
 * @return 框架定义
 */
FrameworkDefinition anomalibFramework()
{
    FrameworkDefinition framework;
    framework.name           = QStringLiteral("anomalib");
    framework.root           = QStringLiteral("python/open-edge-platform/anomalib");
    framework.train_script   = QStringLiteral("train.py");
    framework.predict_script = QStringLiteral("predict.py");
    framework.python_paths   = {
        QStringLiteral("src"),
        QStringLiteral("."),
    };
    framework.weight_extensions = {QStringLiteral(".ckpt")};
    return framework;
}

DLT_REGISTER_FRAMEWORK(DeepLearningMethod::AnomalyDetection, Anomalib, anomalibFramework());

}} // namespace dltool::model
