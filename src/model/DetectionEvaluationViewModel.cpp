#include "model/DetectionEvaluationViewModel.h"

namespace dltool::model {

DetectionEvaluationViewModel::DetectionEvaluationViewModel(QObject *parent, QThreadPool *evaluation_pool)
    : InstanceMatchingEvaluationViewModel(parent, evaluation_pool)
{
}

} // namespace dltool::model
