#include "core/CoreDef.h"
#include "model/ModelRegistry.h"

namespace dltool::model { namespace {

using dltool::core::DeepLearningMethod;

/**
 * @brief 构建 ultralytics 框架定义
 * @return 框架定义
 */
FrameworkDefinition ultralyticsFramework()
{
    FrameworkDefinition framework;
    framework.name = QStringLiteral("ultralytics");
    return framework;
}

DLT_REGISTER_FRAMEWORK(DeepLearningMethod::Detection, Ultralytics, ultralyticsFramework());

}} // namespace dltool::model
