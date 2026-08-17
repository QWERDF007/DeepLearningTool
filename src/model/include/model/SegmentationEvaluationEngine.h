#pragma once

#include "dltool/model/Export.h"
#include "model/InstanceMatchingEvaluationEngine.h"

namespace dltool::model {

/**
 * @brief 语义分割评估引擎。
 *
 * 复用 InstanceMatchingEvaluationEngine 的共享实现，仅固定方法为
 * Segmentation；未来分割专属行为在此扩展。
 */
class MODEL_API SegmentationEvaluationEngine : public InstanceMatchingEvaluationEngine
{
public:
    SegmentationEvaluationEngine();
};

} // namespace dltool::model
