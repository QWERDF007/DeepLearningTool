#include "model/SegmentationEvaluationViewModel.h"

namespace dltool::model {

SegmentationEvaluationViewModel::SegmentationEvaluationViewModel(QObject *parent, QThreadPool *evaluation_pool)
    : InstanceMatchingEvaluationViewModel(parent, evaluation_pool)
{
}

} // namespace dltool::model
