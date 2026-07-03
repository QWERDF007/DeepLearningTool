#include "model/ModelManager.h"

#include "common/Utils.h"
#include "data/DataSelectionTreeModel.h"
#include "data/DatasetViewModelFactory.h"
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

ModelManager::ModelManager(const int method,
                           dltool::database::ProjectDataBase *database,
                           dltool::data::DataManager *data_manager,
                           QObject *parent)
    : QAbstractListModel(parent)
    , database_(database)
    , data_manager_(data_manager)
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
        spdlog::error("初始化模型管理器失败: 数据库对象为空");
        return;
    }

    std::vector<int64_t> model_ids;
    std::vector<QString> uuids;
    std::vector<QString> names;
    std::vector<QString> network_structures;
    std::vector<QString> training_results;
    std::vector<QString> test_results;
    std::vector<qint64>  ctimes;
    std::vector<qint64>  mtimes;
    QString              err_msg;

    const bool ok = database_->getAllModels(model_ids, uuids, names, network_structures, training_results, test_results,
                                            ctimes, mtimes, err_msg);
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
    case UuidRole:
        return getUuid(index);
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
        {            UuidRole,              "uuid"},
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
        spdlog::warn("添加模型失败: 模型名称或网络结构为空");
        return false;
    }

    if (!registeredModelNames(method_).contains(trimmed_network_structure))
    {
        spdlog::warn("添加模型失败: 模型未注册, 方法: {}, 网络结构: {}", method_,
                     trimmed_network_structure.toUtf8().constData());
        return false;
    }

    if (database_ == nullptr)
    {
        spdlog::error("添加模型失败: 数据库对象为空");
        return false;
    }

    QString       err_msg;
    int64_t       model_id{-1};
    const qint64  now  = QDateTime::currentSecsSinceEpoch();
    const QString uuid = dltool::common::uuid();
    const bool ok = database_->addModel(uuid, trimmed_name, trimmed_network_structure, QString(), QString(), now, now,
                                        model_id, err_msg);
    if (!ok)
    {
        spdlog::error("添加模型失败, 名称: {}, 网络结构: {}, 错误: {}", trimmed_name.toUtf8().constData(),
                      trimmed_network_structure.toUtf8().constData(), err_msg.toUtf8().constData());
        return false;
    }

    const int row = rowCount();
    beginInsertRows(QModelIndex(), row, row);
    models_.push_back(ModelRecord{
        model_id,
        uuid,
        trimmed_name,
        trimmed_network_structure,
        QString(),
        QString(),
        now,
        now,
    });
    endInsertRows();

    spdlog::info("模型添加成功, id: {}, 模型名称: {}, 网络结构: {}", model_id, trimmed_name.toUtf8().constData(),
                 trimmed_network_structure.toUtf8().constData());
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
    const bool ok = database_ != nullptr && database_->deleteModel(model_id, err_msg);
    if (!ok)
    {
        spdlog::error("删除模型失败, id: {}, 错误: {}", model_id, err_msg.toUtf8().constData());
        return false;
    }

    beginRemoveRows(QModelIndex(), row, row);
    const QString uuid = models_[static_cast<size_t>(row)].uuid;
    models_.erase(models_.begin() + row);
    endRemoveRows();
    model_instances_.erase(instanceKey(uuid));
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
    const bool         ok          = database_ != nullptr
                 && database_->addModel(new_uuid, copied_name, source.network_structure, source.training_result,
                                        source.test_result, now, now, new_model_id, err_msg);
    if (!ok)
    {
        spdlog::error("复制模型失败, id: {}, 错误: {}", model_id, err_msg.toUtf8().constData());
        return false;
    }

    const int insert_row = rowCount();
    beginInsertRows(QModelIndex(), insert_row, insert_row);
    models_.push_back(ModelRecord{
        new_model_id,
        new_uuid,
        copied_name,
        source.network_structure,
        source.training_result,
        source.test_result,
        now,
        now,
    });
    endInsertRows();

    const auto source_found = model_instances_.find(instanceKey(source.uuid));
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
            copied_model->setUuid(new_uuid);
            QQmlEngine::setObjectOwnership(copied_model.get(), QQmlEngine::CppOwnership);
            model_instances_[instanceKey(new_uuid)] = std::move(copied_model);
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
        {             QStringLiteral("uuid"),                          model.uuid},
        {             QStringLiteral("name"),                          model.name},
        {QStringLiteral("network_structure"),             model.network_structure},
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
    return cachedModelForRecord(models_[static_cast<size_t>(row)]);
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
        if ((method < 0 || model.method == method) && !names.contains(model.name))
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
    const QString trimmed_type_name = type_name.trimmed();
    if (trimmed_type_name.isEmpty())
    {
        return nullptr;
    }

    const auto &registry = modelRegistry();
    const auto  found
        = std::find_if(registry.begin(), registry.end(), [method, &trimmed_type_name](const RegisteredModel &model)
                       { return (method < 0 || model.method == method) && model.name == trimmed_type_name; });
    if (found == registry.end() || !found->factory)
        return nullptr;

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
    auto model = createRegisteredModel(method_, type_name);
    initializeDatasetViewModels(model.get());
    return model;
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
    const QString trimmed_uuid              = record.uuid.trimmed();
    const QString trimmed_network_structure = record.network_structure.trimmed();
    if (trimmed_network_structure.isEmpty())
    {
        return nullptr;
    }

    if (trimmed_uuid.isEmpty() || indexOfUuid(trimmed_uuid) < 0)
    {
        return nullptr;
    }

    auto &model = model_instances_[instanceKey(trimmed_uuid)];
    if (!model || model->typeName() != trimmed_network_structure)
    {
        model = createRegisteredModelInstance(trimmed_network_structure);
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
