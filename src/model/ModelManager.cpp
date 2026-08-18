#include "model/ModelManager.h"

#include "common/Utils.h"
#include "data/DataSelectionTreeModel.h"
#include "data/DatasetViewModelFactory.h"
#include "database/DataBase.h"
#include "database/ModelDataBase.h"
#include "model/IModelConfig.h"
#include "model/IParams.h"
#include "model/ModelDatasetSelection.h"
#include "model/ModelStorageService.h"
#include "model/TaskManager.h"
#include "settings/GlobalSettings.h"
#include "settings/SettingsKeys.h"
#include "settings/SettingsValue.h"

#include <spdlog/spdlog.h>

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcess>
#include <QProcessEnvironment>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QSortFilterProxyModel>
#include <QTcpServer>
#include <algorithm>
#include <utility>

namespace dltool::model {
using common::setError;

namespace {

bool copyDirectoryContents(const QString &source, const QString &target, QString *err_msg)
{
    const QFileInfo source_info(source);
    if (!source_info.exists() || !source_info.isDir())
        return true;
    if (!QDir().mkpath(target))
    {
        if (err_msg)
            *err_msg = QString("创建模型权重目录失败: %1").arg(target);
        return false;
    }
    QDirIterator iterator(source, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext())
    {
        const QFileInfo item(iterator.next());
        const QString   relative    = QDir(source).relativeFilePath(item.absoluteFilePath());
        const QString   destination = QDir(target).filePath(relative);
        if (item.isDir())
        {
            if (!QDir().mkpath(destination))
            {
                if (err_msg)
                    *err_msg = QString("创建模型权重子目录失败: %1").arg(destination);
                return false;
            }
        }
        else if (!QFile::copy(item.absoluteFilePath(), destination))
        {
            if (err_msg)
                *err_msg = QString("复制模型权重文件失败: %1").arg(item.absoluteFilePath());
            return false;
        }
    }
    return true;
}

bool modelHasActiveTasks(const QString &model_uuid)
{
    const TaskManager *task_manager = TaskManager::getInstance();
    return task_manager != nullptr && task_manager->hasActiveModelTasks(model_uuid);
}

class UserVisibleModelProxy final : public QSortFilterProxyModel
{
public:
    explicit UserVisibleModelProxy(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent)
    {
    }

protected:
    bool filterAcceptsRow(const int source_row, const QModelIndex &source_parent) const override
    {
        const auto *manager = qobject_cast<const ModelManager *>(sourceModel());
        if (manager == nullptr)
            return false;

        const QModelIndex source_index   = manager->index(source_row, 0, source_parent);
        const QString     framework_name = manager->data(source_index, ModelManager::FrameworkNameRole).toString();
        if (framework_name.trimmed().isEmpty())
            return false;

        return registeredFramework(manager->method(), framework_name).visible_for_model_creation;
    }
};

QVariantMap extraDataFromBlob(const std::vector<uint8_t> &data)
{
    if (data.empty())
        return {};

    QJsonParseError     error;
    const QJsonDocument document = QJsonDocument::fromJson(
        QByteArray(reinterpret_cast<const char *>(data.data()), static_cast<int>(data.size())), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return {};
    return document.object().toVariantMap();
}

std::vector<uint8_t> extraDataToBlob(const QVariantMap &data)
{
    const QByteArray     serialized = QJsonDocument::fromVariant(data).toJson(QJsonDocument::Compact);
    std::vector<uint8_t> result;
    result.reserve(static_cast<size_t>(serialized.size()));
    for (const char byte : serialized) result.push_back(static_cast<uint8_t>(byte));
    return result;
}

quint16 availableTensorBoardPort()
{
    QTcpServer server;
    if (server.listen(QHostAddress::LocalHost, 6006))
    {
        server.close();
        return 6006;
    }

    if (server.listen(QHostAddress::LocalHost, 0))
    {
        const quint16 port = server.serverPort();
        server.close();
        return port;
    }

    return 0;
}

} // namespace

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
    user_visible_model_ = new UserVisibleModelProxy(this);
    user_visible_model_->setSourceModel(this);
    user_visible_model_->setFilterRole(FrameworkNameRole);
    user_visible_model_->setDynamicSortFilter(true);
    init();
}

ModelManager::~ModelManager()
{
    if (tensorboard_process_ != nullptr && tensorboard_process_->state() != QProcess::NotRunning)
    {
        tensorboard_process_->terminate();
        if (!tensorboard_process_->waitForFinished(2000))
        {
            tensorboard_process_->kill();
            tensorboard_process_->waitForFinished(1000);
        }
    }
    tensorboard_port_ = 0;
}

QString ModelManager::projectDatabasePath() const
{
    return database_ != nullptr ? database_->path() : QString();
}

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

    std::vector<int64_t>              model_ids;
    std::vector<QString>              uuids;
    std::vector<QString>              names;
    std::vector<QString>              framework_names;
    std::vector<QString>              model_architectures;
    std::vector<qint64>               ctimes;
    std::vector<qint64>               mtimes;
    std::vector<std::vector<uint8_t>> extra_data;
    QString                           err_msg;

    const bool ok = database_->getAllModels(model_ids, uuids, names, framework_names, model_architectures, ctimes,
                                            mtimes, extra_data, err_msg);
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
            ctimes[i],
            mtimes[i],
            i < extra_data.size() ? extraDataFromBlob(extra_data[i]) : QVariantMap{},
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
    case CtimeRole:
        return getCtime(index);
    case MtimeRole:
        return getMtime(index);
    case ExtraDataRole:
        return getExtraData(index);
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
        {            CtimeRole,              "ctime"},
        {            MtimeRole,              "mtime"},
        {        ExtraDataRole,         "extra_data"},
    };
}

QString ModelManager::validateModelName(const QString &name) const
{
    const QString trimmed_name = name.trimmed();
    if (trimmed_name.isEmpty())
        return QString("模型名称不能为空");

    static const QRegularExpression valid_name_pattern(QStringLiteral("^[\\p{Han}A-Za-z0-9_-]+$"));
    if (!valid_name_pattern.match(trimmed_name).hasMatch())
        return QString("模型名称仅支持中文、英文、数字、下划线和连字符");

    for (const ModelRecord &model : models_)
    {
        if (model.name == trimmed_name)
            return QString("模型名称已存在");
    }
    return {};
}

bool ModelManager::addModel(const QString &name, const QString &framework_name, const QString &model_architecture)
{
    return addModelRecord(name, framework_name, model_architecture).isValid();
}

ModelManager::ModelRecordView ModelManager::addModelRecord(const QString &name, const QString &framework_name,
                                                           const QString &model_architecture, QString *err_msg)
{
    const QString trimmed_name               = name.trimmed();
    const QString trimmed_framework_name     = framework_name.trimmed();
    const QString trimmed_model_architecture = model_architecture.trimmed();
    const QString name_error                 = validateModelName(name);
    if (!name_error.isEmpty())
    {
        setError(err_msg, name_error);
        spdlog::warn("添加模型失败: {}", name_error.toUtf8().constData());
        return {};
    }
    if (trimmed_framework_name.isEmpty() || trimmed_model_architecture.isEmpty())
    {
        const QString message = QString("模型框架或模型架构为空");
        setError(err_msg, message);
        spdlog::warn("添加模型失败: {}", message.toUtf8().constData());
        return {};
    }

    if (!registeredModelArchitectures(method_, trimmed_framework_name).contains(trimmed_model_architecture))
    {
        const QString message = QString("模型未注册, 方法: %1, 框架: %2, 模型架构: %3")
                                    .arg(method_)
                                    .arg(trimmed_framework_name, trimmed_model_architecture);
        setError(err_msg, message);
        spdlog::warn("添加模型失败: {}", message.toUtf8().constData());
        return {};
    }

    const FrameworkDefinition framework = registeredFramework(method_, trimmed_framework_name);
    if (framework.name.isEmpty())
    {
        const QString message = QString("框架未注册: %1").arg(trimmed_framework_name);
        setError(err_msg, message);
        spdlog::warn("添加模型失败: {}", message.toUtf8().constData());
        return {};
    }
    const bool write_to_database = framework.write_to_database;
    if (write_to_database && database_ == nullptr)
    {
        const QString message = QString("数据库对象为空");
        setError(err_msg, message);
        spdlog::error("添加模型失败: {}", message.toUtf8().constData());
        return {};
    }

    QString       local_err_msg;
    int64_t       model_id{-1};
    const qint64  now  = QDateTime::currentSecsSinceEpoch();
    const QString uuid = dltool::common::uuid();
    if (!write_to_database)
    {
        while (indexOfModel(model_id) >= 0) --model_id;
    }

    ModelStorageService storage(project_dir_);
    const QString       model_dir = storage.path(trimmed_name, ModelStorageLocation::ModelRoot);
    if (QFileInfo::exists(model_dir))
    {
        const QString message = QString("模型目录已存在: %1").arg(model_dir);
        setError(err_msg, message);
        spdlog::warn("添加模型失败: {}", message.toUtf8().constData());
        return {};
    }

    if (!storage.ensureModelStorage(trimmed_name, &local_err_msg))
    {
        setError(err_msg, QString("创建模型目录失败: %1").arg(local_err_msg));
        spdlog::error("添加模型失败, 创建模型目录失败: {}", local_err_msg.toUtf8().constData());
        return {};
    }

    if (write_to_database
        && !database_->addModel(uuid, trimmed_name, trimmed_framework_name, trimmed_model_architecture, now, now,
                                model_id, local_err_msg))
    {
        QString remove_err;
        storage.removeModelStorage(trimmed_name, &remove_err);
        setError(err_msg, local_err_msg);
        spdlog::error("添加模型失败, 名称: {}, 框架: {}, 模型架构: {}, 错误: {}", trimmed_name.toUtf8().constData(),
                      trimmed_framework_name.toUtf8().constData(), trimmed_model_architecture.toUtf8().constData(),
                      local_err_msg.toUtf8().constData());
        return {};
    }

    const ModelRecord record{model_id, uuid, trimmed_name, trimmed_framework_name, trimmed_model_architecture,
                             now,      now};
    const int         row = rowCount();
    beginInsertRows(QModelIndex(), row, row);
    models_.push_back(record);
    endInsertRows();

    spdlog::info("模型添加成功, id: {}, 写入数据库: {}, 模型名称: {}, 框架: {}, 模型架构: {}", model_id,
                 write_to_database, trimmed_name.toUtf8().constData(), trimmed_framework_name.toUtf8().constData(),
                 trimmed_model_architecture.toUtf8().constData());
    return toRecordView(record);
}

bool ModelManager::renameModel(const qint64 model_id, const QString &name)
{
    const QString name_error = validateModelName(name);
    if (!name_error.isEmpty())
    {
        spdlog::warn("模型重命名失败: {}", name_error.toUtf8().constData());
        return false;
    }
    const QString trimmed_name = name.trimmed();

    const int row = indexOfModel(model_id);
    if (row < 0)
    {
        spdlog::warn("模型重命名失败: 模型 {} 不存在", model_id);
        return false;
    }

    const QString old_name = models_[static_cast<size_t>(row)].name;
    if (modelHasActiveTasks(models_[static_cast<size_t>(row)].uuid))
    {
        spdlog::warn("模型重命名失败: 模型仍有活动任务");
        return false;
    }
    ModelStorageService storage(project_dir_);
    QString             err_msg;
    if (!storage.renameModelStorage(old_name, trimmed_name, &err_msg))
    {
        spdlog::error("重命名模型目录失败, id: {}, 错误: {}", model_id, err_msg.toUtf8().constData());
        return false;
    }

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const bool   ok  = database_ != nullptr && database_->updateModelName(model_id, trimmed_name, now, err_msg);
    if (!ok)
    {
        QString rollback_err;
        if (!storage.renameModelStorage(trimmed_name, old_name, &rollback_err))
            spdlog::error("回滚模型目录重命名失败, id: {}, 错误: {}", model_id, rollback_err.toUtf8().constData());
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

    const ModelRecord record = models_[static_cast<size_t>(row)];
    if (modelHasActiveTasks(record.uuid))
    {
        spdlog::warn("模型删除失败: 模型仍有活动任务");
        return false;
    }
    QString                   err_msg;
    const QString             uuid              = record.uuid;
    const QString             name              = record.name;
    const FrameworkDefinition framework         = registeredFramework(method_, record.framework_name);
    const bool                write_to_database = framework.name.isEmpty() || framework.write_to_database;
    const bool ok = !write_to_database || (database_ != nullptr && database_->deleteModel(model_id, err_msg));
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
    if (!storage.removeModelStorage(name, &err_msg))
    {
        spdlog::error("删除模型目录失败, 名称: {}, 错误: {}", name.toUtf8().constData(), err_msg.toUtf8().constData());
    }
    spdlog::info("模型删除成功, id: {}, 写入数据库: {}, 模型名称: {}", model_id, write_to_database,
                 name.toUtf8().constData());
    return true;
}

bool ModelManager::copyModel(const qint64 model_id, const bool copy_train_weights)
{
    const int row = indexOfModel(model_id);
    if (row < 0)
    {
        spdlog::warn("复制模型失败: 模型 {} 不存在", model_id);
        return false;
    }

    // Keep the source record stable while inserting the copied model. The
    // insertion may reallocate models_ and invalidate references into it.
    const ModelRecord   source = models_[row];
    QString             err_msg;
    int64_t             new_model_id{-1};
    const qint64        now         = QDateTime::currentSecsSinceEpoch();
    const QString       copied_name = uniqueCopyName(source.name);
    const QString       new_uuid    = dltool::common::uuid();
    ModelStorageService storage(project_dir_);
    if (QFileInfo::exists(storage.path(copied_name, ModelStorageLocation::ModelRoot)))
    {
        spdlog::error("复制模型失败, 模型目录已存在: {}",
                      storage.path(copied_name, ModelStorageLocation::ModelRoot).toUtf8().constData());
        return false;
    }
    if (!storage.ensureModelStorage(copied_name, &err_msg))
    {
        spdlog::error("复制模型失败, 创建模型目录失败: {}", err_msg.toUtf8().constData());
        return false;
    }

    // Copy only model-level parameters and dataset selections by default.
    // Test-task databases/results/logs are intentionally not copied; weights
    // are an explicit opt-in from the copy dialog/API.
    database::ModelDataBase                 source_database(storage.modelDatabasePath(source.name));
    database::ModelDataBase                 target_database(storage.modelDatabasePath(copied_name));
    QVariantMap                             source_train_params;
    QList<database::DatasetSelectionRecord> source_dataset_selections;
    if (!source_database.readTrainParams(source_train_params, &err_msg)
        || !source_database.readDatasets(source_dataset_selections, &err_msg)
        || !target_database.replaceTrainParams(source_train_params, &err_msg)
        || !target_database.replaceDatasets(source_dataset_selections, &err_msg))
    {
        spdlog::error("复制模型数据库内容失败: {}", err_msg.toUtf8().constData());
        QString remove_err;
        storage.removeModelStorage(copied_name, &remove_err);
        return false;
    }
    if (copy_train_weights)
    {
        if (!copyDirectoryContents(storage.trainWeightsPath(source.name), storage.trainWeightsPath(copied_name),
                                   &err_msg))
        {
            spdlog::error("复制模型权重失败: {}", err_msg.toUtf8().constData());
            QString remove_err;
            storage.removeModelStorage(copied_name, &remove_err);
            return false;
        }
    }

    const bool ok = database_ != nullptr
                 && database_->addModel(new_uuid, copied_name, source.framework_name, source.model_architecture, now,
                                        now, new_model_id, err_msg);
    if (!ok)
    {
        QString remove_err;
        storage.removeModelStorage(copied_name, &remove_err);
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
            ITrainParams       *target_train_params      = copied_model->config()->trainParams();
            const ITrainParams *source_train_inst_params = source_found->second->config()->trainParams();
            if (target_train_params != nullptr && source_train_inst_params != nullptr)
            {
                target_train_params->copyValuesFrom(*source_train_inst_params);
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
        {             QStringLiteral("ctime"),        formatTimestamp(model.ctime)},
        {             QStringLiteral("mtime"),        formatTimestamp(model.mtime)},
        {        QStringLiteral("extra_data"),                    model.extra_data},
    };
}

QVariantMap ModelManager::userVisibleModelAt(const int row) const
{
    if (user_visible_model_ == nullptr || row < 0 || row >= user_visible_model_->rowCount())
        return {};

    const QModelIndex source_index = user_visible_model_->mapToSource(user_visible_model_->index(row, 0));
    if (!source_index.isValid())
        return {};

    return modelAt(source_index.row());
}

QAbstractItemModel *ModelManager::userVisibleModel() const
{
    return user_visible_model_;
}

QVariantMap ModelManager::modelRecordForUuid(const QString &uuid) const
{
    const int row = indexOfUuid(uuid);
    if (row < 0)
        return {};
    return modelAt(row);
}

bool ModelManager::updateModelExtraData(const QString &model_uuid, const QVariantMap &updates, QString *err_msg)
{
    const int row = indexOfUuid(model_uuid.trimmed());
    if (row < 0)
    {
        const QString message = QString("模型不存在: %1").arg(model_uuid);
        setError(err_msg, message);
        return false;
    }

    if (updates.isEmpty())
        return true;

    ModelRecord &record = models_[static_cast<size_t>(row)];
    QVariantMap  merged = record.extra_data;
    for (auto it = updates.cbegin(); it != updates.cend(); ++it) merged.insert(it.key(), it.value());

    if (merged == record.extra_data)
        return true;

    const FrameworkDefinition framework         = registeredFramework(method_, record.framework_name);
    const bool                write_to_database = framework.name.isEmpty() || framework.write_to_database;
    QString                   local_err_msg;
    if (write_to_database)
    {
        if (database_ == nullptr)
        {
            setError(err_msg, QString("数据库对象为空"));
            return false;
        }
        if (!database_->updateModelExtraData(record.model_id, extraDataToBlob(merged), local_err_msg))
        {
            setError(err_msg, local_err_msg);
            spdlog::error("更新模型扩展数据失败, uuid: {}, 错误: {}", record.uuid.toUtf8().constData(),
                          local_err_msg.toUtf8().constData());
            return false;
        }
    }

    record.extra_data = merged;
    emit dataChanged(index(row), index(row), {ExtraDataRole});
    emit modelExtraDataChanged(record.uuid);
    return true;
}

bool ModelManager::resetModelTaskState(const QString &model_uuid, const QString &section_key, const QStringList &fields,
                                       const QVariantMap &preset, QString *err_msg)
{
    const int row = indexOfUuid(model_uuid.trimmed());
    if (row < 0)
        return setError(err_msg, QString("模型不存在: %1").arg(model_uuid.trimmed()));

    ModelRecord &record  = models_[static_cast<size_t>(row)];
    QVariantMap  merged  = record.extra_data;
    QVariantMap  section = merged.value(section_key).toMap();
    bool         changed = false;
    for (const QString &field : fields) changed = section.remove(field) > 0 || changed;
    for (auto it = preset.cbegin(); it != preset.cend(); ++it)
        changed = section.value(it.key()) != it.value() || changed;
    if (!changed)
        return true;

    for (auto it = preset.cbegin(); it != preset.cend(); ++it) section.insert(it.key(), it.value());
    if (section.isEmpty())
        merged.remove(section_key);
    else
        merged.insert(section_key, section);
    if (merged == record.extra_data)
        return true;

    const FrameworkDefinition framework         = registeredFramework(method_, record.framework_name);
    const bool                write_to_database = framework.name.isEmpty() || framework.write_to_database;
    QString                   local_err_msg;
    if (write_to_database)
    {
        if (database_ == nullptr)
        {
            setError(err_msg, QString("数据库对象为空"));
            return false;
        }
        if (!database_->updateModelExtraData(record.model_id, extraDataToBlob(merged), local_err_msg))
        {
            setError(err_msg, local_err_msg);
            spdlog::error("重置模型任务状态失败, uuid: {}, 错误: {}", record.uuid.toUtf8().constData(),
                          local_err_msg.toUtf8().constData());
            return false;
        }
    }

    record.extra_data = merged;
    emit dataChanged(index(row), index(row), {ExtraDataRole});
    emit modelExtraDataChanged(record.uuid);
    return true;
}

bool ModelManager::touchModelModifiedTime(const QString &model_uuid, QString *err_msg)
{
    const int row = indexOfUuid(model_uuid.trimmed());
    if (row < 0)
    {
        const QString message = QString("模型不存在: %1").arg(model_uuid);
        setError(err_msg, message);
        return false;
    }

    ModelRecord              &record            = models_[static_cast<size_t>(row)];
    const qint64              now               = QDateTime::currentSecsSinceEpoch();
    const FrameworkDefinition framework         = registeredFramework(method_, record.framework_name);
    const bool                write_to_database = framework.name.isEmpty() || framework.write_to_database;
    QString                   local_err_msg;
    if (write_to_database)
    {
        if (database_ == nullptr)
        {
            setError(err_msg, QString("数据库对象为空"));
            return false;
        }
        if (!database_->updateModelMtime(record.model_id, now, local_err_msg))
        {
            setError(err_msg, local_err_msg);
            spdlog::error("更新模型修改时间失败, uuid: {}, 错误: {}", record.uuid.toUtf8().constData(),
                          local_err_msg.toUtf8().constData());
            return false;
        }
    }

    record.mtime = now;
    emit dataChanged(index(row), index(row), {MtimeRole});
    return true;
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

    const int model_row = indexOfUuid(trimmed_uuid);
    if (model_row < 0)
        return;

    const QString                           model_name = models_[static_cast<size_t>(model_row)].name;
    const ModelStorageService               storage(project_dir_);
    database::ModelDataBase                 model_database(storage.modelDatabasePath(model_name));
    QVariantMap                             train_params;
    QList<database::DatasetSelectionRecord> dataset_records;
    QString                                 error;
    if (!model_database.readTrainParams(train_params, &error) || !model_database.readDatasets(dataset_records, &error))
    {
        spdlog::error("读取模型数据库失败, 模型: {}, 错误: {}", model_name.toUtf8().constData(),
                      error.toUtf8().constData());
        return;
    }
    const_cast<ModelManager *>(this)->applyLoadedModelTaskConfigs(trimmed_uuid, train_params,
                                                                  modelDatasetSelectionsFromDatabase(dataset_records));
}

QString ModelManager::startTensorBoard(const QString &model_uuid)
{
    const ModelRecordView record = modelRecordViewForUuid(model_uuid);
    if (!record.isValid() || record.name.trimmed().isEmpty())
    {
        spdlog::error("启动 TensorBoard 失败: 模型记录不存在, uuid: {}", model_uuid.toUtf8().constData());
        return {};
    }

    if (tensorboard_process_ != nullptr && tensorboard_process_->state() != QProcess::NotRunning
        && tensorboard_model_uuid_ == record.uuid)
    {
        spdlog::debug("TensorBoard 已在运行, 模型: {}", record.name.toUtf8().constData());
        return QStringLiteral("http://127.0.0.1:%1/").arg(tensorboard_port_);
    }

    if (tensorboard_process_ != nullptr)
    {
        if (tensorboard_process_->state() != QProcess::NotRunning)
        {
            spdlog::info("切换 TensorBoard 模型, 停止旧进程: {}", tensorboard_model_uuid_.toUtf8().constData());
            tensorboard_process_->terminate();
            if (!tensorboard_process_->waitForFinished(1000))
            {
                tensorboard_process_->kill();
                tensorboard_process_->waitForFinished(1000);
            }
        }
        tensorboard_process_->deleteLater();
        tensorboard_process_ = nullptr;
        tensorboard_model_uuid_.clear();
        tensorboard_port_ = 0;
    }

    const QString   python_env_path = dltool::settings::GlobalSettings::pythonEnvironmentPath();
    const QFileInfo python_env_info(dltool::common::cleanPath(python_env_path));
    const QString python = (!python_env_path.trimmed().isEmpty() && python_env_info.exists() && python_env_info.isDir())
                             ? dltool::common::pythonExecutableFromEnvPath(python_env_path)
                             : QString();
    const QString log_dir = ModelStorageService(project_dir_).trainLogsPath(record.name);
    if (python_env_path.trimmed().isEmpty())
    {
        spdlog::error("启动 TensorBoard 失败: 未配置 Python 环境目录");
        return {};
    }
    if (!python_env_info.exists() || !python_env_info.isDir())
    {
        spdlog::error("启动 TensorBoard 失败: Python 环境目录无效: {}", python_env_path.toUtf8().constData());
        return {};
    }
    if (python.isEmpty())
    {
        spdlog::error("启动 TensorBoard 失败: Python 可执行文件不存在, 环境目录: {}",
                      python_env_path.toUtf8().constData());
        return {};
    }
    if (log_dir.isEmpty())
    {
        spdlog::error("启动 TensorBoard 失败: 模型日志目录为空, 模型: {}", record.name.toUtf8().constData());
        return {};
    }
    QString directory_error;
    if (!dltool::common::ensureDirectory(log_dir, &directory_error))
    {
        spdlog::error("启动 TensorBoard 失败: 创建模型日志目录失败, 目录: {}, 原因: {}", log_dir.toUtf8().constData(),
                      directory_error.toUtf8().constData());
        return {};
    }

    const quint16 port = availableTensorBoardPort();
    if (port == 0)
    {
        spdlog::error("启动 TensorBoard 失败: 无法找到可用本地端口");
        return {};
    }

    tensorboard_process_ = new QProcess(this);
    tensorboard_process_->setProgram(python);
    tensorboard_process_->setArguments({QStringLiteral("-m"), QStringLiteral("tensorboard.main"),
                                        QStringLiteral("--logdir"), log_dir, QStringLiteral("--host"),
                                        QStringLiteral("127.0.0.1"), QStringLiteral("--port"), QString::number(port)});
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    tensorboard_process_->setProcessEnvironment(env);
    QProcess *process = tensorboard_process_;
    connect(process, &QProcess::readyReadStandardError, this,
            [process]()
            {
                const QByteArray output = process->readAllStandardError();
                if (!output.isEmpty())
                    spdlog::error("TensorBoard: {}", QString::fromLocal8Bit(output).trimmed().toUtf8().constData());
            });
    connect(process, &QProcess::errorOccurred, this, [process](QProcess::ProcessError)
            { spdlog::error("TensorBoard 进程错误: {}", process->errorString().toUtf8().constData()); });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [process](int exit_code, QProcess::ExitStatus exit_status)
            {
                if (exit_code != 0 || exit_status != QProcess::NormalExit)
                    spdlog::error("TensorBoard 异常退出, 退出码: {}, 状态: {}", exit_code,
                                  exit_status == QProcess::NormalExit ? "normal" : "crashed");
            });
    tensorboard_model_uuid_ = record.uuid;
    tensorboard_port_       = port;
    spdlog::info("启动 TensorBoard, 模型: {}, 日志目录: {}", record.name.toUtf8().constData(),
                 log_dir.toUtf8().constData());
    tensorboard_process_->start();
    return QStringLiteral("http://127.0.0.1:%1/").arg(tensorboard_port_);
}

void ModelManager::applyLoadedModelTaskConfigs(const QString &model_uuid, const QVariantMap &train_params,
                                               const ModelDatasetSelections &dataset_selections)
{
    const auto found = model_instances_.find(instanceKey(model_uuid));
    if (found == model_instances_.end() || !found->second || found->second->config() == nullptr)
        return;

    IModel       *model        = found->second.get();
    IModelConfig *model_config = found->second->config();
    const int     model_index  = indexOfUuid(model_uuid);
    const QString model_name   = model_index >= 0 ? models_.at(static_cast<size_t>(model_index)).name : QString();
    if (ITrainParams *params = model_config->trainParams(); params != nullptr)
    {
        params->setWeightContext(project_dir_, projectDatabasePath(), model->frameworkName(),
                                 model->modelArchitecture(), model_name);
        params->setValuesMap(train_params);
    }
    if (ITestParams *params = model_config->testParams(); params != nullptr)
    {
        params->setWeightContext(project_dir_, projectDatabasePath(), model->frameworkName(),
                                 model->modelArchitecture(), model_name);
    }
    applyModelDatasetSelections(model, modelDatasetSelectionsMap(dataset_selections));
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
            if (IModelConfig *model_config = model->config(); model_config != nullptr)
            {
                if (ITrainParams *params = model_config->trainParams(); params != nullptr)
                {
                    params->setWeightContext(project_dir_, projectDatabasePath(), model->frameworkName(),
                                             model->modelArchitecture(), record.name);
                }
                if (ITestParams *params = model_config->testParams(); params != nullptr)
                {
                    params->setWeightContext(project_dir_, projectDatabasePath(), model->frameworkName(),
                                             model->modelArchitecture(), record.name);
                }
            }
        }
    }
    return model.get();
}

ModelManager::ModelRecordView ModelManager::toRecordView(const ModelRecord &record)
{
    return ModelRecordView{
        record.model_id,           record.uuid,  record.name,  record.framework_name,
        record.model_architecture, record.ctime, record.mtime,
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

QVariant ModelManager::getCtime(const QModelIndex &index) const
{
    return formatTimestamp(models_.at(index.row()).ctime);
}

QVariant ModelManager::getMtime(const QModelIndex &index) const
{
    return formatTimestamp(models_.at(index.row()).mtime);
}

QVariant ModelManager::getExtraData(const QModelIndex &index) const
{
    return models_.at(index.row()).extra_data;
}

QString ModelManager::formatTimestamp(qint64 timestamp)
{
    if (timestamp <= 0)
        return QString();
    return QDateTime::fromSecsSinceEpoch(timestamp).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

} // namespace dltool::model
