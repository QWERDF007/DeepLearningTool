#pragma once

#include "dltool/model/Export.h"
#include "model/InstanceMatchingEvaluationViewModel.h"

#include <QtQml>

namespace dltool::model {

/**
 * @brief 语义分割评估 ViewModel。
 *
 * 当前与检测共享实例匹配行为，未来分割专属数据在此扩展。
 */
class MODEL_API SegmentationEvaluationViewModel : public InstanceMatchingEvaluationViewModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SegmentationEvaluationViewModel)
    QML_UNCREATABLE("SegmentationEvaluationViewModel is owned by EvaluationViewModelRegistry")

public:
    explicit SegmentationEvaluationViewModel(QObject *parent = nullptr);
};

} // namespace dltool::model
