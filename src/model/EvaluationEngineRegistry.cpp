#include "model/EvaluationEngineRegistry.h"

#include "model/AnomalyEvaluationEngine.h"
#include "model/DetectionEvaluationEngine.h"
#include "model/SegmentationEvaluationEngine.h"

#include <utility>

namespace dltool::model {

EvaluationEngineRegistry::EvaluationEngineRegistry()
{
    registerBuiltins();
}

EvaluationEngineRegistry &EvaluationEngineRegistry::instance()
{
    static EvaluationEngineRegistry registry;
    return registry;
}

void EvaluationEngineRegistry::registerBuiltins()
{
    registerEngine(evaluation::Method::AnomalyDetection,
                   []() { return std::make_unique<AnomalyEvaluationEngine>(); });
    registerEngine(evaluation::Method::Detection, []() { return std::make_unique<DetectionEvaluationEngine>(); });
    registerEngine(evaluation::Method::Segmentation, []() { return std::make_unique<SegmentationEvaluationEngine>(); });
}

void EvaluationEngineRegistry::registerEngine(evaluation::Method method,
                                              std::function<std::unique_ptr<IEvaluationEngine>()> factory)
{
    factories_.insert(static_cast<int>(method), std::move(factory));
}

std::unique_ptr<IEvaluationEngine> EvaluationEngineRegistry::createEngine(evaluation::Method method) const
{
    const auto it = factories_.constFind(static_cast<int>(method));
    if (it == factories_.cend())
        return nullptr;
    return it.value()();
}

} // namespace dltool::model
