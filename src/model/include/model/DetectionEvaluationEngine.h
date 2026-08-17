#pragma once

#include "dltool/model/Export.h"
#include "model/InstanceMatchingEvaluationEngine.h"

namespace dltool::model {

/**
 * @brief 目标检测评估引擎。
 *
 * 复用 InstanceMatchingEvaluationEngine 的共享实现，仅固定方法为
 * Detection；未来检测专属行为在此扩展。
 */
class MODEL_API DetectionEvaluationEngine : public InstanceMatchingEvaluationEngine
{
public:
    DetectionEvaluationEngine();
};

} // namespace dltool::model
