#include "model/ModelManager.h"

#include "database/DataBase.h"
#include "model/IParams.h"

#include <spdlog/spdlog.h>

#include <QDateTime>
#include <QQmlEngine>
#include <algorithm>
#include <utility>

namespace dltool::model {

namespace {

struct RegisteredModel
{
    int                        method{-1};
    QString                    name;
    ModelManager::ModelFactory factory;
};

std::vector<RegisteredModel> &modelRegistry()
{
    static std::vector<RegisteredModel> registry;
    return registry;
}

} // namespace

ModelManager::ModelManager(const int method, dltool::database::ProjectDataBase *database, QObject *parent)
    : QAbstractListModel(parent)
    , database_(database)
    , method_(method)
{
    init();
}

ModelManager::~ModelManager() {}

void ModelManager::init()
{
    beginResetModel();
    models_.clear();
    model_instances_.clear();

    if (database_ == nullptr)
    {
        endResetModel();
        spdlog::error("init model manager failed: database is null");
        return;
    }

    std::vector<int64_t> model_ids;
    std::vector<QString> names;
    std::vector<QString> network_structures;
    std::vector<QString> training_results;
    std::vector<QString> test_results;
    std::vector<qint64>  ctimes;
    std::vector<qint64>  mtimes;
    QString              err_msg;

    const bool ok = database_->getAllModels(model_ids, names, network_structures, training_results, test_results,
                                            ctimes, mtimes, err_msg);
    if (!ok)
    {
        endResetModel();
        spdlog::error("query all models failed, error: {}", err_msg.toUtf8().constData());
        return;
    }

    models_.reserve(model_ids.size());
    for (size_t i = 0; i < model_ids.size(); ++i)
    {
        models_.push_back(ModelRecord{
            model_ids[i],
            names[i],
            network_structures[i],
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
    case NameRole:
        return getName(index);
    case NetworkStructureRole:
        return getNetworkStructure(index);
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
        {         ModelIdRole,          "model_id"},
        {            NameRole,              "name"},
        {NetworkStructureRole, "network_structure"},
        {  TrainingResultRole,   "training_result"},
        {      TestResultRole,       "test_result"},
        {           CtimeRole,             "ctime"},
        {           MtimeRole,             "mtime"},
    };
}

bool ModelManager::addModel(const QString &name, const QString &network_structure)
{
    const QString trimmed_name              = name.trimmed();
    const QString trimmed_network_structure = network_structure.trimmed();
    if (trimmed_name.isEmpty() || trimmed_network_structure.isEmpty())
    {
        spdlog::warn("add model failed: model name or network structure is empty");
        return false;
    }

    if (!registeredModelNames(method_).contains(trimmed_network_structure))
    {
        spdlog::warn("add model failed: model is not registered for method {}, network: {}", method_,
                     trimmed_network_structure.toUtf8().constData());
        return false;
    }

    if (database_ == nullptr)
    {
        spdlog::error("add model failed: database is null");
        return false;
    }

    QString      err_msg;
    int64_t      model_id{-1};
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const bool   ok  = database_->addModel(trimmed_name, trimmed_network_structure, now, now, model_id, err_msg);
    if (!ok)
    {
        spdlog::error("add model failed, name: {}, network: {}, error: {}", trimmed_name.toUtf8().constData(),
                      trimmed_network_structure.toUtf8().constData(), err_msg.toUtf8().constData());
        return false;
    }

    const int row = rowCount();
    beginInsertRows(QModelIndex(), row, row);
    models_.push_back(ModelRecord{
        model_id,
        trimmed_name,
        trimmed_network_structure,
        QString(),
        QString(),
        now,
        now,
    });
    endInsertRows();

    spdlog::info("add model succeeded, id: {}, name: {}, network: {}", model_id, trimmed_name.toUtf8().constData(),
                 trimmed_network_structure.toUtf8().constData());
    return true;
}

bool ModelManager::renameModel(const qint64 model_id, const QString &name)
{
    const QString trimmed_name = name.trimmed();
    if (trimmed_name.isEmpty())
    {
        spdlog::warn("rename model failed: model name is empty");
        return false;
    }

    const int row = indexOfModel(model_id);
    if (row < 0)
    {
        spdlog::warn("rename model failed: model {} not found", model_id);
        return false;
    }

    QString      err_msg;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const bool   ok  = database_ != nullptr && database_->updateModelName(model_id, trimmed_name, now, err_msg);
    if (!ok)
    {
        spdlog::error("rename model failed, id: {}, error: {}", model_id, err_msg.toUtf8().constData());
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
        spdlog::warn("delete model failed: model {} not found", model_id);
        return false;
    }

    QString    err_msg;
    const bool ok = database_ != nullptr && database_->deleteModel(model_id, err_msg);
    if (!ok)
    {
        spdlog::error("delete model failed, id: {}, error: {}", model_id, err_msg.toUtf8().constData());
        return false;
    }

    beginRemoveRows(QModelIndex(), row, row);
    models_.erase(models_.begin() + row);
    endRemoveRows();
    model_instances_.erase(model_id);
    return true;
}

bool ModelManager::copyModel(const qint64 model_id)
{
    const int row = indexOfModel(model_id);
    if (row < 0)
    {
        spdlog::warn("copy model failed: model {} not found", model_id);
        return false;
    }

    const ModelRecord &source = models_[row];
    QString            err_msg;
    int64_t            new_model_id{-1};
    const qint64       now         = QDateTime::currentSecsSinceEpoch();
    const QString      copied_name = uniqueCopyName(source.name);
    const bool         ok          = database_ != nullptr
                 && database_->addModel(copied_name, source.network_structure, source.training_result,
                                        source.test_result, now, now, new_model_id, err_msg);
    if (!ok)
    {
        spdlog::error("copy model failed, id: {}, error: {}", model_id, err_msg.toUtf8().constData());
        return false;
    }

    const int insert_row = rowCount();
    beginInsertRows(QModelIndex(), insert_row, insert_row);
    models_.push_back(ModelRecord{
        new_model_id,
        copied_name,
        source.network_structure,
        source.training_result,
        source.test_result,
        now,
        now,
    });
    endInsertRows();

    const auto source_found = model_instances_.find(model_id);
    if (source_found != model_instances_.end() && source_found->second)
    {
        auto copied_model = createRegisteredModelInstance(source.network_structure);
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
            QQmlEngine::setObjectOwnership(copied_model.get(), QQmlEngine::CppOwnership);
            model_instances_[new_model_id] = std::move(copied_model);
        }
    }

    return true;
}

QStringList ModelManager::supportedNetworkStructures() const
{
    return registeredModelNames(method_);
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
        {             QStringLiteral("name"),                          model.name},
        {QStringLiteral("network_structure"),             model.network_structure},
        {  QStringLiteral("training_result"),               model.training_result},
        {      QStringLiteral("test_result"),                   model.test_result},
        {            QStringLiteral("ctime"),                         model.ctime},
        {            QStringLiteral("mtime"),                         model.mtime},
    };
}

IModel *ModelManager::modelForId(const qint64 model_id, const QString &network_structure) const
{
    return cachedModelForRecord(model_id, network_structure);
}

bool ModelManager::registerModel(const int method, const QString &type_name, ModelFactory factory)
{
    const QString trimmed_type_name = type_name.trimmed();
    if (trimmed_type_name.isEmpty() || !factory)
    {
        return false;
    }

    auto      &registry = modelRegistry();
    const auto found
        = std::find_if(registry.begin(), registry.end(), [method, &trimmed_type_name](const RegisteredModel &model)
                       { return model.method == method && model.name == trimmed_type_name; });
    if (found != registry.end())
    {
        return false;
    }

    registry.push_back(RegisteredModel{method, trimmed_type_name, std::move(factory)});
    return true;
}

bool ModelManager::registerModel(const QString &type_name, ModelFactory factory)
{
    return registerModel(-1, type_name, std::move(factory));
}

QStringList ModelManager::registeredModelNames(const int method)
{
    QStringList names;
    const auto &registry = modelRegistry();
    names.reserve(static_cast<int>(registry.size()));
    for (const RegisteredModel &model : registry)
    {
        if (method < 0 || model.method == method)
        {
            names.append(model.name);
        }
    }
    return names;
}

QStringList ModelManager::registeredModelNames()
{
    return registeredModelNames(-1);
}

std::unique_ptr<IModel> ModelManager::createRegisteredModel(const int method, const QString &type_name)
{
    const auto &registry = modelRegistry();
    const auto found = std::find_if(registry.begin(), registry.end(), [method, &type_name](const RegisteredModel &model)
                                    { return (method < 0 || model.method == method) && model.name == type_name; });
    if (found == registry.end() || !found->factory)
    {
        return nullptr;
    }
    return found->factory();
}

std::unique_ptr<IModel> ModelManager::createRegisteredModel(const QString &type_name)
{
    return createRegisteredModel(-1, type_name);
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

std::vector<std::unique_ptr<IModel>> ModelManager::registeredModels()
{
    return registeredModels(-1);
}

std::unique_ptr<IModel> ModelManager::createRegisteredModelInstance(const QString &type_name) const
{
    return createRegisteredModel(method_, type_name);
}

std::vector<std::unique_ptr<IModel>> ModelManager::registeredModelInstances() const
{
    return registeredModels(method_);
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

IModel *ModelManager::cachedModelForRecord(const qint64 model_id, const QString &network_structure) const
{
    const QString trimmed_network_structure = network_structure.trimmed();
    if (trimmed_network_structure.isEmpty())
    {
        return nullptr;
    }

    if (model_id < 0 || indexOfModel(model_id) < 0)
    {
        return nullptr;
    }

    auto &model = model_instances_[model_id];
    if (!model || model->typeName() != trimmed_network_structure)
    {
        model = createRegisteredModelInstance(trimmed_network_structure);
        if (model)
        {
            model->setParent(const_cast<ModelManager *>(this));
            QQmlEngine::setObjectOwnership(model.get(), QQmlEngine::CppOwnership);
        }
    }
    return model.get();
}

QVariant ModelManager::getModelId(const QModelIndex &index) const
{
    return static_cast<qint64>(models_.at(index.row()).model_id);
}

QVariant ModelManager::getName(const QModelIndex &index) const
{
    return models_.at(index.row()).name;
}

QVariant ModelManager::getNetworkStructure(const QModelIndex &index) const
{
    return models_.at(index.row()).network_structure;
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
