#pragma once

#include "IModel.h"
#include "ModelExport.h"

#include <QAbstractListModel>
#include <QStringList>
#include <QtQml>
#include <functional>
#include <memory>
#include <vector>

namespace dltool::database {
class ProjectDataBase;
} // namespace dltool::database

namespace dltool::model {

class MODEL_API ModelManager : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ModelManager)
    QML_UNCREATABLE("Can not create ModelManager directly!")
public:
    using ModelFactory = std::function<std::unique_ptr<IModel>()>;

    explicit ModelManager(const int method, dltool::database::ProjectDataBase *database, QObject *parent = nullptr);
    ~ModelManager();

    Q_PROPERTY(int method READ method CONSTANT FINAL)

    enum Role
    {
        ModelIdRole = Qt::UserRole + 1,
        NameRole,
        NetworkStructureRole,
        TrainingResultRole,
        TestResultRole,
        CtimeRole,
        MtimeRole,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE bool addModel(const QString &name, const QString &network_structure);
    Q_INVOKABLE bool renameModel(const qint64 model_id, const QString &name);
    Q_INVOKABLE bool deleteModel(const qint64 model_id);
    Q_INVOKABLE bool copyModel(const qint64 model_id);

    Q_INVOKABLE QStringList supportedNetworkStructures() const;

    Q_INVOKABLE QStringList availableModelNames() const;

    int method() const
    {
        return method_;
    }

    static bool registerModel(const int method, const QString &type_name, ModelFactory factory);
    static bool registerModel(const QString &type_name, ModelFactory factory);
    static QStringList registeredModelNames(const int method);
    static QStringList registeredModelNames();
    static std::unique_ptr<IModel> createRegisteredModel(const int method, const QString &type_name);
    static std::unique_ptr<IModel> createRegisteredModel(const QString &type_name);
    static std::vector<std::unique_ptr<IModel>> registeredModels(const int method);
    static std::vector<std::unique_ptr<IModel>> registeredModels();

    std::unique_ptr<IModel> createRegisteredModelInstance(const QString &type_name) const;
    std::vector<std::unique_ptr<IModel>> registeredModelInstances() const;

private:
    struct ModelRecord
    {
        int64_t model_id{-1};
        QString name;
        QString network_structure;
        QString training_result;
        QString test_result;
        qint64 ctime{0};
        qint64 mtime{0};
    };

    void init();
    int indexOfModel(const int64_t model_id) const;
    QString uniqueCopyName(const QString &name) const;

    QVariant getModelId(const QModelIndex &index) const;
    QVariant getName(const QModelIndex &index) const;
    QVariant getNetworkStructure(const QModelIndex &index) const;
    QVariant getTrainingResult(const QModelIndex &index) const;
    QVariant getTestResult(const QModelIndex &index) const;
    QVariant getCtime(const QModelIndex &index) const;
    QVariant getMtime(const QModelIndex &index) const;

    dltool::database::ProjectDataBase *database_{nullptr};
    int method_{-1};
    std::vector<ModelRecord> models_;
};

} // namespace dltool::model
