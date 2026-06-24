#include "YamlModel.h"
#include "core/CoreDef.h"
#include "model/ModelManager.h"

namespace dltool::model { namespace {

constexpr int DetectionMethod = dltool::core::DeepLearningMethod::Detection;

const bool YoloV5Registered
    = ModelManager::registerModel(DetectionMethod, QStringLiteral("YOLOv5"),
                                  []() { return createYamlModel(DetectionMethod, QStringLiteral("YOLOv5")); });

const bool YoloV8Registered
    = ModelManager::registerModel(DetectionMethod, QStringLiteral("YOLOv8"),
                                  []() { return createYamlModel(DetectionMethod, QStringLiteral("YOLOv8")); });

}} // namespace dltool::model
