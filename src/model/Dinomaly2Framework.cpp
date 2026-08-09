#include "core/CoreDef.h"
#include "model/ModelRegistry.h"

namespace dltool::model { namespace {

using dltool::core::DeepLearningMethod;

/**
 * @brief 构建 Dinomaly2（Mask 约束训练）框架定义
 */
FrameworkDefinition dinomaly2Framework()
{
    FrameworkDefinition framework;
    framework.name           = QStringLiteral("dinomaly2");
    framework.root           = QStringLiteral("python/guojiajeremy/Dinomaly2");
    framework.train_script   = QStringLiteral("train.py");
    framework.predict_script = QStringLiteral("predict.py");
    framework.python_paths   = {
        QStringLiteral("."),
        QStringLiteral("../../task"),
    };
    return framework;
}

DLT_REGISTER_FRAMEWORK(DeepLearningMethod::AnomalyDetection, Dinomaly2, dinomaly2Framework());

}} // namespace dltool::model
