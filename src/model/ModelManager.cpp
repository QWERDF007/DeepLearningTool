#include "model/ModelManager.h"

#include "database/DataBase.h"

#include <spdlog/spdlog.h>

#include <QDateTime>

namespace dltool::model {

ModelManager::ModelManager(dltool::database::ProjectDataBase *database, QObject *parent)
    : QAbstractListModel(parent)
    , database_(database)
{
    init();
}

ModelManager::~ModelManager() {}

void ModelManager::init()
{
    beginResetModel();
    models_.clear();

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
    std::vector<qint64> ctimes;
    std::vector<qint64> mtimes;
    QString err_msg;

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
        {          ModelIdRole,          "model_id"},
        {             NameRole,              "name"},
        { NetworkStructureRole, "network_structure"},
        {   TrainingResultRole,   "training_result"},
        {       TestResultRole,       "test_result"},
        {            CtimeRole,             "ctime"},
        {            MtimeRole,             "mtime"},
    };
}

bool ModelManager::addModel(const QString &name, const QString &network_structure)
{
    const QString trimmed_name = name.trimmed();
    const QString trimmed_network_structure = network_structure.trimmed();
    if (trimmed_name.isEmpty() || trimmed_network_structure.isEmpty())
    {
        spdlog::warn("add model failed: model name or network structure is empty");
        return false;
    }

    if (database_ == nullptr)
    {
        spdlog::error("add model failed: database is null");
        return false;
    }

    QString err_msg;
    int64_t model_id{-1};
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const bool ok = database_->addModel(trimmed_name, trimmed_network_structure, now, now, model_id, err_msg);
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

QStringList ModelManager::supportedNetworkStructures() const
{
    return {
        QStringLiteral("LeNet"),
        QStringLiteral("AlexNet"),
        QStringLiteral("VGG16"),
        QStringLiteral("ResNet18"),
        QStringLiteral("ResNet50"),
        QStringLiteral("MobileNetV2"),
        QStringLiteral("EfficientNet-B0"),
        QStringLiteral("YOLOv5"),
        QStringLiteral("RF-DETR"),
    };
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
    return value.isEmpty() ? QStringLiteral("\u672a\u8bad\u7ec3") : value;
}

QVariant ModelManager::getTestResult(const QModelIndex &index) const
{
    const QString &value = models_.at(index.row()).test_result;
    return value.isEmpty() ? QStringLiteral("\u672a\u6d4b\u8bd5") : value;
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
