#include "model/YamlModel.h"
#include "core/CoreDef.h"
#include "model/ModelRegistry.h"

namespace dltool::model { namespace {

constexpr int SegmentationMethod = dltool::core::DeepLearningMethod::Segmentation;

DLT_REGISTER_YAML_MODEL(SegmentationMethod, YoloV8Seg, "ultralytics", "YOLOv8-seg");

}} // namespace dltool::model
