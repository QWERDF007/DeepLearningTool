#include "model/EvaluationResult.h"

#include "model/EvaluationCharts.h"

namespace dltool::model {

QVariantMap evaluationResultToProtocolMap(const EvaluationResult &result, QString *err_msg)
{
    /**
     * @brief Phase 3 -> Phase 5 的兼容桥。
     *
     * 后台引擎已经产出强类型 EvaluationResult；该函数把它重新组织成现有
     * assembleEvaluationResult 所需的上下文并调用同一实现，保证协议快照
     * 与重构前逐字段一致。ViewModel 改为直接消费强类型结果后删除本函数。
     */

    const EvaluationResultContext context{result.images,
                                          result.class_catalog,
                                          result.per_class,
                                          result.overall,
                                          result.image_counts,
                                          result.matrix,
                                          result.event_maps,
                                          result.prediction_count,
                                          result.method,
                                          result.confidence_threshold,
                                          result.iou_threshold,
                                          result.matching_strategy,
                                          result.evaluation_config,
                                          nullptr,
                                          err_msg};
    return assembleEvaluationResult(context);
}

} // namespace dltool::model
