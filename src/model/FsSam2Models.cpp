#include "YamlModel.h"
#include "core/CoreDef.h"
#include "model/ModelRegistry.h"

namespace dltool::model { namespace {

constexpr int DetectionMethod        = dltool::core::DeepLearningMethod::Detection;
constexpr int SegmentationMethod     = dltool::core::DeepLearningMethod::Segmentation;
constexpr int AnomalyDetectionMethod = dltool::core::DeepLearningMethod::AnomalyDetection;

DLT_REGISTER_YAML_MODEL(DetectionMethod, FsSam2DetectionModel, "FS-SAM2", "FS-SAM2");
DLT_REGISTER_YAML_MODEL(SegmentationMethod, FsSam2SegmentationModel, "FS-SAM2", "FS-SAM2");
DLT_REGISTER_YAML_MODEL(AnomalyDetectionMethod, FsSam2AnomalyModel, "FS-SAM2", "FS-SAM2");

}} // namespace dltool::model
