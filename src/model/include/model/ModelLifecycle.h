#pragma once

#include "dltool/model/Export.h"

#include <QString>

#include <cstdint>

namespace dltool::database {
class ProjectDataBase;
}

namespace dltool::model {

struct MODEL_API ModelLifecycleRecord
{
    qint64  model_id{-1};
    QString uuid;
    QString name;
    QString framework_name;
    QString model_architecture;
    qint64  ctime{0};
    qint64  mtime{0};
};

enum class ModelLifecycleState
{
    Succeeded,
    Failed,
    RecoveryRequired,
};

struct MODEL_API ModelLifecycleResult
{
    ModelLifecycleState state{ModelLifecycleState::Failed};
    qint64              model_id{-1};
    QString              operation_id;
    QString              error;
    bool                 cleanup_pending{false};

    bool succeeded() const
    {
        return state == ModelLifecycleState::Succeeded;
    }

    bool recoveryRequired() const
    {
        return state == ModelLifecycleState::RecoveryRequired;
    }
};

/**
 * @brief 模型元数据持久化 seam。
 *
 * 生命周期模块只依赖模型记录的最小读写能力，不直接依赖 Qt Model 或项目数据库的 SQL 接口。
 */
class MODEL_API IModelRecordStore
{
public:
    virtual ~IModelRecordStore() = default;

    virtual bool addModel(const ModelLifecycleRecord &record, qint64 &model_id, QString &error) = 0;
    virtual bool updateModelName(qint64 model_id, const QString &name, qint64 mtime, QString &error) = 0;
    virtual bool deleteModel(qint64 model_id, QString &error) = 0;
    virtual bool findModel(qint64 model_id, ModelLifecycleRecord &record, bool &exists, QString &error) const = 0;
    virtual bool findModelByUuid(const QString &uuid, ModelLifecycleRecord &record, bool &exists, QString &error) const = 0;
};

/**
 * @brief 项目数据库模型记录 Adapter。
 */
class MODEL_API ProjectModelRecordStore final : public IModelRecordStore
{
public:
    explicit ProjectModelRecordStore(dltool::database::ProjectDataBase *database);

    bool addModel(const ModelLifecycleRecord &record, qint64 &model_id, QString &error) override;
    bool updateModelName(qint64 model_id, const QString &name, qint64 mtime, QString &error) override;
    bool deleteModel(qint64 model_id, QString &error) override;
    bool findModel(qint64 model_id, ModelLifecycleRecord &record, bool &exists, QString &error) const override;
    bool findModelByUuid(const QString &uuid, ModelLifecycleRecord &record, bool &exists, QString &error) const override;

private:
    dltool::database::ProjectDataBase *database_{nullptr};
};

/**
 * @brief 模型文件系统 Adapter。
 *
 * ModelStorageService 是生产实现；生命周期模块通过该 seam 组织 staging、quarantine 和发布，
 * 测试可以注入一个可故障的文件 Adapter 验证跨介质恢复。
 */
class MODEL_API IModelStorageAdapter
{
public:
    virtual ~IModelStorageAdapter() = default;

    virtual QString modelsRootPath() const = 0;
    virtual QString modelRoot(const QString &name) const = 0;
    virtual QString modelDatabasePath(const QString &name) const = 0;
    virtual QString modelDatabasePathAt(const QString &root) const = 0;
    virtual QString trainWeightsPathAt(const QString &root) const = 0;
    virtual QString operationRoot() const = 0;
    virtual QString operationStagingRoot(const QString &operation_id) const = 0;
    virtual QString operationQuarantineRoot(const QString &operation_id) const = 0;
    virtual QString operationJournalPath(const QString &operation_id) const = 0;

    virtual bool ensureModelStorageAt(const QString &root, QString *error = nullptr) const = 0;
    virtual bool copyDirectoryContents(const QString &source, const QString &target, QString *error = nullptr) const = 0;
    virtual bool moveDirectory(const QString &source, const QString &target, QString *error = nullptr) = 0;
    virtual bool removeDirectory(const QString &root, QString *error = nullptr) const = 0;
};

/**
 * @brief 协调模型记录、model.db 和模型目录的可恢复生命周期。
 */
class MODEL_API ModelLifecycle final
{
public:
    ModelLifecycle(IModelRecordStore &records, IModelStorageAdapter &storage);

    ModelLifecycleResult create(ModelLifecycleRecord record);
    ModelLifecycleResult copy(const ModelLifecycleRecord &source, ModelLifecycleRecord target,
                              bool copy_train_weights);
    ModelLifecycleResult rename(qint64 model_id, const QString &old_name, const QString &new_name);
    ModelLifecycleResult remove(qint64 model_id, const QString &name);

    /**
     * @brief 恢复启动前未完成的 staging/quarantine 操作。
     */
    ModelLifecycleResult recoverPending();

private:
    IModelRecordStore   &records_;
    IModelStorageAdapter &storage_;
};

} // namespace dltool::model
