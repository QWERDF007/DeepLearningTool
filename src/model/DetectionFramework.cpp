#include "core/CoreDef.h"
#include "model/ModelRegistry.h"

namespace dltool::model { namespace {

using dltool::core::DeepLearningMethod;

FrameworkDefinition ultralyticsFramework()
{
    FrameworkDefinition framework;
    framework.name = QStringLiteral("ultralytics");
    return framework;
}

DLT_REGISTER_FRAMEWORK(DeepLearningMethod::Detection, Ultralytics, ultralyticsFramework());

}} // namespace dltool::model
