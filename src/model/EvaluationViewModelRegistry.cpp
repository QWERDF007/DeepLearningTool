#include "model/EvaluationViewModelRegistry.h"

#include "model/AnomalyEvaluationViewModel.h"
#include "model/DetectionEvaluationViewModel.h"
#include "model/SegmentationEvaluationViewModel.h"

#include <utility>

namespace dltool::model {

EvaluationViewModelRegistry::EvaluationViewModelRegistry()
{
    registerBuiltins();
}

EvaluationViewModelRegistry &EvaluationViewModelRegistry::instance()
{
    static EvaluationViewModelRegistry registry;
    return registry;
}

void EvaluationViewModelRegistry::registerBuiltins()
{
    registerViewModel(evaluation::Method::AnomalyDetection,
                      [](QObject *parent) { return new AnomalyEvaluationViewModel(parent); });
    registerViewModel(evaluation::Method::Detection,
                      [](QObject *parent) { return new DetectionEvaluationViewModel(parent); });
    registerViewModel(evaluation::Method::Segmentation,
                      [](QObject *parent) { return new SegmentationEvaluationViewModel(parent); });
}

void EvaluationViewModelRegistry::registerViewModel(evaluation::Method method,
                                                    std::function<ModelEvaluationViewModel *(QObject *parent)> factory)
{
    factories_.insert(static_cast<int>(method), std::move(factory));
}

ModelEvaluationViewModel *EvaluationViewModelRegistry::createViewModel(evaluation::Method method,
                                                                       QObject *parent) const
{
    const auto it = factories_.constFind(static_cast<int>(method));
    if (it == factories_.cend())
        return nullptr;
    return it.value()(parent);
}

} // namespace dltool::model
