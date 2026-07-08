#include "model/ModelManager.h"

#include "common/Utils.h"
#include "data/DataSelectionTreeModel.h"
#include "data/DatasetViewModelFactory.h"
#include "database/DataBase.h"
#include "model/IModelConfig.h"
#include "model/IParams.h"
#include "model/ModelDatasetSelection.h"
#include "model/ModelStorageService.h"
#include "model/ModelTaskConfigService.h"

#include <spdlog/spdlog.h>

#include <QDateTime>
#include <QFileInfo>
#include <QQmlEngine>
#include <algorithm>
#include <utility>

namespace dltool::model {

ModelManager::ModelManager(const int method, dltool::database::ProjectDataBase *database,
                           dltool::data::DataManager *data_manager, QObject *parent)
    : QAbstractListModel(parent)
    , database_(database)
    , data_manager_(data_manager)
    , method_(method)
    , project_dir_(database != nullptr
                       ? dltool::common::cleanPath(QFileInfo(database->path()).absoluteDir().absolutePath())
                       : QString())
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
        {          ModelIdRole,           "model_id"},
        {             UuidRole,               "uuid"},
        {             NameRole,               "name"},
        {    FrameworkNameRole,     "framework_name"},
        {ModelArchitectureRole, "model_architecture"},
        {   TrainingResultRole,    "training_result"},
        {       TestResultRole,        "test_result"},
        {            CtimeRole,              "ctime"},
        {            MtimeRole,              "mtime"},
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

    QString       err_msg;
    const QString uuid = models_[static_cast<size_t>(row)].uuid;
    const bool    ok   = database_ != nullptr && database_->deleteModel(model_id, err_msg);
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

    const ModelRecord  &source = models_[row];
    QString             err_msg;
    int64_t             new_model_id{-1};
    const qint64        now         = QDateTime::currentSecsSinceEpoch();
    const QString       copied_name = uniqueCopyName(source.name);
    const QString       new_uuid    = dltool::common::uuid();
    ModelStorageService storage(project_dir_);
    if (!storage.ensureModelStorage(new_uuid, &err_msg))
    {
        spdlog::error("复制模型失败, 创建模型目录失败: {}", err_msg.toUtf8().constData());
        return false;
    }

    const bool ok = database_ != nullptr
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
        {          QStringLiteral("model_id"), static_cast<qint64>(model.model_id)},
        {              QStringLiteral("uuid"),                          model.uuid},
        {              QStringLiteral("name"),                          model.name},
        {    QStringLiteral("framework_name"),                model.framework_name},
        {QStringLiteral("model_architecture"),            model.model_architecture},
        {   QStringLiteral("training_result"),               model.training_result},
        {       QStringLiteral("test_result"),                   model.test_result},
        {             QStringLiteral("ctime"),                         model.ctime},
        {             QStringLiteral("mtime"),                         model.mtime},
    };
}

QVariantMap ModelManager::modelRecordForUuid(const QString &uuid) const
{
    const int row = indexOfUuid(uuid);
    if (row < 0)
        return {};
    return modelAt(row);
}

ModelManager::ModelRecordView ModelManager::modelRecordViewForUuid(const QString &uuid) const
{
    const int row = indexOfUuid(uuid);
    if (row < 0)
        return {};
    return toRecordView(models_.at(static_cast<size_t>(row)));
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

void ModelManager::requestModelTaskConfigLoad(const QString &model_uuid) const
{
    const QString trimmed_uuid = model_uuid.trimmed();
    if (trimmed_uuid.isEmpty())
        return;

    const std::string key = instanceKey(trimmed_uuid);
    if (config_load_started_.find(key) != config_load_started_.end())
        return;
    config_load_started_.insert(key);

    const ModelTaskConfigService                config_service(project_dir_);
    const dltool::model::LoadedModelTaskConfigs configs = config_service.load(trimmed_uuid);
    const_cast<ModelManager *>(this)->applyLoadedModelTaskConfigs(configs.model_uuid, configs.train_params,
                                                                  configs.test_params);
}

void ModelManager::applyLoadedModelTaskConfigs(const QString &model_uuid, const QVariantMap &train_params,
                                               const QVariantMap &test_params)
{
    const auto found = model_instances_.find(instanceKey(model_uuid));
    if (found == model_instances_.end() || !found->second || found->second->config() == nullptr)
        return;

    IModel       *model        = found->second.get();
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
    const ModelStorageService storage(project_dir_);
    const QVariantMap         dataset_selections
        = readModelDatasetSelectionsFile(storage.path(model_uuid, ModelStorageLocation::Datasets));
    applyModelDatasetSelections(model, dataset_selections);
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

ModelManager::ModelRecordView ModelManager::toRecordView(const ModelRecord &record)
{
    return ModelRecordView{
        record.model_id,
        record.uuid,
        record.name,
        record.framework_name,
        record.model_architecture,
        record.training_result,
        record.test_result,
        record.ctime,
        record.mtime,
    };
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
