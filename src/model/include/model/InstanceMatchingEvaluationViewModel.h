#pragma once

#include "dltool/model/Export.h"
#include "model/ModelEvaluationViewModel.h"

#include <QObject>
#include <QVariantList>
#include <QtQml>

namespace dltool::model {

/**
 * @brief 检测/分割 ViewModel 共享中间基类。
 *
 * 公共评估加载、过滤、选择与聚合仍在 ModelEvaluationViewModel 基类中；
 * 这里只承载实例匹配方法共享的方法特有数据（PR 曲线类别选项）。
 * Detection/Segmentation 子类继承并注册 QML 类型，QML 页面属性声明为
 * ModelEvaluationViewModel 基类，运行时访问这些扩展属性。
 */
class MODEL_API InstanceMatchingEvaluationViewModel : public ModelEvaluationViewModel
{
    Q_OBJECT
    Q_PROPERTY(QVariantList precisionRecallClasses READ precisionRecallClasses NOTIFY methodDataChanged FINAL)

public:
    explicit InstanceMatchingEvaluationViewModel(QObject *parent = nullptr);

    QVariantList precisionRecallClasses() const;

signals:
    void methodDataChanged();

protected:
    void applyMethodSpecificData(const EvaluationResult &result) override;

private:
    QVariantList precision_recall_classes_;
};

} // namespace dltool::model
