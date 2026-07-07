#include "model/ModelManager.h"

#include "common/Utils.h"
#include "data/DatasetViewModelFactory.h"
#include "database/DataBase.h"
#include "model/ExternalModelTaskRunner.h"
#include "model/IModelConfig.h"
#include "model/IParams.h"
#include "model/ModelStorageService.h"
#include "model/ModelTaskConfigService.h"
#include "model/ModelTaskPreparationService.h"
#include "model/TaskManager.h"

#include <spdlog/spdlog.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QQmlEngine>
#include <algorithm>
#include <utility>

namespace dltool::model {

namespace {

struct RegisteredModel
{
    int                        method{-1};
    QString                    framework_name;
    QString                    model_architecture;
    ModelManager::ModelFactory factory;
};

struct RegisteredFramework
{
    ModelManager::FrameworkDefinition definition;
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

ModelManager::FrameworkDefinition resolvedFrameworkDefinition(
    const ModelManager::FrameworkDefinition &definition)
{
    ModelManager::FrameworkDefinition resolved = definition;
    resolved.root = runtimePath(definition.root);
    resolved.train_script = resolvePath(resolved.root, definition.train_script);
    resolved.predict_script = resolvePath(resolved.root, definition.predict_script);

    resolved.scripts.clear();
    for (auto it = definition.scripts.constBegin(); it != definition.scripts.constEnd(); ++it)
        resolved.scripts.insert(it.key(), resolvePath(resolved.root, it.value()));

    resolved.python_paths.clear();
    for (const QString &path : definition.python_paths)
        resolved.python_paths.append(resolvePath(resolved.root, path));
    return resolved;
}

} // namespace

ModelManager::ModelManager(const int method,
                           dltool::database::ProjectDataBase *database,
                           dltool::data::DataManager *data_manager,
                           QObject *parent)
    : QAbstractListModel(parent)
    , database_(database)
    , data_manager_(data_manager)
    , method_(method)
    , project_dir_(database != nullptr ? cleanPath(QFileInfo(database->path()).absoluteDir().absolutePath()) : QString())
    , external_task_runner_(std::make_unique<ExternalModelTaskRunner>(this))
{
    init();
}

ModelManager::~ModelManager() {}

void ModelManager::init()
{
    beginResetModel();
    models_.clear();
    model_instances_.clear();
    config_load_started_.clear();

    if (database_ == nullptr)
    {
        endResetModel();
        spdlog::error("初始化模型管理器失败: 数据库对象为空");
        return;
    }

    std::vector<int64_t> model_ids;
    std::vector<QString> uuids;
    std::vector<QString> names;
    std::vector<QString> framework_names;
    std::vector<QString> model_architectures;
    std::vector<QString> training_results;
    std::vector<QString> test_results;
    std::vector<qint64>  ctimes;
    std::vector<qint64>  mtimes;
    QString              err_msg;

    const bool ok = database_->getAllModels(model_ids, uuids, names, framework_names, model_architectures,
                                            training_results, test_results, ctimes, mtimes, err_msg);
    if (!ok)
    {
        endResetModel();
        spdlog::error("查询所有模型失败, 错误: {}", err_msg.toUtf8().constData());
        return;
    }

    models_.reserve(model_ids.size());
    for (size_t i = 0; i < model_ids.size(); ++i)
    {
        models_.push_back(ModelRecord{
            model_ids[i],
            uuids[i],
            names[i],
            framework_names[i],
            model_architectures[i],
            training_results[i],
            test_results[i],
            ctimes[i],
            mtimes[i],
        });
    }

    endResetModel();
}

int ModelManager::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(models_.size());
}

QVariant ModelManager::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= rowCount())
        return QVariant();

    switch (role)
    {
    case ModelIdRole:
        return getModelId(index);
    case UuidRole:
        return getUuid(index);
    case NameRole:
        return getName(index);
    case FrameworkNameRole:
        return getFrameworkName(index);
    case ModelArchitectureRole:
        return getModelArchitecture(index);
    case TrainingResultRole:
        return getTrainingResult(index);
    case TestResultRole:
        return getTestResult(index);
    case CtimeRole:
        return getCtime(index);
    case MtimeRole:
        return getMtime(index);
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> ModelManager::roleNames() const
{
    return {
        {       ModelIdRole,            "model_id"},
        {          UuidRole,                "uuid"},
        {          NameRole,                "name"},
        { FrameworkNameRole,      "framework_name"},
        {ModelArchitectureRole, "model_architecture"},
        {TrainingResultRole,     "training_result"},
        {    TestResultRole,         "test_result"},
        {         CtimeRole,               "ctime"},
        {         MtimeRole,               "mtime"},
    };
}

bool ModelManager::addModel(const QString &name, const QString &framework_name, const QString &model_architecture)
{
    const QString trimmed_name               = name.trimmed();
    const QString trimmed_framework_name     = framework_name.trimmed();
    const QString trimmed_model_architecture = model_architecture.trimmed();
    if (trimmed_name.isEmpty() || trimmed_framework_name.isEmpty() || trimmed_model_architecture.isEmpty())
    {
        spdlog::warn("添加模型失败: 模型名称、框架或模型架构为空");
        return false;
    }

    if (!registeredModelArchitectures(method_, trimmed_framework_name).contains(trimmed_model_architecture))
    {
        spdlog::warn("添加模型失败: 模型未注册, 方法: {}, 框架: {}, 模型架构: {}", method_,
                     trimmed_framework_name.toUtf8().constData(), trimmed_model_architecture.toUtf8().constData());
        return false;
    }

    if (database_ == nullptr)
    {
        spdlog::error("添加模型失败: 数据库对象为空");
        return false;
    }

    QString             err_msg;
    int64_t             model_id{-1};
    const qint64        now  = QDateTime::currentSecsSinceEpoch();
    const QString       uuid = dltool::common::uuid();
    ModelStorageService storage(project_dir_);
    if (!storage.ensureModelStorage(uuid, &err_msg))
    {
        spdlog::error("添加模型失败, 创建模型目录失败: {}", err_msg.toUtf8().constData());
        return false;
    }

    const bool ok = database_->addModel(uuid, trimmed_name, trimmed_framework_name, trimmed_model_architecture,
                                        QString(), QString(), now, now, model_id, err_msg);
    if (!ok)
    {
        QString remove_err;
        storage.removeModelStorage(uuid, &remove_err);
        spdlog::error("添加模型失败, 名称: {}, 框架: {}, 模型架构: {}, 错误: {}", trimmed_name.toUtf8().constData(),
                      trimmed_framework_name.toUtf8().constData(), trimmed_model_architecture.toUtf8().constData(),
                      err_msg.toUtf8().constData());
        return false;
    }

    const int row = rowCount();
    beginInsertRows(QModelIndex(), row, row);
    models_.push_back(ModelRecord{
        model_id,
        uuid,
        trimmed_name,
        trimmed_framework_name,
        trimmed_model_architecture,
        QString(),
        QString(),
        now,
        now,
    });
    endInsertRows();

    spdlog::info("模型添加成功, id: {}, 模型名称: {}, 框架: {}, 模型架构: {}", model_id,
                 trimmed_name.toUtf8().constData(), trimmed_framework_name.toUtf8().constData(),
                 trimmed_model_architecture.toUtf8().constData());
    return true;
}

bool ModelManager::renameModel(const qint64 model_id, const QString &name)
{
    const QString trimmed_name = name.trimmed();
    if (trimmed_name.isEmpty())
    {
        spdlog::warn("模型重命名失败: 模型名称为空");
        return false;
    }

    const int row = indexOfModel(model_id);
    if (row < 0)
    {
        spdlog::warn("模型重命名失败: 模型 {} 不存在", model_id);
        return false;
    }

    QString      err_msg;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const bool   ok  = database_ != nullptr && database_->updateModelName(model_id, trimmed_name, now, err_msg);
    if (!ok)
    {
        spdlog::error("重命名模型失败, id: {}, 错误: {}", model_id, err_msg.toUtf8().constData());
        return false;
    }

    models_[row].name  = trimmed_name;
    models_[row].mtime = now;
    emit dataChanged(index(row), index(row), {NameRole, MtimeRole});
    return true;
}

bool ModelManager::deleteModel(const qint64 model_id)
{
    const int row = indexOfModel(model_id);
    if (row < 0)
    {
        spdlog::warn("删除模型失败: 模型 {} 不存在", model_id);
        return false;
    }

    QString    err_msg;
    const QString uuid = models_[static_cast<size_t>(row)].uuid;
    const bool ok = database_ != nullptr && database_->deleteModel(model_id, err_msg);
    if (!ok)
    {
        spdlog::error("删除模型失败, id: {}, 错误: {}", model_id, err_msg.toUtf8().constData());
        return false;
    }

    beginRemoveRows(QModelIndex(), row, row);
    models_.erase(models_.begin() + row);
    endRemoveRows();
    model_instances_.erase(instanceKey(uuid));
    config_load_started_.erase(instanceKey(uuid));
    ModelStorageService storage(project_dir_);
    if (!storage.removeModelStorage(uuid, &err_msg))
    {
        spdlog::error("删除模型目录失败, uuid: {}, 错误: {}", uuid.toUtf8().constData(), err_msg.toUtf8().constData());
    }
    return true;
}

bool ModelManager::copyModel(const qint64 model_id)
{
    const int row = indexOfModel(model_id);
    if (row < 0)
    {
        spdlog::warn("复制模型失败: 模型 {} 不存在", model_id);
        return false;
    }

    const ModelRecord &source = models_[row];
    QString            err_msg;
    int64_t            new_model_id{-1};
    const qint64       now         = QDateTime::currentSecsSinceEpoch();
    const QString      copied_name = uniqueCopyName(source.name);
    const QString      new_uuid    = dltool::common::uuid();
    ModelStorageService storage(project_dir_);
    if (!storage.ensureModelStorage(new_uuid, &err_msg))
    {
        spdlog::error("复制模型失败, 创建模型目录失败: {}", err_msg.toUtf8().constData());
        return false;
    }

    const bool         ok          = database_ != nullptr
                 && database_->addModel(new_uuid, copied_name, source.framework_name, source.model_architecture,
                                        source.training_result, source.test_result, now, now, new_model_id, err_msg);
    if (!ok)
    {
        QString remove_err;
        storage.removeModelStorage(new_uuid, &remove_err);
        spdlog::error("复制模型失败, id: {}, 错误: {}", model_id, err_msg.toUtf8().constData());
        return false;
    }

    const int insert_row = rowCount();
    beginInsertRows(QModelIndex(), insert_row, insert_row);
    models_.push_back(ModelRecord{
        new_model_id,
        new_uuid,
        copied_name,
        source.framework_name,
        source.model_architecture,
        source.training_result,
        source.test_result,
        now,
        now,
    });
    endInsertRows();

    const auto source_found = model_instances_.find(instanceKey(source.uuid));
    if (source_found != model_instances_.end() && source_found->second)
    {
        auto copied_model = createRegisteredModelInstance(source.framework_name, source.model_architecture);
        if (copied_model && copied_model->config() && source_found->second->config())
        {
            ITrainParams       *target_train_params = copied_model->config()->trainParams();
            const ITrainParams *source_train_params = source_found->second->config()->trainParams();
            if (target_train_params != nullptr && source_train_params != nullptr)
            {
                target_train_params->copyValuesFrom(*source_train_params);
            }

            ITestParams       *target_test_params = copied_model->config()->testParams();
            const ITestParams *source_test_params = source_found->second->config()->testParams();
            if (target_test_params != nullptr && source_test_params != nullptr)
            {
                target_test_params->copyValuesFrom(*source_test_params);
            }

            copied_model->setParent(const_cast<ModelManager *>(this));
            copied_model->setUuid(new_uuid);
            QQmlEngine::setObjectOwnership(copied_model.get(), QQmlEngine::CppOwnership);
            model_instances_[instanceKey(new_uuid)] = std::move(copied_model);
            config_load_started_.insert(instanceKey(new_uuid));
        }
    }

    return true;
}

QStringList ModelManager::supportedFrameworks() const
{
    return registeredFrameworkNames(method_);
}

QStringList ModelManager::supportedModelArchitectures(const QString &framework_name) const
{
    return registeredModelArchitectures(method_, framework_name);
}

QStringList ModelManager::availableModelNames() const
{
    return registeredModelNames(method_);
}

QVariantMap ModelManager::modelAt(const int row) const
{
    if (row < 0 || row >= rowCount())
    {
        return {};
    }

    const ModelRecord &model = models_.at(static_cast<size_t>(row));
    return {
        {         QStringLiteral("model_id"), static_cast<qint64>(model.model_id)},
        {             QStringLiteral("uuid"),                          model.uuid},
        {             QStringLiteral("name"),                          model.name},
        {   QStringLiteral("framework_name"),                model.framework_name},
        {QStringLiteral("model_architecture"),          model.model_architecture},
        {  QStringLiteral("training_result"),               model.training_result},
        {      QStringLiteral("test_result"),                   model.test_result},
        {            QStringLiteral("ctime"),                         model.ctime},
        {            QStringLiteral("mtime"),                         model.mtime},
    };
}

IModel *ModelManager::modelForUuid(const QString &uuid) const
{
    const int row = indexOfUuid(uuid);
    if (row < 0)
        return nullptr;

    IModel *model = cachedModelForRecord(models_[static_cast<size_t>(row)]);
    if (model != nullptr)
        requestModelTaskConfigLoad(models_[static_cast<size_t>(row)].uuid);
    return model;
}

int ModelManager::addModelTask(const QString &model_uuid, const QString &model_name, ModelTaskType task_type)
{
    return TaskManager::getInstance()->addModelTask(model_uuid, model_name, task_type);
}

int ModelManager::startModelTask(const QString &model_uuid, const QString &model_name, ModelTaskType task_type)
{
    return TaskManager::getInstance()->startModelTask(model_uuid, model_name, task_type);
}

bool ModelManager::stopModelTask(const QString &model_uuid, ModelTaskType task_type)
{
    return TaskManager::getInstance()->stopModelTask(model_uuid, task_type);
}

bool ModelManager::deleteModelTask(const QString &model_uuid, ModelTaskType task_type)
{
    return TaskManager::getInstance()->deleteModelTask(model_uuid, task_type);
}

void ModelManager::requestModelTaskConfigLoad(const QString &model_uuid) const
{
    const QString trimmed_uuid = model_uuid.trimmed();
    if (trimmed_uuid.isEmpty())
        return;

    const std::string key = instanceKey(trimmed_uuid);
    if (config_load_started_.find(key) != config_load_started_.end())
        return;
    config_load_started_.insert(key);

    const ModelTaskConfigService config_service(project_dir_);
    const dltool::model::LoadedModelTaskConfigs configs = config_service.load(trimmed_uuid);
    const_cast<ModelManager *>(this)->applyLoadedModelTaskConfigs(configs.model_uuid, configs.train_params,
                                                                  configs.test_params,
                                                                  configs.dataset_selections);
}

void ModelManager::applyLoadedModelTaskConfigs(const QString &model_uuid, const QVariantMap &train_params,
                                               const QVariantMap &test_params,
                                               const QVariantMap &dataset_selections)
{
    const auto found = model_instances_.find(instanceKey(model_uuid));
    if (found == model_instances_.end() || !found->second || found->second->config() == nullptr)
        return;

    IModel *model = found->second.get();
    IModelConfig *model_config = found->second->config();
    if (!train_params.isEmpty())
    {
        if (ITrainParams *params = model_config->trainParams(); params != nullptr)
            params->setValuesMap(train_params);
    }
    if (!test_params.isEmpty())
    {
        if (ITestParams *params = model_config->testParams(); params != nullptr)
            params->setValuesMap(test_params);
    }
    applyModelDatasetSelections(model, dataset_selections);
}

bool ModelManager::hasTaskHandler(int task_id) const
{
    auto *task_manager = TaskManager::getInstance();
    if (task_manager == nullptr || task_manager->tasks() == nullptr)
        return false;

    const QVariantMap task = task_manager->tasks()->taskForId(task_id);
    const QString model_uuid = task.value(QStringLiteral("model_uuid")).toString().trimmed();
    return !model_uuid.isEmpty() && modelForUuid(model_uuid) != nullptr;
}

bool ModelManager::modelTaskSupportsPause(const QString &model_uuid, ModelTaskType task_type) const
{
    IModel *model = modelForUuid(model_uuid);
    if (model == nullptr)
        return true;

    const FrameworkDefinition framework = registeredFramework(method_, model->frameworkName());
    return !ModelTaskPreparationService::frameworkHasScript(framework, task_type);
}

bool ModelManager::startTask(int task_id)
{
    auto *task_manager = TaskManager::getInstance();
    if (task_manager == nullptr || task_manager->tasks() == nullptr)
        return false;

    const QVariantMap task = task_manager->tasks()->taskForId(task_id);
    const int status_value = task.value(QStringLiteral("status_value"), TaskTableModel::Pending).toInt();
    if (status_value == TaskTableModel::Running)
        return true;

    const QString model_uuid = task.value(QStringLiteral("model_uuid")).toString().trimmed();
    const QString model_name = task.value(QStringLiteral("model_name")).toString().trimmed();
    const auto task_type = static_cast<ModelTaskType>(task.value(QStringLiteral("task_type"), 0).toInt());
    IModel *model = modelForUuid(model_uuid);
    if (model == nullptr)
        return false;

    const FrameworkDefinition framework = registeredFramework(method_, model->frameworkName());
    if (ModelTaskPreparationService::frameworkHasScript(framework, task_type))
        return startExternalModelTask(model_uuid, model_name, task_type, task_id) >= 0;

    return true;
}

bool ModelManager::stopTask(int task_id)
{
    auto *task_manager = TaskManager::getInstance();
    if (task_manager == nullptr || task_manager->tasks() == nullptr || !hasTaskHandler(task_id))
        return false;

    return external_task_runner_ != nullptr && external_task_runner_->stop(task_id);
}

bool ModelManager::deleteTask(int task_id)
{
    auto *task_manager = TaskManager::getInstance();
    if (task_manager == nullptr || task_manager->tasks() == nullptr || !hasTaskHandler(task_id))
        return false;

    return external_task_runner_ != nullptr && external_task_runner_->deleteTask(task_id);
}

bool ModelManager::registerFramework(const int method, const FrameworkDefinition &definition)
{
    FrameworkDefinition normalized = definition;
    normalized.method = method;
    normalized.name = definition.name.trimmed();
    const QString trimmed_framework_name = normalized.name;
    if (trimmed_framework_name.isEmpty())
    {
        return false;
    }

    auto      &registry = frameworkRegistry();
    const auto found
        = std::find_if(registry.begin(), registry.end(),
                       [method, &trimmed_framework_name](const RegisteredFramework &framework)
                       {
                           return framework.definition.method == method
                               && framework.definition.name == trimmed_framework_name;
                       });
    if (found != registry.end())
    {
        return false;
    }

    registry.push_back(RegisteredFramework{normalized});
    return true;
}

bool ModelManager::registerModel(const int method, const QString &framework_name, const QString &model_architecture,
                                 ModelFactory factory)
{
    const QString trimmed_framework_name     = framework_name.trimmed();
    const QString trimmed_model_architecture = model_architecture.trimmed();
    if (trimmed_framework_name.isEmpty() || trimmed_model_architecture.isEmpty() || !factory)
    {
        return false;
    }

    auto      &registry = modelRegistry();
    const auto found
        = std::find_if(registry.begin(), registry.end(),
                       [method, &trimmed_framework_name, &trimmed_model_architecture](const RegisteredModel &model)
                       {
                           return model.method == method && model.framework_name == trimmed_framework_name
                               && model.model_architecture == trimmed_model_architecture;
                       });
    if (found != registry.end())
    {
        return false;
    }

    registry.push_back(
        RegisteredModel{method, trimmed_framework_name, trimmed_model_architecture, std::move(factory)});
    return true;
}

ModelManager::FrameworkDefinition ModelManager::registeredFramework(const int method, const QString &framework_name)
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

QStringList ModelManager::registeredFrameworkNames(const int method)
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

QStringList ModelManager::registeredModelArchitectures(const int method, const QString &framework_name)
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

QStringList ModelManager::registeredModelNames(const int method)
{
    QStringList names;
    const auto &registry = modelRegistry();
    names.reserve(static_cast<int>(registry.size()));
    for (const RegisteredModel &model : registry)
    {
        if ((method < 0 || model.method == method) && !names.contains(model.model_architecture))
        {
            names.append(model.model_architecture);
        }
    }
    return names;
}

std::unique_ptr<IModel> ModelManager::createRegisteredModel(const int method, const QString &framework_name,
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

std::vector<std::unique_ptr<IModel>> ModelManager::registeredModels(const int method)
{
    std::vector<std::unique_ptr<IModel>> models;
    const auto                          &registry = modelRegistry();
    models.reserve(registry.size());
    for (const RegisteredModel &model : registry)
    {
        if ((method < 0 || model.method == method) && model.factory)
        {
            models.emplace_back(model.factory());
        }
    }
    return models;
}

std::unique_ptr<IModel> ModelManager::createRegisteredModelInstance(const QString &framework_name,
                                                                    const QString &model_architecture) const
{
    auto model = createRegisteredModel(method_, framework_name, model_architecture);
    initializeDatasetViewModels(model.get());
    return model;
}

std::vector<std::unique_ptr<IModel>> ModelManager::registeredModelInstances() const
{
    return registeredModels(method_);
}

int ModelManager::startExternalModelTask(const QString &model_uuid, const QString &model_name,
                                         ModelTaskType task_type, int task_id)
{
    IModel *model = modelForUuid(model_uuid);
    if (model == nullptr)
        return -1;

    const FrameworkDefinition framework = registeredFramework(method_, model->frameworkName());
    if (external_task_runner_ == nullptr)
        return -1;

    ModelTaskPreparationService::Request request;
    request.task_id    = task_id;
    request.model_uuid = model_uuid;
    request.model_name = model_name;
    request.task_type  = task_type;
    request.model      = model;
    request.framework  = framework;

    PreparedExternalModelTask      prepared;
    QString                        err_msg;
    const ModelTaskPreparationService preparation(method_, project_dir_, data_manager_);
    if (!preparation.prepare(request, prepared, &err_msg))
    {
        spdlog::error("启动模型任务失败: {}", err_msg.toUtf8().constData());
        if (auto *task_manager = TaskManager::getInstance(); task_manager != nullptr && task_manager->tasks() != nullptr)
            task_manager->tasks()->failTask(task_id);
        return task_id;
    }

    return external_task_runner_->start(prepared);
}

int ModelManager::indexOfModel(const int64_t model_id) const
{
    for (int i = 0; i < static_cast<int>(models_.size()); ++i)
    {
        if (models_[i].model_id == model_id)
            return i;
    }
    return -1;
}

int ModelManager::indexOfUuid(const QString &uuid) const
{
    const QString trimmed_uuid = uuid.trimmed();
    if (trimmed_uuid.isEmpty())
        return -1;
    for (int i = 0; i < static_cast<int>(models_.size()); ++i)
    {
        if (models_[static_cast<size_t>(i)].uuid == trimmed_uuid)
            return i;
    }
    return -1;
}

QString ModelManager::uniqueCopyName(const QString &name) const
{
    const QString base      = QString("%1 Copy").arg(name);
    QString       candidate = base;
    int           suffix    = 2;
    auto          exists    = [this](const QString &candidate_name)
    {
        return std::any_of(models_.begin(), models_.end(),
                           [&candidate_name](const ModelRecord &model) { return model.name == candidate_name; });
    };

    while (exists(candidate))
    {
        candidate = QString("%1 %2").arg(base).arg(suffix++);
    }
    return candidate;
}

IModel *ModelManager::cachedModelForRecord(const ModelRecord &record) const
{
    const QString trimmed_uuid               = record.uuid.trimmed();
    const QString trimmed_framework_name     = record.framework_name.trimmed();
    const QString trimmed_model_architecture = record.model_architecture.trimmed();
    if (trimmed_framework_name.isEmpty() || trimmed_model_architecture.isEmpty())
    {
        return nullptr;
    }

    if (trimmed_uuid.isEmpty() || indexOfUuid(trimmed_uuid) < 0)
    {
        return nullptr;
    }

    auto &model = model_instances_[instanceKey(trimmed_uuid)];
    if (!model || model->frameworkName() != trimmed_framework_name
        || model->modelArchitecture() != trimmed_model_architecture)
    {
        model = createRegisteredModelInstance(trimmed_framework_name, trimmed_model_architecture);
        if (model)
        {
            model->setParent(const_cast<ModelManager *>(this));
            model->setUuid(trimmed_uuid);
            QQmlEngine::setObjectOwnership(model.get(), QQmlEngine::CppOwnership);
        }
    }
    return model.get();
}

void ModelManager::initializeDatasetViewModels(IModel *model) const
{
    if (model == nullptr || data_manager_ == nullptr)
        return;

    if (model->trainDatasetViewModel() == nullptr)
    {
        model->setTrainDatasetViewModel(
            dltool::data::DatasetViewModelFactory::createDatasetSelectionModel(data_manager_, model));
    }
    if (model->validationDatasetViewModel() == nullptr)
    {
        model->setValidationDatasetViewModel(
            dltool::data::DatasetViewModelFactory::createDatasetSelectionModel(data_manager_, model));
    }
    if (model->testDatasetViewModel() == nullptr)
    {
        model->setTestDatasetViewModel(
            dltool::data::DatasetViewModelFactory::createDatasetSelectionModel(data_manager_, model));
    }
}

std::string ModelManager::instanceKey(const QString &uuid)
{
    return uuid.toStdString();
}

QVariant ModelManager::getModelId(const QModelIndex &index) const
{
    return static_cast<qint64>(models_.at(index.row()).model_id);
}

QVariant ModelManager::getUuid(const QModelIndex &index) const
{
    return models_.at(index.row()).uuid;
}

QVariant ModelManager::getName(const QModelIndex &index) const
{
    return models_.at(index.row()).name;
}

QVariant ModelManager::getFrameworkName(const QModelIndex &index) const
{
    return models_.at(index.row()).framework_name;
}

QVariant ModelManager::getModelArchitecture(const QModelIndex &index) const
{
    return models_.at(index.row()).model_architecture;
}

QVariant ModelManager::getTrainingResult(const QModelIndex &index) const
{
    const QString &value = models_.at(index.row()).training_result;
    return value.isEmpty() ? QString("未训练") : value;
}

QVariant ModelManager::getTestResult(const QModelIndex &index) const
{
    const QString &value = models_.at(index.row()).test_result;
    return value.isEmpty() ? QString("未测试") : value;
}

QVariant ModelManager::getCtime(const QModelIndex &index) const
{
    return QDateTime::fromSecsSinceEpoch(models_.at(index.row()).ctime).toString(QStringLiteral("yyyy/MM/dd hh:mm"));
}

QVariant ModelManager::getMtime(const QModelIndex &index) const
{
    return QDateTime::fromSecsSinceEpoch(models_.at(index.row()).mtime).toString(QStringLiteral("yyyy/MM/dd hh:mm"));
}

} // namespace dltool::model
