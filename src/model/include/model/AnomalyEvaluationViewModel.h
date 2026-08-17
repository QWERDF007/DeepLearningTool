#pragma once

#include "dltool/model/Export.h"
#include "model/ModelEvaluationViewModel.h"

#include <QObject>
#include <QtQml>

namespace dltool::model {

/**
 * @brief 异常检测评估 ViewModel。
 *
 * QML 页面属性声明为 ModelEvaluationViewModel 基类，运行时访问
 * classificationThreshold 等异常方法扩展属性。
 */
class MODEL_API AnomalyEvaluationViewModel : public ModelEvaluationViewModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(AnomalyEvaluationViewModel)
    QML_UNCREATABLE("AnomalyEvaluationViewModel is owned by EvaluationViewModelRegistry")
    Q_PROPERTY(double classificationThreshold READ classificationThreshold NOTIFY methodDataChanged FINAL)

public:
    explicit AnomalyEvaluationViewModel(QObject *parent = nullptr);

    double classificationThreshold() const;

signals:
    void methodDataChanged();

protected:
    void applyMethodSpecificData(const EvaluationResult &result) override;

private:
    double classification_threshold_{evaluation::kDefaultConfidenceThreshold};
};

} // namespace dltool::model
