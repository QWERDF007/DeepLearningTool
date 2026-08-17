#pragma once

#include "dltool/model/Export.h"
#include "model/IEvaluationEngine.h"
#include "model/ModelEvaluationProtocol.h"

#include <QHash>
#include <functional>
#include <memory>

namespace dltool::model {

/**
 * @brief 评估引擎注册表（Meyers 单例）。
 *
 * 按评估方法注册/创建引擎工厂。首次 instance() 访问时自动注册内置引擎
 * （AnomalyDetection/Detection/Segmentation）；外部可覆盖或追加方法工厂。
 */
class MODEL_API EvaluationEngineRegistry
{
public:
    EvaluationEngineRegistry(const EvaluationEngineRegistry &) = delete;
    EvaluationEngineRegistry &operator=(const EvaluationEngineRegistry &) = delete;

    /**
     * @brief 单例访问。
     * @return 注册表实例。
     */
    static EvaluationEngineRegistry &instance();

    /**
     * @brief 注册评估方法对应的引擎工厂。
     *
     * 已注册的同方法条目允许被替换。
     * @param method 评估方法。
     * @param factory 引擎工厂函数。
     */
    void registerEngine(evaluation::Method method, std::function<std::unique_ptr<IEvaluationEngine>()> factory);

    /**
     * @brief 创建评估引擎。
     * @param method 评估方法。
     * @return 新引擎；未注册的方法返回 nullptr。
     */
    std::unique_ptr<IEvaluationEngine> createEngine(evaluation::Method method) const;

private:
    EvaluationEngineRegistry();

    void registerBuiltins();

    QHash<int, std::function<std::unique_ptr<IEvaluationEngine>()>> factories_;
};

} // namespace dltool::model
