#include "YamlModel.h"
#include "core/CoreDef.h"
#include "model/ModelManager.h"

namespace dltool::model { namespace {

constexpr int DetectionMethod = dltool::core::DeepLearningMethod::Detection;

DLT_REGISTER_YAML_MODEL(DetectionMethod, YoloV5, "ultralytics", "YOLOv5");
DLT_REGISTER_YAML_MODEL(DetectionMethod, YoloV8, "ultralytics", "YOLOv8");

}} // namespace dltool::model
