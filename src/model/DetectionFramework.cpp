#include "core/CoreDef.h"
#include "model/ModelManager.h"

namespace dltool::model { namespace {

using dltool::core::DeepLearningMethod;

ModelManager::FrameworkDefinition ultralyticsFramework()
{
    ModelManager::FrameworkDefinition framework;
    framework.name = QStringLiteral("ultralytics");
    return framework;
}

DLT_REGISTER_FRAMEWORK(DeepLearningMethod::Detection, Ultralytics, ultralyticsFramework());

}} // namespace dltool::model
