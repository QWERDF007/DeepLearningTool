#pragma once

#include "IModel.h"
#include "dltool/model/Export.h"
#include "model/ModelRegistry.h"

#include <QAbstractListModel>
#include <QStringList>
#include <QVariantMap>
#include <QtQml>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class QSortFilterProxyModel;
class QProcess;

namespace dltool::database {
class ProjectDataBase;
} // namespace dltool::database

namespace dltool::data {
class DataManager;
} // namespace dltool::data

namespace dltool::model {

class MODEL_API ModelManager : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ModelManager)
    QML_UNCREATABLE("Can not create ModelManager directly!")
public:
    explicit ModelManager(const int method, dltool::database::ProjectDataBase *database,
                          dltool::data::DataManager *data_manager, QObject *parent = nullptr);
    ~ModelManager();

    Q_PROPERTY(int method READ method CONSTANT FINAL)
    Q_PROPERTY(QAbstractItemModel *userVisibleModel READ userVisibleModel CONSTANT FINAL)

    enum Role
    {
        ModelIdRole = Qt::UserRole + 1,
        UuidRole,
        NameRole,
        FrameworkNameRole,
        ModelArchitectureRole,
        CtimeRole,
        MtimeRole,
    };

    struct ModelRecordView
    {
        int64_t model_id{-1};
        QString uuid;
        QString name;
        QString framework_name;
        QString model_architecture;
        qint64  ctime{0};
        qint64  mtime{0};

        bool isValid() const
        {
            return model_id >= 0 && !uuid.trimmed().isEmpty();
        }
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE bool addModel(const QString &name, const QString &framework_name, const QString &model_architecture);
    Q_INVOKABLE bool renameModel(const qint64 model_id, const QString &name);
    Q_INVOKABLE QString validateModelName(const QString &name) const;
    Q_INVOKABLE bool deleteModel(const qint64 model_id);
    Q_INVOKABLE bool copyModel(const qint64 model_id);

    Q_INVOKABLE QStringList supportedFrameworks() const;
    Q_INVOKABLE QStringList supportedModelArchitectures(const QString &framework_name) const;

    Q_INVOKABLE QStringList availableModelNames() const;

    Q_INVOKABLE QVariantMap modelAt(int row) const;
    Q_INVOKABLE QVariantMap userVisibleModelAt(int row) const;
    QVariantMap             modelRecordForUuid(const QString &uuid) const;
    ModelRecordView         modelRecordViewForUuid(const QString &uuid) const;

    Q_INVOKABLE dltool::model::IModel *modelForUuid(const QString &uuid) const;
    Q_INVOKABLE QString startTensorBoard(const QString &model_uuid);

    int method() const
    {
        return method_;
    }

    QAbstractItemModel *userVisibleModel() const;

    QString projectDirectory() const
    {
        return project_dir_;
    }

    std::unique_ptr<IModel>              createRegisteredModelInstance(const QString &framework_name,
                                                                       const QString &model_architecture) const;
    std::vector<std::unique_ptr<IModel>> registeredModelInstances() const;

    ModelRecordView addModelRecord(const QString &name, const QString &framework_name,
                                   const QString &model_architecture, QString *err_msg = nullptr);

private:
    struct ModelRecord
    {
        int64_t model_id{-1};
        QString uuid;
        QString name;
        QString framework_name;
        QString model_architecture;
        qint64  ctime{0};
        qint64  mtime{0};
    };

    void init();

    int indexOfModel(const int64_t model_id) const;

    int indexOfUuid(const QString &uuid) const;

    QString uniqueCopyName(const QString &name) const;

    IModel *cachedModelForRecord(const ModelRecord &record) const;

    static ModelRecordView toRecordView(const ModelRecord &record);

    void initializeDatasetViewModels(IModel *model) const;

    void requestModelTaskConfigLoad(const QString &model_uuid) const;

    void applyLoadedModelTaskConfigs(const QString &model_uuid, const QString &model_name,
                                     const QVariantMap &train_params, const QVariantMap &test_params);

    static std::string instanceKey(const QString &uuid);

    QVariant getModelId(const QModelIndex &index) const;
    QVariant getUuid(const QModelIndex &index) const;
    QVariant getName(const QModelIndex &index) const;
    QVariant getFrameworkName(const QModelIndex &index) const;
    QVariant getModelArchitecture(const QModelIndex &index) const;
    QVariant getCtime(const QModelIndex &index) const;
    QVariant getMtime(const QModelIndex &index) const;
    static QString formatTimestamp(qint64 timestamp);

    dltool::database::ProjectDataBase *database_{nullptr};

    dltool::data::DataManager *data_manager_{nullptr};

    QSortFilterProxyModel *user_visible_model_{nullptr};
    QProcess               *tensorboard_process_{nullptr};
    QString                 tensorboard_model_uuid_;

    int method_{-1};

    QString project_dir_;

    std::vector<ModelRecord> models_;

    mutable std::unordered_map<std::string, std::unique_ptr<IModel>> model_instances_;

    mutable std::unordered_set<std::string> config_load_started_;
};

} // namespace dltool::model
