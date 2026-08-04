#include "model/YamlModel.h"
#include "core/CoreDef.h"
#include "model/ModelRegistry.h"

namespace dltool::model { namespace {

constexpr int AnomalyDetectionMethod = dltool::core::DeepLearningMethod::AnomalyDetection;

DLT_REGISTER_YAML_MODEL(AnomalyDetectionMethod, Patchcore, "anomalib", "patchcore");
DLT_REGISTER_YAML_MODEL(AnomalyDetectionMethod, Dinomaly2, "anomalib", "dinomaly2");

}} // namespace dltool::model
