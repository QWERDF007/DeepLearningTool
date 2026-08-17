#pragma once

#include "dltool/model/Export.h"
#include "model/InstanceMatchingEvaluationViewModel.h"

#include <QtQml>

namespace dltool::model {

/**
 * @brief 目标检测评估 ViewModel。
 *
 * QML 页面属性仍声明为 ModelEvaluationViewModel；实际实例由
 * EvaluationViewModelRegistry 创建，QML 运行时访问继承的
 * precisionRecallClasses 等扩展属性。
 */
class MODEL_API DetectionEvaluationViewModel : public InstanceMatchingEvaluationViewModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(DetectionEvaluationViewModel)
    QML_UNCREATABLE("DetectionEvaluationViewModel is owned by EvaluationViewModelRegistry")

public:
    explicit DetectionEvaluationViewModel(QObject *parent = nullptr);
};

} // namespace dltool::model
