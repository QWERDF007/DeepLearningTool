#include "project/Dataset.h"

#include "data/DataBase.h"

#include <spdlog/spdlog.h>

namespace dltool::project {

Dataset::Dataset(const int64_t id, const QString &name, QObject *parent)
    : QObject(parent)
    , id_(id)
    , name_(name)
{
}

Dataset::~Dataset() {}

bool Dataset::setName(const QString &name)
{
    if (name_ == name)
        return false;
    name_ = name;
    return true;
}

DatasetsListModel::DatasetsListModel(data::ProjectDataBase *database, QObject *parent)
    : QAbstractListModel(parent)
    , database_(database)
{
    if (database_)
    {
        QString    err_msg;
        const auto datasets = database_->getAllDatasets(err_msg);
        if (!err_msg.isEmpty())
        {
            spdlog::error("查询所有数据集失败, error: {}", err_msg.toUtf8().constData());
        }
        else
        {
            for (const auto &[dataset_id, name] : datasets)
            {
                datasets_.emplace(dataset_id, new Dataset(dataset_id, name, this));
            }
        }
    }
}

DatasetsListModel::~DatasetsListModel() {}

int DatasetsListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(datasets_.size());
}

QVariant DatasetsListModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= rowCount())
        return QVariant();
    switch (role)
    {
    case DatasetIdRole:
        return getDatasetId(index);
    case NameRole:
        return getName(index);
    case StatsRole:
        return getStats(index);
    default:
        return QVariant();
    }
}

bool DatasetsListModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    return QAbstractListModel::setData(index, value, role);
}

QHash<int, QByteArray> DatasetsListModel::roleNames() const
{
    return {
        {DatasetIdRole, "dataset_id"},
        {     NameRole,       "name"},
        {    StatsRole,      "stats"},
    };
}

bool DatasetsListModel::insertRows(int row, int count, const QModelIndex &parent)
{
    if (count < 1 || row < 0 || row > rowCount(parent))
        return false;
    beginInsertRows(QModelIndex(), row, row + count - 1);
    // TODO
    endInsertRows();
    return true;
}

bool DatasetsListModel::removeRows(int row, int count, const QModelIndex &parent)
{
    if (count <= 0 || row < 0 || (row + count) > rowCount(parent))
        return false;
    beginRemoveRows(QModelIndex(), row, row + count - 1);
    // TODO
    endRemoveRows();
    return true;
}

bool DatasetsListModel::addDataset(const QString &name)
{
    if (database_ == nullptr)
    {
        spdlog::error("添加数据集失败: {}, 数据库未初始化", name.toUtf8().constData());
        return false;
    }
    QString err_msg;
    int64_t dataset_id{-1};
    bool    ok = database_->addDataset(name, dataset_id, err_msg);
    if (!ok)
    {
        spdlog::error("添加数据集失败: {}, error: {}", name.toUtf8().constData(), err_msg.toUtf8().constData());
        return false;
    }
    spdlog::info("添加数据集: {}", name.toUtf8().constData());
    const int row = rowCount();
    // const int count = 1;
    // beginInsertRows(QModelIndex(), row, row + count - 1);
    beginInsertRows(QModelIndex(), row, row);
    datasets_.emplace(dataset_id, new Dataset(dataset_id, name, this));
    endInsertRows();
    return true;
}

bool DatasetsListModel::updateDataset(const QString &old_name, const QString &new_name)
{
    if (database_ == nullptr)
    {
        spdlog::error("更新数据集失败: {}, 数据库未初始化", old_name.toUtf8().constData());
        return false;
    }
    QString err_msg;
    bool    ok = database_->updateDataset(old_name, new_name, err_msg);
    if (!ok)
    {
        spdlog::error("更新数据集失败: {}, error: {}", old_name.toUtf8().constData(), err_msg.toUtf8().constData());
        return false;
    }
    spdlog::info("更新数据集: {} -> {}", old_name.toUtf8().constData(), new_name.toUtf8().constData());
    int idx{0};
    for (const auto &[dataset_id, dataset] : datasets_)
    {
        if (dataset && dataset->name() == old_name)
        {
            dataset->setName(new_name);
            emit dataChanged(index(idx), index(idx), {NameRole});
            break;
        }
        ++idx;
    }
    return true;
}

bool DatasetsListModel::deleteDataset(const int64_t dataset_id)
{
    if (database_ == nullptr)
    {
        spdlog::error("删除数据集失败: {}, 数据库未初始化", dataset_id);
        return false;
    }

    QString err_msg;
    bool    ok = database_->deleteDataset(dataset_id, err_msg);
    if (!ok)
    {
        spdlog::error("删除数据集失败: {}, error: {}", dataset_id, err_msg.toUtf8().constData());
        return false;
    }
    spdlog::info("删除数据集: {}", dataset_id);
    int idx{0};
    for (const auto &[_, dataset] : datasets_)
    {
        if (dataset && dataset->id() == dataset_id)
        {
            beginRemoveRows(QModelIndex(), idx, idx);
            delete dataset;
            datasets_.erase(dataset_id);
            endRemoveRows();
            break;
        }
        ++idx;
    }
    return true;
}

QList<QString> DatasetsListModel::getDatasetsName() const
{
    QList<QString> names;
    for (const auto &[id, dataset] : datasets_)
    {
        names.append(dataset->name());
    }
    return names;
}

int DatasetsListModel::getDatasetId(const QModelIndex &index) const
{
    int idx = 0;
    for (const auto &[id, dataset] : datasets_)
    {
        if (index.row() == idx)
        {
            return id;
        }
        ++idx;
    }
    return -1;
}

QVariant DatasetsListModel::getName(const QModelIndex &index) const
{
    const int id = getDatasetId(index);
    if (id != -1)
        return datasets_.at(id)->name();
    return QVariant();
}

QVariant DatasetsListModel::getStats(const QModelIndex &index) const
{
    const int id = getDatasetId(index);
    if (id != -1)
        return "0/0";
    return QVariant();
}

} // namespace dltool::project
