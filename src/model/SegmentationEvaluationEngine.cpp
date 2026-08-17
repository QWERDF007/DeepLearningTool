#include "model/SegmentationEvaluationEngine.h"

namespace dltool::model {

SegmentationEvaluationEngine::SegmentationEvaluationEngine()
    : InstanceMatchingEvaluationEngine(evaluation::Method::Segmentation)
{
}

} // namespace dltool::model
