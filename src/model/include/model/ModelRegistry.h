#pragma once

#include "dltool/model/Export.h"
#include "model/ModelTaskTypes.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>
#include <vector>

namespace dltool::model {

class IModel;

using ModelFactory = std::function<std::unique_ptr<IModel>()>;

/**
 * @brief 框架任务能力，描述框架对特定任务类型的支持及对应脚本
 */
struct MODEL_API FrameworkTaskCapability
{
    ModelTaskType task_type{ModelTaskType::Unknown}; ///< 任务类型
    QString       script;                            ///< 执行脚本路径

    /**
     * @brief 检查能力是否有效
     * @return 有效返回 true
     */
    bool isValid() const
    {
        return isKnownModelTask(task_type) && !script.trimmed().isEmpty();
    }
};

/**
 * @brief 框架定义，描述一个深度学习框架的根目录、脚本、Python 路径等元信息
 */
struct MODEL_API FrameworkDefinition
{
    int                                  method{-1};                       ///< 深度学习方法
    QString                              name;                             ///< 框架名称
    QString                              root;                             ///< 框架根目录
    QString                              train_script;                     ///< 训练脚本路径
    QString                              predict_script;                   ///< 预测脚本路径
    std::vector<FrameworkTaskCapability> task_capabilities;                ///< 任务能力列表
    QHash<QString, QString>              scripts;                          ///< 额外脚本映射
    QStringList                          python_paths;                     ///< PYTHONPATH 追加路径
    bool                                 visible_for_model_creation{true}; ///< 是否在模型创建界面可见
    bool                                 write_to_database{true};          ///< 是否写入数据库

    /**
     * @brief 获取指定任务类型的能力
     * @param task_type 任务类型
     * @return 任务能力，未找到返回空
     */
    FrameworkTaskCapability taskCapability(ModelTaskType task_type) const;

    /**
     * @brief 获取指定任务类型的脚本路径
     * @param task_type 任务类型
     * @return 脚本路径
     */
    QString scriptFor(ModelTaskType task_type) const;

    /**
     * @brief 检查是否支持外部任务
     * @param task_type 任务类型
     * @return 支持返回 true
     */
    bool supportsExternalTask(ModelTaskType task_type) const;
};

/**
 * @brief 注册框架定义
 * @param method 深度学习方法
 * @param definition 框架定义
 * @return 注册成功返回 true
 */
MODEL_API bool registerFramework(int method, const FrameworkDefinition &definition);

/**
 * @brief 注册模型工厂
 * @param method 深度学习方法
 * @param framework_name 框架名称
 * @param model_architecture 模型架构名称
 * @param factory 模型工厂函数
 * @return 注册成功返回 true
 */
MODEL_API bool registerModel(int method, const QString &framework_name, const QString &model_architecture,
                             ModelFactory factory);

/**
 * @brief 获取已注册的框架定义
 * @param method 深度学习方法
 * @param framework_name 框架名称
 * @return 框架定义
 */
MODEL_API FrameworkDefinition registeredFramework(int method, const QString &framework_name);

/**
 * @brief 获取已注册的框架名称列表
 * @param method 深度学习方法
 * @return 框架名称列表
 */
MODEL_API QStringList registeredFrameworkNames(int method);

/**
 * @brief 获取已注册的模型架构列表
 * @param method 深度学习方法
 * @param framework_name 框架名称
 * @return 模型架构名称列表
 */
MODEL_API QStringList registeredModelArchitectures(int method, const QString &framework_name);

/**
 * @brief 获取已注册的模型名称列表
 * @param method 深度学习方法
 * @return 模型名称列表
 */
MODEL_API QStringList registeredModelNames(int method);

/**
 * @brief 创建已注册的模型实例
 * @param method 深度学习方法
 * @param framework_name 框架名称
 * @param model_architecture 模型架构名称
 * @return 模型实例
 */
MODEL_API std::unique_ptr<IModel> createRegisteredModel(int method, const QString &framework_name,
                                                        const QString &model_architecture);

/**
 * @brief 获取所有已注册的模型实例
 * @param method 深度学习方法
 * @return 模型实例列表
 */
MODEL_API std::vector<std::unique_ptr<IModel>> registeredModels(int method);

} // namespace dltool::model

/// 注册框架的便利宏，在静态初始化阶段调用 registerFramework
#define DLT_REGISTER_FRAMEWORK(ModelMethod, RegistrationName, FrameworkDefinitionExpr) \
    const bool RegistrationName##FrameworkRegistered                                   \
        = dltool::model::registerFramework(ModelMethod, FrameworkDefinitionExpr)

/// 注册模型的便利宏，在静态初始化阶段调用 registerModel
#define DLT_REGISTER_MODEL(ModelMethod, FrameworkName, ModelClass)                 \
    const bool ModelClass##Registered = dltool::model::registerModel(              \
        ModelMethod, QStringLiteral(#FrameworkName), ModelClass::staticTypeName(), \
        []() -> std::unique_ptr<dltool::model::IModel> { return std::make_unique<ModelClass>(); })

/// 注册 YAML 配置模型的便利宏，使用 createYamlModel 工厂函数
#define DLT_REGISTER_YAML_MODEL(ModelMethod, RegistrationName, FrameworkName, ModelArchitecture) \
    const bool RegistrationName##Registered = dltool::model::registerModel(                      \
        ModelMethod, QStringLiteral(FrameworkName), QStringLiteral(ModelArchitecture),           \
        []() -> std::unique_ptr<dltool::model::IModel>                                           \
        { return createYamlModel(ModelMethod, QStringLiteral(FrameworkName), QStringLiteral(ModelArchitecture)); })
