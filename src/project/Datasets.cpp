#include "project/Datasets.h"

#include "data/DataBase.h"

#include <spdlog/spdlog.h>

namespace dltool::project {

DatasetsListModel::DatasetsListModel(data::ProjectDataBase *database, QObject *parent)
    : QAbstractListModel(parent)
    , database_(database)
{
    init();
}

DatasetsListModel::~DatasetsListModel() {}

void DatasetsListModel::init()
{
    if (database_)
    {
        QString              err_msg;
        std::vector<int64_t> datasets_id;
        std::vector<QString> datasets_name;
        database_->getAllDatasets(datasets_id, datasets_name, err_msg);
        if (!err_msg.isEmpty())
        {
            spdlog::error("查询所有数据集失败, error: {}", err_msg.toUtf8().constData());
        }
        else
        {
            for (size_t i = 0; i < datasets_id.size(); ++i)
            {
                datasets_.emplace(datasets_id[i], new Dataset(datasets_id[i], datasets_name[i], this));
            }
        }
    }
    connect(this, &DatasetsListModel::statsChanged, this, &DatasetsListModel::onStatsChanged);
}

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
    const int row   = rowCount(); // 添加到队列尾部
    const int count = 1;
    beginInsertRows(QModelIndex(), row, row + count - 1);
    // beginInsertRows(QModelIndex(), row, row);
    datasets_.emplace(dataset_id, new Dataset(dataset_id, name, this));
    endInsertRows();
    return true;
}

bool DatasetsListModel::updateDataset(const int64_t dataset_id, const QString &name)
{
    if (database_ == nullptr)
    {
        spdlog::error("更新数据集失败: {}, 数据库未初始化", dataset_id);
        return false;
    }
    auto found = datasets_.find(dataset_id);
    if (found == datasets_.end())
    {
        spdlog::error("更新数据集失败: {}, 数据集不存在", dataset_id);
        return false;
    }
    if (found->second->name() == name)
        return true;
    QString err_msg;
    bool    ok = database_->updateDataset(dataset_id, name, err_msg);
    if (!ok)
    {
        spdlog::error("更新数据集失败: {}, error: {}", dataset_id, err_msg.toUtf8().constData());
        return false;
    }
    spdlog::info("更新数据集: {} -> {}", found->second->name().toUtf8().constData(), name.toUtf8().constData());
    int idx{0};
    for (const auto &[_, dataset] : datasets_)
    {
        if (dataset && dataset->id() == dataset_id)
        {
            dataset->setName(name);
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
    auto found = datasets_.find(dataset_id);
    if (found == datasets_.end())
    {
        spdlog::error("删除数据集失败: {}, 数据集不存在", dataset_id);
        return false;
    }
    spdlog::info("删除数据集: {}", found->second->name().toUtf8().constData());
    QString err_msg;
    bool    ok = database_->deleteDataset(dataset_id, err_msg);
    if (!ok)
    {
        spdlog::error("删除数据集失败: {}, error: {}", found->second->name().toUtf8().constData(),
                      err_msg.toUtf8().constData());
        return false;
    }
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

QList<QString> DatasetsListModel::getAllDatasetsName() const
{
    QList<QString> list;
    for (const auto &[id, dataset] : datasets_)
    {
        list.append(dataset->name());
    }
    return list;
}

int DatasetsListModel::getDatasetId(const QString &dataset_name) const
{
    for (const auto &[id, dataset] : datasets_)
    {
        if (dataset_name == dataset->name())
            return id;
    }
    return -1;
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

QString DatasetsListModel::getDatasetName(const int dataset_id) const
{
    auto found = datasets_.find(dataset_id);
    if (found == datasets_.end())
        return QString();
    return found->second->name();
}

void DatasetsListModel::addImages(const std::vector<int64_t> &dataset_ids, const std::vector<int64_t> &image_ids)
{
    if (dataset_ids.size() != image_ids.size())
    {
        spdlog::error("添加图像失败: 数据集id和图像id数量不一致");
        return;
    }
    std::map<int64_t, std::vector<int64_t>> datasets_image_ids;
    for (size_t i = 0; i < dataset_ids.size(); ++i)
    {
        const int64_t dataset_id = dataset_ids[i];
        if (datasets_image_ids.find(dataset_id) == datasets_image_ids.end())
        {
            datasets_image_ids[dataset_id] = std::vector<int64_t>();
            datasets_image_ids[dataset_id].reserve(image_ids.size());
        }
        datasets_image_ids[dataset_id].push_back(image_ids[i]);
    }

    for (const auto &[dataset_id, image_ids] : datasets_image_ids)
    {
        auto found = datasets_.find(dataset_id);
        if (found == datasets_.end())
        {
            spdlog::error("添加图像失败: 数据集不存在: {}", dataset_id);
            continue;
        }
        found->second->addImageIds(image_ids);
    }
    emit statsChanged();
}

void DatasetsListModel::deleteImages(const std::vector<int64_t> &dataset_ids, const std::vector<int64_t> &image_ids)
{
    if (dataset_ids.size() != image_ids.size())
    {
        spdlog::error("删除图像失败: 数据集id和图像id数量不一致");
        return;
    }
    std::map<int64_t, std::vector<int64_t>> datasets_image_ids;
    for (size_t i = 0; i < dataset_ids.size(); ++i)
    {
        const int64_t dataset_id = dataset_ids[i];
        if (datasets_image_ids.find(dataset_id) == datasets_image_ids.end())
        {
            datasets_image_ids[dataset_id] = std::vector<int64_t>();
            datasets_image_ids[dataset_id].reserve(image_ids.size());
        }
        datasets_image_ids[dataset_id].push_back(image_ids[i]);
    }
    for (const auto &[dataset_id, image_ids] : datasets_image_ids)
    {
        auto found = datasets_.find(dataset_id);
        if (found == datasets_.end())
        {
            spdlog::error("删除图像失败: 数据集不存在: {}", dataset_id);
            continue;
        }
        found->second->removeImageIds(image_ids);
    }
    emit statsChanged();
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
        return QString("%1/%2").arg(0).arg(database_->getImagesCount(id));
    return QVariant();
}

void DatasetsListModel::onStatsChanged()
{
    if (rowCount() > 0)
        emit dataChanged(index(0), index(rowCount() - 1), {StatsRole});
}

} // namespace dltool::project
