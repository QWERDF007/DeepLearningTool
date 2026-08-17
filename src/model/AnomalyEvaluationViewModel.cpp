#include "model/AnomalyEvaluationViewModel.h"

#include "model/EvaluationResult.h"

namespace dltool::model {

AnomalyEvaluationViewModel::AnomalyEvaluationViewModel(QObject *parent)
    : ModelEvaluationViewModel(parent)
{
}

double AnomalyEvaluationViewModel::classificationThreshold() const
{
    return classification_threshold_;
}

void AnomalyEvaluationViewModel::applyMethodSpecificData(const EvaluationResult &result)
{
    if (qFuzzyCompare(classification_threshold_ + 1.0, result.confidence_threshold + 1.0))
        return;
    classification_threshold_ = result.confidence_threshold;
    emit methodDataChanged();
}

} // namespace dltool::model
