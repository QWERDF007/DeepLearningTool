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
                      [](QObject *parent, QThreadPool *evaluation_pool)
                      { return new AnomalyEvaluationViewModel(parent, evaluation_pool); });
    registerViewModel(evaluation::Method::Detection,
                      [](QObject *parent, QThreadPool *evaluation_pool)
                      { return new DetectionEvaluationViewModel(parent, evaluation_pool); });
    registerViewModel(evaluation::Method::Segmentation,
                      [](QObject *parent, QThreadPool *evaluation_pool)
                      { return new SegmentationEvaluationViewModel(parent, evaluation_pool); });
}

void EvaluationViewModelRegistry::registerViewModel(evaluation::Method                                         method,
                                                    std::function<ModelEvaluationViewModel *(QObject *parent,
                                                                                             QThreadPool *evaluation_pool)>
                                                        factory)
{
    factories_.insert(static_cast<int>(method), std::move(factory));
}

ModelEvaluationViewModel *EvaluationViewModelRegistry::createViewModel(evaluation::Method method, QObject *parent,
                                                                         QThreadPool *evaluation_pool) const
{
    const auto it = factories_.constFind(static_cast<int>(method));
    if (it == factories_.cend())
        return nullptr;
    return it.value()(parent, evaluation_pool);
}

} // namespace dltool::model
