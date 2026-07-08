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

struct MODEL_API FrameworkDefinition
{
    int                     method{-1};
    QString                 name;
    QString                 root;
    QString                 train_script;
    QString                 predict_script;
    QHash<QString, QString> scripts;
    QStringList             python_paths;
    bool                    visible_for_model_creation{true};

    QString scriptFor(ModelTaskType task_type) const;
    bool    supportsExternalTask(ModelTaskType task_type) const;
};

MODEL_API bool registerFramework(int method, const FrameworkDefinition &definition);
MODEL_API bool registerModel(int method, const QString &framework_name, const QString &model_architecture,
                             ModelFactory factory);

MODEL_API FrameworkDefinition registeredFramework(int method, const QString &framework_name);
MODEL_API QStringList         registeredFrameworkNames(int method);
MODEL_API QStringList         registeredModelArchitectures(int method, const QString &framework_name);
MODEL_API QStringList         registeredModelNames(int method);

MODEL_API std::unique_ptr<IModel>              createRegisteredModel(int method, const QString &framework_name,
                                                                     const QString &model_architecture);
MODEL_API std::vector<std::unique_ptr<IModel>> registeredModels(int method);

} // namespace dltool::model

#define DLT_REGISTER_FRAMEWORK(ModelMethod, RegistrationName, FrameworkDefinitionExpr) \
    const bool RegistrationName##FrameworkRegistered                                   \
        = dltool::model::registerFramework(ModelMethod, FrameworkDefinitionExpr)

#define DLT_REGISTER_MODEL(ModelMethod, FrameworkName, ModelClass)                                        \
    const bool ModelClass##Registered = dltool::model::registerModel(                                     \
        ModelMethod, QStringLiteral(#FrameworkName), ModelClass::staticTypeName(),                        \
        []() -> std::unique_ptr<dltool::model::IModel> { return std::make_unique<ModelClass>(); })

#define DLT_REGISTER_YAML_MODEL(ModelMethod, RegistrationName, FrameworkName, ModelArchitecture)          \
    const bool RegistrationName##Registered = dltool::model::registerModel(                               \
        ModelMethod, QStringLiteral(FrameworkName), QStringLiteral(ModelArchitecture),                     \
        []() -> std::unique_ptr<dltool::model::IModel>                                                    \
        { return createYamlModel(ModelMethod, QStringLiteral(FrameworkName), QStringLiteral(ModelArchitecture)); })
