#include "model/ModelRegistry.h"

#include "model/IModel.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <utility>

namespace dltool::model {

QString FrameworkDefinition::scriptFor(ModelTaskType task_type) const
{
    if (name.isEmpty())
        return {};
    if (task_type == ModelTaskType::Train)
        return train_script;
    if (task_type == ModelTaskType::Test)
        return predict_script;
    return {};
}

bool FrameworkDefinition::supportsExternalTask(ModelTaskType task_type) const
{
    return !scriptFor(task_type).isEmpty();
}

namespace {

struct RegisteredModel
{
    int          method{-1};
    QString      framework_name;
    QString      model_architecture;
    ModelFactory factory;
};

struct RegisteredFramework
{
    FrameworkDefinition definition;
};

std::vector<RegisteredModel> &modelRegistry()
{
    static std::vector<RegisteredModel> registry;
    return registry;
}

std::vector<RegisteredFramework> &frameworkRegistry()
{
    static std::vector<RegisteredFramework> registry;
    return registry;
}

QString cleanPath(const QString &path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty())
        return {};
    return QDir::cleanPath(QDir::fromNativeSeparators(trimmed));
}

QString runtimePath(const QString &path)
{
    const QString cleaned = cleanPath(path);
    if (cleaned.isEmpty() || QFileInfo(cleaned).isAbsolute())
        return cleaned;
    return cleanPath(QDir(QCoreApplication::applicationDirPath()).filePath(cleaned));
}

QString resolvePath(const QString &base_dir, const QString &path)
{
    const QString cleaned = cleanPath(path);
    if (cleaned.isEmpty() || QFileInfo(cleaned).isAbsolute())
        return cleaned;
    return cleanPath(QDir(base_dir).filePath(cleaned));
}

FrameworkDefinition resolvedFrameworkDefinition(const FrameworkDefinition &definition)
{
    FrameworkDefinition resolved = definition;
    resolved.root               = runtimePath(definition.root);
    resolved.train_script       = resolvePath(resolved.root, definition.train_script);
    resolved.predict_script     = resolvePath(resolved.root, definition.predict_script);

    resolved.scripts.clear();
    for (auto it = definition.scripts.constBegin(); it != definition.scripts.constEnd(); ++it)
        resolved.scripts.insert(it.key(), resolvePath(resolved.root, it.value()));

    resolved.python_paths.clear();
    for (const QString &path : definition.python_paths)
        resolved.python_paths.append(resolvePath(resolved.root, path));
    return resolved;
}

} // namespace

bool registerFramework(const int method, const FrameworkDefinition &definition)
{
    FrameworkDefinition normalized = definition;
    normalized.method              = method;
    normalized.name                = definition.name.trimmed();
    const QString trimmed_framework_name = normalized.name;
    if (trimmed_framework_name.isEmpty())
        return false;

    auto      &registry = frameworkRegistry();
    const auto found
        = std::find_if(registry.begin(), registry.end(),
                       [method, &trimmed_framework_name](const RegisteredFramework &framework)
                       {
                           return framework.definition.method == method
                               && framework.definition.name == trimmed_framework_name;
                       });
    if (found != registry.end())
        return false;

    registry.push_back(RegisteredFramework{normalized});
    return true;
}

bool registerModel(const int method, const QString &framework_name, const QString &model_architecture,
                   ModelFactory factory)
{
    const QString trimmed_framework_name     = framework_name.trimmed();
    const QString trimmed_model_architecture = model_architecture.trimmed();
    if (trimmed_framework_name.isEmpty() || trimmed_model_architecture.isEmpty() || !factory)
        return false;

    auto      &registry = modelRegistry();
    const auto found
        = std::find_if(registry.begin(), registry.end(),
                       [method, &trimmed_framework_name, &trimmed_model_architecture](const RegisteredModel &model)
                       {
                           return model.method == method && model.framework_name == trimmed_framework_name
                               && model.model_architecture == trimmed_model_architecture;
                       });
    if (found != registry.end())
        return false;

    registry.push_back(
        RegisteredModel{method, trimmed_framework_name, trimmed_model_architecture, std::move(factory)});
    return true;
}

FrameworkDefinition registeredFramework(const int method, const QString &framework_name)
{
    const QString trimmed_framework_name = framework_name.trimmed();
    if (trimmed_framework_name.isEmpty())
        return {};

    const auto &registry = frameworkRegistry();
    const auto  found
        = std::find_if(registry.begin(), registry.end(),
                       [method, &trimmed_framework_name](const RegisteredFramework &framework)
                       {
                           return (method < 0 || framework.definition.method == method)
                               && framework.definition.name == trimmed_framework_name;
                       });
    if (found == registry.end())
        return {};
    return resolvedFrameworkDefinition(found->definition);
}

QStringList registeredFrameworkNames(const int method)
{
    QStringList names;
    const auto &registry = frameworkRegistry();
    names.reserve(static_cast<int>(registry.size()));
    for (const RegisteredFramework &framework : registry)
    {
        if ((method < 0 || framework.definition.method == method) && framework.definition.visible_for_model_creation
            && !names.contains(framework.definition.name))
        {
            names.append(framework.definition.name);
        }
    }
    return names;
}

QStringList registeredModelArchitectures(const int method, const QString &framework_name)
{
    const QString trimmed_framework_name = framework_name.trimmed();
    QStringList   names;
    const auto   &registry = modelRegistry();
    names.reserve(static_cast<int>(registry.size()));
    for (const RegisteredModel &model : registry)
    {
        if ((method < 0 || model.method == method) && model.framework_name == trimmed_framework_name
            && !names.contains(model.model_architecture))
        {
            names.append(model.model_architecture);
        }
    }
    return names;
}

QStringList registeredModelNames(const int method)
{
    QStringList names;
    const auto &registry = modelRegistry();
    names.reserve(static_cast<int>(registry.size()));
    for (const RegisteredModel &model : registry)
    {
        if ((method < 0 || model.method == method) && !names.contains(model.model_architecture))
            names.append(model.model_architecture);
    }
    return names;
}

std::unique_ptr<IModel> createRegisteredModel(const int method, const QString &framework_name,
                                              const QString &model_architecture)
{
    const QString trimmed_framework_name     = framework_name.trimmed();
    const QString trimmed_model_architecture = model_architecture.trimmed();
    if (trimmed_framework_name.isEmpty() || trimmed_model_architecture.isEmpty())
        return nullptr;

    const auto &registry = modelRegistry();
    const auto  found
        = std::find_if(registry.begin(), registry.end(),
                       [method, &trimmed_framework_name, &trimmed_model_architecture](const RegisteredModel &model)
                       {
                           return (method < 0 || model.method == method) && model.framework_name == trimmed_framework_name
                               && model.model_architecture == trimmed_model_architecture;
                       });
    if (found == registry.end() || !found->factory)
        return nullptr;

    return found->factory();
}

std::vector<std::unique_ptr<IModel>> registeredModels(const int method)
{
    std::vector<std::unique_ptr<IModel>> models;
    const auto                          &registry = modelRegistry();
    models.reserve(registry.size());
    for (const RegisteredModel &model : registry)
    {
        if ((method < 0 || model.method == method) && model.factory)
            models.emplace_back(model.factory());
    }
    return models;
}

} // namespace dltool::model
