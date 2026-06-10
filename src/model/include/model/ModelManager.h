#pragma once

#include "ModelExport.h"

#include <QAbstractListModel>
#include <QStringList>
#include <QtQml>
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
    explicit ModelManager(dltool::database::ProjectDataBase *database, QObject *parent = nullptr);
    ~ModelManager();

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

    Q_INVOKABLE QStringList supportedNetworkStructures() const;

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

    QVariant getModelId(const QModelIndex &index) const;
    QVariant getName(const QModelIndex &index) const;
    QVariant getNetworkStructure(const QModelIndex &index) const;
    QVariant getTrainingResult(const QModelIndex &index) const;
    QVariant getTestResult(const QModelIndex &index) const;
    QVariant getCtime(const QModelIndex &index) const;
    QVariant getMtime(const QModelIndex &index) const;

    dltool::database::ProjectDataBase *database_{nullptr};
    std::vector<ModelRecord> models_;
};

} // namespace dltool::model
