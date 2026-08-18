#pragma once

#include "dltool/model/Export.h"
#include "model/ModelEvaluationProtocol.h"
#include "model/ModelEvaluationViewModel.h"

#include <QHash>
#include <QObject>
#include <functional>

namespace dltool::model {

/**
 * @brief 评估 ViewModel 注册表（Meyers 单例）。
 *
 * 按评估方法注册/创建 ViewModel 子类工厂。首次 instance() 访问时自动
 * 注册内置实现（AnomalyDetection/Detection/Segmentation）；返回的实例
 * 父对象归调用方，注册表不持有 ViewModel。
 */
class MODEL_API EvaluationViewModelRegistry
{
public:
    EvaluationViewModelRegistry(const EvaluationViewModelRegistry &)            = delete;
    EvaluationViewModelRegistry &operator=(const EvaluationViewModelRegistry &) = delete;

    /**
     * @brief 单例访问。
     * @return 注册表实例。
     */
    static EvaluationViewModelRegistry &instance();

    /**
     * @brief 注册评估方法对应的 ViewModel 工厂。
     *
     * 已注册的同方法条目允许被替换。
     * @param method 评估方法。
     * @param factory 工厂函数（接收 QObject 父对象）。
     */
    void registerViewModel(evaluation::Method                                         method,
                           std::function<ModelEvaluationViewModel *(QObject *parent)> factory);

    /**
     * @brief 创建 ViewModel。
     * @param method 评估方法。
     * @param parent QObject 父对象。
     * @return 新 ViewModel（父对象为 parent）；未注册的方法返回 nullptr。
     */
    ModelEvaluationViewModel *createViewModel(evaluation::Method method, QObject *parent = nullptr) const;

private:
    EvaluationViewModelRegistry();

    void registerBuiltins();

    QHash<int, std::function<ModelEvaluationViewModel *(QObject *parent)>> factories_;
};

} // namespace dltool::model
