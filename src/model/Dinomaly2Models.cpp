#include "core/CoreDef.h"
#include "model/ModelRegistry.h"
#include "model/YamlModel.h"

namespace dltool::model { namespace {

constexpr int AnomalyDetectionMethod = dltool::core::DeepLearningMethod::AnomalyDetection;

DLT_REGISTER_YAML_MODEL(AnomalyDetectionMethod, Dinomaly2Mask, "dinomaly2", "dinomaly2");

}} // namespace dltool::model
