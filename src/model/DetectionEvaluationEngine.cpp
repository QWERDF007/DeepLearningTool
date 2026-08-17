#include "model/DetectionEvaluationEngine.h"

namespace dltool::model {

DetectionEvaluationEngine::DetectionEvaluationEngine()
    : InstanceMatchingEvaluationEngine(evaluation::Method::Detection)
{
}

} // namespace dltool::model
