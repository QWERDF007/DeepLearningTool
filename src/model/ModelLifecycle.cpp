#include "model/ModelLifecycle.h"

#include "database/DataBase.h"
#include "database/ModelDataBase.h"
#include "model/ModelStorageService.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>

namespace dltool::model {

namespace {

enum class OperationKind
{
    Create,
    Copy,
    Rename,
    Remove,
};

struct OperationJournal
{
    QString       id;
    OperationKind kind{OperationKind::Create};
    QString       phase;
    qint64        model_id{-1};
    QString       uuid;
    QString       source_name;
    QString       target_name;
    QString       staging_path;
    QString       quarantine_path;
};

QString operationKindName(const OperationKind kind)
{
    switch (kind)
    {
    case OperationKind::Create:
        return QStringLiteral("create");
    case OperationKind::Copy:
        return QStringLiteral("copy");
    case OperationKind::Rename:
        return QStringLiteral("rename");
    case OperationKind::Remove:
        return QStringLiteral("remove");
    }
    return {};
}

bool parseOperationKind(const QString &value, OperationKind &kind)
{
    if (value == QStringLiteral("create"))
        kind = OperationKind::Create;
    else if (value == QStringLiteral("copy"))
        kind = OperationKind::Copy;
    else if (value == QStringLiteral("rename"))
        kind = OperationKind::Rename;
    else if (value == QStringLiteral("remove"))
        kind = OperationKind::Remove;
    else
        return false;
    return true;
}

bool setError(QString &error, const QString &message)
{
    error = message;
    return false;
}

QString newOperationId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

ModelLifecycleResult failed(const QString &error, const QString &operation_id = {})
{
    ModelLifecycleResult result;
    result.state        = ModelLifecycleState::Failed;
    result.operation_id = operation_id;
    result.error        = error;
    return result;
}

ModelLifecycleResult recoveryRequired(const QString &error, const QString &operation_id, const qint64 model_id = -1)
{
    ModelLifecycleResult result;
    result.state        = ModelLifecycleState::RecoveryRequired;
    result.operation_id = operation_id;
    result.model_id     = model_id;
    result.error        = error;
    return result;
}

QJsonObject toJson(const OperationJournal &journal)
{
    return {
        {QStringLiteral("id"), journal.id},
        {QStringLiteral("kind"), operationKindName(journal.kind)},
        {QStringLiteral("phase"), journal.phase},
        {QStringLiteral("model_id"), journal.model_id},
        {QStringLiteral("uuid"), journal.uuid},
        {QStringLiteral("source_name"), journal.source_name},
        {QStringLiteral("target_name"), journal.target_name},
        {QStringLiteral("staging_path"), journal.staging_path},
        {QStringLiteral("quarantine_path"), journal.quarantine_path},
    };
}

bool fromJson(const QJsonObject &object, OperationJournal &journal, QString &error)
{
    journal.id = object.value(QStringLiteral("id")).toString().trimmed();
    if (journal.id.isEmpty() || !parseOperationKind(object.value(QStringLiteral("kind")).toString(), journal.kind))
        return setError(error, QStringLiteral("模型操作记录缺少有效的操作身份"));

    journal.phase          = object.value(QStringLiteral("phase")).toString().trimmed();
    journal.model_id      = object.value(QStringLiteral("model_id")).toInteger(-1);
    journal.uuid          = object.value(QStringLiteral("uuid")).toString();
    journal.source_name   = object.value(QStringLiteral("source_name")).toString();
    journal.target_name   = object.value(QStringLiteral("target_name")).toString();
    journal.staging_path  = object.value(QStringLiteral("staging_path")).toString();
    journal.quarantine_path = object.value(QStringLiteral("quarantine_path")).toString();
    if (journal.phase.isEmpty())
        return setError(error, QStringLiteral("模型操作记录缺少操作阶段"));
    return true;
}

bool writeJournal(const IModelStorageAdapter &storage, const OperationJournal &journal, QString &error)
{
    const QString root = storage.operationRoot();
    const QString path = storage.operationJournalPath(journal.id);
    if (root.isEmpty() || path.isEmpty() || !QDir().mkpath(root))
        return setError(error, QStringLiteral("创建模型操作记录目录失败"));

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return setError(error, QStringLiteral("打开模型操作记录失败: %1").arg(file.errorString()));
    const QByteArray data = QJsonDocument(toJson(journal)).toJson(QJsonDocument::Compact);
    if (file.write(data) != data.size() || !file.commit())
        return setError(error, QStringLiteral("写入模型操作记录失败: %1").arg(file.errorString()));
    return true;
}

bool readJournal(const QString &path, OperationJournal &journal, QString &error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return setError(error, QStringLiteral("打开模型操作记录失败: %1").arg(file.errorString()));
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        return setError(error, QStringLiteral("解析模型操作记录失败: %1").arg(parse_error.errorString()));
    return fromJson(document.object(), journal, error);
}

bool removeJournal(const IModelStorageAdapter &storage, const QString &operation_id, QString &error)
{
    const QString path = storage.operationJournalPath(operation_id);
    if (path.isEmpty() || !QFileInfo::exists(path) || QFile::remove(path))
        return true;
    return setError(error, QStringLiteral("删除模型操作记录失败: %1").arg(path));
}

bool updateJournal(const IModelStorageAdapter &storage, OperationJournal &journal, const QString &phase,
                  QString &error)
{
    journal.phase = phase;
    return writeJournal(storage, journal, error);
}

bool cleanupPath(const IModelStorageAdapter &storage, const QString &path, QString &error)
{
    if (path.isEmpty() || !QDir(path).exists())
        return true;
    return storage.removeDirectory(path, &error);
}

} // namespace

ProjectModelRecordStore::ProjectModelRecordStore(dltool::database::ProjectDataBase *database)
    : database_(database)
{
}

bool ProjectModelRecordStore::addModel(const ModelLifecycleRecord &record, qint64 &model_id, QString &error)
{
    if (database_ == nullptr)
        return setError(error, QStringLiteral("项目数据库为空"));
    return database_->addModel(record.uuid, record.name, record.framework_name, record.model_architecture,
                               record.ctime, record.mtime, model_id, error);
}

bool ProjectModelRecordStore::updateModelName(const qint64 model_id, const QString &name, const qint64 mtime,
                                              QString &error)
{
    return database_ != nullptr && database_->updateModelName(model_id, name, mtime, error);
}

bool ProjectModelRecordStore::deleteModel(const qint64 model_id, QString &error)
{
    return database_ != nullptr && database_->deleteModel(model_id, error);
}

bool ProjectModelRecordStore::findModel(const qint64 model_id, ModelLifecycleRecord &record, bool &exists,
                                        QString &error) const
{
    exists = false;
    if (database_ == nullptr)
        return setError(error, QStringLiteral("项目数据库为空"));

    std::vector<int64_t>              model_ids;
    std::vector<QString>              uuids;
    std::vector<QString>              names;
    std::vector<QString>              framework_names;
    std::vector<QString>              model_architectures;
    std::vector<qint64>                ctimes;
    std::vector<qint64>                mtimes;
    std::vector<std::vector<uint8_t>> extra_data;
    if (!database_->getAllModels(model_ids, uuids, names, framework_names, model_architectures, ctimes, mtimes,
                                 extra_data, error))
        return false;

    for (size_t index = 0; index < model_ids.size(); ++index)
    {
        if (model_ids[index] != model_id)
            continue;
        record.model_id          = model_ids[index];
        record.uuid              = index < uuids.size() ? uuids[index] : QString();
        record.name              = index < names.size() ? names[index] : QString();
        record.framework_name    = index < framework_names.size() ? framework_names[index] : QString();
        record.model_architecture = index < model_architectures.size() ? model_architectures[index] : QString();
        record.ctime             = index < ctimes.size() ? ctimes[index] : 0;
        record.mtime             = index < mtimes.size() ? mtimes[index] : 0;
        exists                   = true;
        return true;
    }
    return true;
}

bool ProjectModelRecordStore::findModelByUuid(const QString &uuid, ModelLifecycleRecord &record, bool &exists,
                                              QString &error) const
{
    exists = false;
    if (database_ == nullptr)
        return setError(error, QStringLiteral("项目数据库为空"));
    if (uuid.trimmed().isEmpty())
        return true;

    std::vector<int64_t>              model_ids;
    std::vector<QString>              uuids;
    std::vector<QString>              names;
    std::vector<QString>              framework_names;
    std::vector<QString>              model_architectures;
    std::vector<qint64>               ctimes;
    std::vector<qint64>               mtimes;
    std::vector<std::vector<uint8_t>> extra_data;
    if (!database_->getAllModels(model_ids, uuids, names, framework_names, model_architectures, ctimes, mtimes,
                                 extra_data, error))
        return false;

    for (size_t index = 0; index < uuids.size(); ++index)
    {
        if (uuids[index] != uuid)
            continue;
        record.model_id           = index < model_ids.size() ? model_ids[index] : -1;
        record.uuid               = uuids[index];
        record.name               = index < names.size() ? names[index] : QString();
        record.framework_name     = index < framework_names.size() ? framework_names[index] : QString();
        record.model_architecture = index < model_architectures.size() ? model_architectures[index] : QString();
        record.ctime              = index < ctimes.size() ? ctimes[index] : 0;
        record.mtime              = index < mtimes.size() ? mtimes[index] : 0;
        exists                    = true;
        return true;
    }
    return true;
}

ModelLifecycle::ModelLifecycle(IModelRecordStore &records, IModelStorageAdapter &storage)
    : records_(records)
    , storage_(storage)
{
}

ModelLifecycleResult ModelLifecycle::create(ModelLifecycleRecord record)
{
    record.name = record.name.trimmed();
    if (record.name.isEmpty())
        return failed(QStringLiteral("模型名称为空"));

    const QString target = storage_.modelRoot(record.name);
    if (target.isEmpty() || QFileInfo::exists(target))
        return failed(QStringLiteral("目标模型目录已存在或路径无效: %1").arg(record.name));

    OperationJournal journal;
    journal.id           = newOperationId();
    journal.kind         = OperationKind::Create;
    journal.phase        = QStringLiteral("prepared");
    journal.uuid         = record.uuid;
    journal.target_name  = record.name;
    journal.staging_path = storage_.operationStagingRoot(journal.id);
    QString error;
    if (!writeJournal(storage_, journal, error))
        return failed(error, journal.id);

    if (!storage_.ensureModelStorageAt(journal.staging_path, &error))
    {
        cleanupPath(storage_, journal.staging_path, error);
        removeJournal(storage_, journal.id, error);
        return failed(error, journal.id);
    }
    if (!updateJournal(storage_, journal, QStringLiteral("staged"), error))
        return recoveryRequired(error, journal.id);

    qint64 model_id = -1;
    if (!records_.addModel(record, model_id, error))
    {
        QString cleanup_error;
        if (!cleanupPath(storage_, journal.staging_path, cleanup_error)
            || !removeJournal(storage_, journal.id, cleanup_error))
            return recoveryRequired(QStringLiteral("模型记录创建失败且清理失败: %1").arg(cleanup_error), journal.id);
        return failed(error, journal.id);
    }

    journal.model_id = model_id;
    if (!updateJournal(storage_, journal, QStringLiteral("database-committed"), error))
        return recoveryRequired(error, journal.id, model_id);

    if (!storage_.moveDirectory(journal.staging_path, target, &error))
        return recoveryRequired(error, journal.id, model_id);
    if (!updateJournal(storage_, journal, QStringLiteral("published"), error))
        return ModelLifecycleResult{ModelLifecycleState::Succeeded, model_id, journal.id, error, true};

    QString cleanup_error;
    if (!removeJournal(storage_, journal.id, cleanup_error))
        return ModelLifecycleResult{ModelLifecycleState::Succeeded, model_id, journal.id, cleanup_error, true};
    return ModelLifecycleResult{ModelLifecycleState::Succeeded, model_id, journal.id, {}, false};
}

ModelLifecycleResult ModelLifecycle::copy(const ModelLifecycleRecord &source, ModelLifecycleRecord target,
                                         const bool copy_train_weights)
{
    target.name = target.name.trimmed();
    const QString source_root = storage_.modelRoot(source.name);
    const QString target_root = storage_.modelRoot(target.name);
    if (source_root.isEmpty() || !QDir(source_root).exists())
        return failed(QStringLiteral("源模型目录不存在: %1").arg(source.name));
    if (target_root.isEmpty() || QFileInfo::exists(target_root))
        return failed(QStringLiteral("目标模型目录已存在或路径无效: %1").arg(target.name));

    OperationJournal journal;
    journal.id           = newOperationId();
    journal.kind         = OperationKind::Copy;
    journal.phase        = QStringLiteral("prepared");
    journal.uuid         = target.uuid;
    journal.source_name  = source.name;
    journal.target_name  = target.name;
    journal.staging_path = storage_.operationStagingRoot(journal.id);
    QString error;
    if (!writeJournal(storage_, journal, error))
        return failed(error, journal.id);
    if (!storage_.ensureModelStorageAt(journal.staging_path, &error))
    {
        cleanupPath(storage_, journal.staging_path, error);
        removeJournal(storage_, journal.id, error);
        return failed(error, journal.id);
    }

    {
        database::ModelDataBase source_database(storage_.modelDatabasePath(source.name));
        database::ModelDataBase target_database(storage_.modelDatabasePathAt(journal.staging_path));
        QVariantMap                             train_params;
        QList<database::DatasetSelectionRecord> selections;
        bool db_ok = true;
        if (!source_database.readTrainParams(train_params, &error))
            db_ok = false;
        else if (!source_database.readDatasets(selections, &error))
            db_ok = false;
        else if (!target_database.replaceTrainParams(train_params, &error))
            db_ok = false;
        else if (!target_database.replaceDatasets(selections, &error))
            db_ok = false;
        if (!db_ok)
        {
            QString cleanup_error;
            cleanupPath(storage_, journal.staging_path, cleanup_error);
            removeJournal(storage_, journal.id, cleanup_error);
            return failed(error, journal.id);
        }
    }

    if (copy_train_weights
        && !storage_.copyDirectoryContents(storage_.trainWeightsPathAt(source_root),
                                           storage_.trainWeightsPathAt(journal.staging_path), &error))
    {
        QString cleanup_error;
        cleanupPath(storage_, journal.staging_path, cleanup_error);
        removeJournal(storage_, journal.id, cleanup_error);
        return failed(error, journal.id);
    }
    if (!updateJournal(storage_, journal, QStringLiteral("staged"), error))
        return recoveryRequired(error, journal.id);

    qint64 model_id = -1;
    if (!records_.addModel(target, model_id, error))
    {
        QString cleanup_error;
        if (!cleanupPath(storage_, journal.staging_path, cleanup_error)
            || !removeJournal(storage_, journal.id, cleanup_error))
            return recoveryRequired(QStringLiteral("复制模型记录失败且清理失败: %1").arg(cleanup_error), journal.id);
        return failed(error, journal.id);
    }
    journal.model_id = model_id;
    if (!updateJournal(storage_, journal, QStringLiteral("database-committed"), error))
        return recoveryRequired(error, journal.id, model_id);
    if (!storage_.moveDirectory(journal.staging_path, target_root, &error))
        return recoveryRequired(error, journal.id, model_id);
    if (!updateJournal(storage_, journal, QStringLiteral("published"), error))
        return ModelLifecycleResult{ModelLifecycleState::Succeeded, model_id, journal.id, error, true};

    QString cleanup_error;
    if (!removeJournal(storage_, journal.id, cleanup_error))
        return ModelLifecycleResult{ModelLifecycleState::Succeeded, model_id, journal.id, cleanup_error, true};
    return ModelLifecycleResult{ModelLifecycleState::Succeeded, model_id, journal.id, {}, false};
}

ModelLifecycleResult ModelLifecycle::rename(const qint64 model_id, const QString &old_name, const QString &new_name)
{
    const QString source = storage_.modelRoot(old_name);
    const QString target = storage_.modelRoot(new_name.trimmed());
    if (source.isEmpty() || target.isEmpty() || !QDir(source).exists() || QFileInfo::exists(target))
        return failed(QStringLiteral("模型重命名路径无效或目标已存在"));

    OperationJournal journal;
    journal.id              = newOperationId();
    journal.kind            = OperationKind::Rename;
    journal.phase           = QStringLiteral("prepared");
    journal.model_id        = model_id;
    journal.source_name     = old_name;
    journal.target_name     = new_name.trimmed();
    journal.staging_path    = storage_.operationStagingRoot(journal.id);
    QString error;
    if (!writeJournal(storage_, journal, error))
        return failed(error, journal.id);
    if (!storage_.moveDirectory(source, journal.staging_path, &error))
    {
        removeJournal(storage_, journal.id, error);
        return failed(error, journal.id);
    }
    if (!updateJournal(storage_, journal, QStringLiteral("staged"), error))
        return recoveryRequired(error, journal.id, model_id);

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (!records_.updateModelName(model_id, journal.target_name, now, error))
    {
        QString rollback_error;
        if (!storage_.moveDirectory(journal.staging_path, source, &rollback_error)
            || !removeJournal(storage_, journal.id, rollback_error))
            return recoveryRequired(QStringLiteral("模型重命名失败且回滚失败: %1").arg(rollback_error), journal.id, model_id);
        return failed(error, journal.id);
    }
    if (!updateJournal(storage_, journal, QStringLiteral("database-committed"), error))
        return recoveryRequired(error, journal.id, model_id);
    if (!storage_.moveDirectory(journal.staging_path, target, &error))
        return recoveryRequired(error, journal.id, model_id);
    if (!updateJournal(storage_, journal, QStringLiteral("published"), error))
        return ModelLifecycleResult{ModelLifecycleState::Succeeded, model_id, journal.id, error, true};

    QString cleanup_error;
    if (!removeJournal(storage_, journal.id, cleanup_error))
        return ModelLifecycleResult{ModelLifecycleState::Succeeded, model_id, journal.id, cleanup_error, true};
    return ModelLifecycleResult{ModelLifecycleState::Succeeded, model_id, journal.id, {}, false};
}

ModelLifecycleResult ModelLifecycle::remove(const qint64 model_id, const QString &name)
{
    const QString source = storage_.modelRoot(name);
    if (source.isEmpty() || !QDir(source).exists())
        return failed(QStringLiteral("模型目录不存在: %1").arg(name));

    OperationJournal journal;
    journal.id               = newOperationId();
    journal.kind             = OperationKind::Remove;
    journal.phase            = QStringLiteral("prepared");
    journal.model_id         = model_id;
    journal.source_name      = name;
    journal.quarantine_path  = storage_.operationQuarantineRoot(journal.id);
    QString error;
    if (!writeJournal(storage_, journal, error))
        return failed(error, journal.id);
    if (!storage_.moveDirectory(source, journal.quarantine_path, &error))
    {
        removeJournal(storage_, journal.id, error);
        return failed(error, journal.id);
    }
    if (!updateJournal(storage_, journal, QStringLiteral("staged"), error))
        return recoveryRequired(error, journal.id, model_id);

    if (!records_.deleteModel(model_id, error))
    {
        QString rollback_error;
        if (!storage_.moveDirectory(journal.quarantine_path, source, &rollback_error)
            || !removeJournal(storage_, journal.id, rollback_error))
            return recoveryRequired(QStringLiteral("模型删除失败且回滚失败: %1").arg(rollback_error), journal.id, model_id);
        return failed(error, journal.id);
    }
    if (!updateJournal(storage_, journal, QStringLiteral("database-committed"), error))
        return recoveryRequired(error, journal.id, model_id);

    QString cleanup_error;
    if (!cleanupPath(storage_, journal.quarantine_path, cleanup_error))
    {
        updateJournal(storage_, journal, QStringLiteral("cleanup-pending"), cleanup_error);
        return ModelLifecycleResult{ModelLifecycleState::Succeeded, model_id, journal.id, cleanup_error, true};
    }
    if (!removeJournal(storage_, journal.id, cleanup_error))
        return ModelLifecycleResult{ModelLifecycleState::Succeeded, model_id, journal.id, cleanup_error, true};
    return ModelLifecycleResult{ModelLifecycleState::Succeeded, model_id, journal.id, {}, false};
}

ModelLifecycleResult ModelLifecycle::recoverPending()
{
    const QString root = storage_.operationRoot();
    if (root.isEmpty() || !QDir(root).exists())
        return ModelLifecycleResult{ModelLifecycleState::Succeeded};

    const QStringList paths = QDir(root).entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    QStringList        errors;
    for (const QString &file_name : paths)
    {
        OperationJournal journal;
        QString           error;
        const QString     path = QDir(root).filePath(file_name);
        if (!readJournal(path, journal, error))
        {
            errors.push_back(error);
            continue;
        }

        ModelLifecycleRecord record;
        bool                 exists = false;
        if (journal.model_id > 0)
        {
            if (!records_.findModel(journal.model_id, record, exists, error))
            {
                errors.push_back(error);
                continue;
            }
        }
        if (!exists && !journal.uuid.isEmpty())
        {
            if (!records_.findModelByUuid(journal.uuid, record, exists, error))
            {
                errors.push_back(error);
                continue;
            }
            if (exists)
                journal.model_id = record.model_id;
        }

        const QString target = storage_.modelRoot(journal.target_name);
        const QString source = storage_.modelRoot(journal.source_name);
        bool          recovered = false;
        switch (journal.kind)
        {
        case OperationKind::Create:
        case OperationKind::Copy:
            if (exists && QFileInfo::exists(target))
            {
                recovered = cleanupPath(storage_, journal.staging_path, error);
            }
            else if (exists && QDir(journal.staging_path).exists())
            {
                recovered = storage_.moveDirectory(journal.staging_path, target, &error);
                if (recovered && QDir(journal.staging_path).exists())
                    recovered = cleanupPath(storage_, journal.staging_path, error);
            }
            else if (!exists)
            {
                bool ok = cleanupPath(storage_, journal.staging_path, error);
                if (QFileInfo::exists(target))
                    ok = cleanupPath(storage_, target, error) && ok;
                recovered = ok;
            }
            break;
        case OperationKind::Rename:
            if (exists && record.name == journal.target_name)
            {
                if (QFileInfo::exists(target))
                    recovered = cleanupPath(storage_, journal.staging_path, error);
                else if (QDir(journal.staging_path).exists())
                    recovered = storage_.moveDirectory(journal.staging_path, target, &error);
            }
            else if (exists && record.name == journal.source_name)
            {
                if (QFileInfo::exists(source))
                    recovered = cleanupPath(storage_, journal.staging_path, error);
                else if (QDir(journal.staging_path).exists())
                    recovered = storage_.moveDirectory(journal.staging_path, source, &error);
                else if (QFileInfo::exists(target))
                    recovered = storage_.moveDirectory(target, source, &error);
            }
            break;
        case OperationKind::Remove:
            if (exists)
            {
                if (QFileInfo::exists(source))
                    recovered = cleanupPath(storage_, journal.quarantine_path, error);
                else if (QDir(journal.quarantine_path).exists())
                    recovered = storage_.moveDirectory(journal.quarantine_path, source, &error);
            }
            else
            {
                recovered = cleanupPath(storage_, journal.quarantine_path, error);
            }
            break;
        }

        if (!recovered)
        {
            errors.push_back(error.isEmpty() ? QStringLiteral("模型操作恢复失败") : error);
            continue;
        }
        if (!removeJournal(storage_, journal.id, error))
            errors.push_back(error);
    }

    if (!errors.isEmpty())
    {
        ModelLifecycleResult result;
        result.state = ModelLifecycleState::RecoveryRequired;
        result.error = errors.join(QStringLiteral("；"));
        return result;
    }
    return ModelLifecycleResult{ModelLifecycleState::Succeeded};
}

} // namespace dltool::model
