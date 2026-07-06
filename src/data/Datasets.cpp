#include "data/Datasets.h"

#include "database/DataBase.h"

#include <spdlog/spdlog.h>

#include <algorithm>

namespace dltool::data {

DatasetsListModel::DatasetsListModel(dltool::database::ProjectDataBase *database, QObject *parent)
    : QAbstractListModel(parent)
    , database_(database)
    , selection_(new QItemSelectionModel(this))
{
    init();
}

DatasetsListModel::~DatasetsListModel() {}

void DatasetsListModel::init()
{
    connect(selection_, &QItemSelectionModel::selectionChanged, this, &DatasetsListModel::updateSelection);
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
    case ProgressRole:
        return getProgress(index);
    case SelectedRole:
        return getSelected(index);
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
        { ProgressRole,   "progress"},
        { SelectedRole,   "selected"},
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

bool DatasetsListModel::addDatasets(const std::vector<QString> &names, std::vector<int64_t> &dataset_ids)
{
    dataset_ids.clear();
    if (database_ == nullptr)
    {
        spdlog::error("批量添加数据集失败: 数据库未初始化");
        return false;
    }
    if (names.empty())
    {
        return true;
    }

    QString err_msg;
    bool    ok = database_->addDatasets(names, dataset_ids, err_msg);
    if (!ok)
    {
        spdlog::error("批量添加数据集失败, 数量: {}, error: {}", names.size(), err_msg.toUtf8().constData());
        return false;
    }
    if (dataset_ids.size() != names.size())
    {
        spdlog::error("批量添加数据集失败: 返回的数据集 ID 数量不一致");
        dataset_ids.clear();
        return false;
    }

    const int row   = rowCount();
    const int count = static_cast<int>(dataset_ids.size());
    beginInsertRows(QModelIndex(), row, row + count - 1);
    for (size_t i = 0; i < dataset_ids.size(); ++i)
    {
        datasets_.emplace(dataset_ids[i], new Dataset(dataset_ids[i], names[i], this));
    }
    endInsertRows();

    spdlog::info("批量添加数据集, 数量: {}", dataset_ids.size());
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
            if (last_index_ >= rowCount())
            {
                setLastIndex(rowCount() - 1);
            }
            break;
        }
        ++idx;
    }
    return true;
}

void DatasetsListModel::shiftSelect(int current_index, int previous_index, QItemSelectionModel::SelectionFlags command)
{
    if (rowCount() <= 0)
        return;

    const int top    = std::max(0, std::min(current_index, previous_index));
    const int bottom = std::min(rowCount() - 1, std::max(current_index, previous_index));
    if (top > bottom)
        return;

    QItemSelection selection;
    selection.select(index(top), index(bottom));
    selection_->select(selection, command);
}

void DatasetsListModel::selectAll()
{
    if (rowCount() <= 0)
        return;

    QItemSelection selection;
    selection.select(index(0), index(rowCount() - 1));
    selection_->select(selection, QItemSelectionModel::Select);
}

std::vector<int64_t> DatasetsListModel::getSelectedDatasetIds() const
{
    const QModelIndexList selected_indexes = selection_->selectedIndexes();

    std::vector<int64_t> dataset_ids;
    dataset_ids.reserve(selected_indexes.size());
    for (const QModelIndex &selected_index : selected_indexes)
    {
        const int dataset_id = getDatasetId(selected_index);
        if (dataset_id >= 0)
        {
            dataset_ids.push_back(dataset_id);
        }
    }
    return dataset_ids;
}

void DatasetsListModel::setLastIndex(int last_index)
{
    if (last_index_ == last_index)
        return;

    last_index_ = last_index;
    emit lastSelectedIndexChanged();
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

std::vector<int64_t> DatasetsListModel::getAllDatasetIds() const
{
    std::vector<int64_t> ids;
    ids.reserve(datasets_.size());
    for (const auto &[id, dataset] : datasets_)
    {
        ids.push_back(id);
    }
    return ids;
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

    for (const auto &[dataset_id, dataset_image_ids] : datasets_image_ids)
    {
        auto found = datasets_.find(dataset_id);
        if (found == datasets_.end())
        {
            spdlog::error("添加图像失败: 数据集不存在: {}", dataset_id);
            continue;
        }
        if (labelled_image_stats_.find(dataset_id) == labelled_image_stats_.end())
        {
            labelled_image_stats_[dataset_id] = 0;
        }
        found->second->addImageIds(dataset_image_ids);
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
    for (const auto &[dataset_id, dataset_image_ids] : datasets_image_ids)
    {
        auto found = datasets_.find(dataset_id);
        if (found == datasets_.end())
        {
            spdlog::error("删除图像失败: 数据集不存在: {}", dataset_id);
            continue;
        }
        found->second->removeImageIds(dataset_image_ids);
    }
    emit statsChanged();
}

void DatasetsListModel::moveImages(const std::vector<int64_t>              &source_dataset_ids,
                                   const std::vector<int64_t>              &target_dataset_ids,
                                   const std::vector<int64_t>              &image_ids,
                                   const std::vector<std::vector<int64_t>> &images_label_ids,
                                   const std::vector<int64_t>              &image_label_class_ids)
{
    if (source_dataset_ids.size() != image_ids.size() || target_dataset_ids.size() != image_ids.size()
        || images_label_ids.size() != image_ids.size() || image_label_class_ids.size() != image_ids.size())
    {
        spdlog::error("移动图像失败: 数据集id、图像id和标签数量不一致");
        return;
    }

    std::map<int64_t, std::vector<int64_t>> source_images;
    std::map<int64_t, std::vector<int64_t>> target_images;
    for (size_t i = 0; i < image_ids.size(); ++i)
    {
        if (labelled_image_stats_.find(source_dataset_ids[i]) == labelled_image_stats_.end())
        {
            labelled_image_stats_[source_dataset_ids[i]] = 0;
        }
        if (labelled_image_stats_.find(target_dataset_ids[i]) == labelled_image_stats_.end())
        {
            labelled_image_stats_[target_dataset_ids[i]] = 0;
        }

        source_images[source_dataset_ids[i]].push_back(image_ids[i]);
        target_images[target_dataset_ids[i]].push_back(image_ids[i]);

        if (!images_label_ids[i].empty() || image_label_class_ids[i] >= 0)
        {
            auto source_stats = labelled_image_stats_.find(source_dataset_ids[i]);
            if (source_stats != labelled_image_stats_.end() && source_stats->second > 0)
            {
                --source_stats->second;
            }
            ++labelled_image_stats_[target_dataset_ids[i]];
        }
    }

    for (const auto &[dataset_id, dataset_image_ids] : source_images)
    {
        auto found = datasets_.find(dataset_id);
        if (found == datasets_.end())
        {
            spdlog::error("移动图像失败: 源数据集不存在: {}", dataset_id);
            continue;
        }
        found->second->removeImageIds(dataset_image_ids);
    }

    for (const auto &[dataset_id, dataset_image_ids] : target_images)
    {
        auto found = datasets_.find(dataset_id);
        if (found == datasets_.end())
        {
            spdlog::error("移动图像失败: 目标数据集不存在: {}", dataset_id);
            continue;
        }
        found->second->addImageIds(dataset_image_ids);
    }

    emit statsChanged();
}

void DatasetsListModel::setStats(const std::vector<int64_t> &dataset_ids, const std::vector<int64_t> &image_ids,
                                 const std::vector<std::vector<int64_t>> &images_label_ids,
                                 const std::vector<int64_t> &image_label_class_ids)
{
    if (dataset_ids.size() != image_ids.size() || images_label_ids.size() != image_ids.size()
        || image_label_class_ids.size() != image_ids.size())
    {
        spdlog::error("更新数据集标注统计失败: 数据集id、图像id和标签数量不一致");
        return;
    }

    labelled_image_stats_.clear();
    for (size_t i = 0; i < dataset_ids.size(); ++i)
    {
        const int64_t dataset_id = dataset_ids[i];
        const bool    has_label  = !images_label_ids[i].empty() || image_label_class_ids[i] >= 0;

        auto found = labelled_image_stats_.find(dataset_id);
        if (found == labelled_image_stats_.end())
        {
            labelled_image_stats_[dataset_id] = 0;
        }
        if (!has_label)
            continue;
        labelled_image_stats_[dataset_id] += 1;
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
    const int dataset_id = getDatasetId(index);
    if (dataset_id != -1)
    {
        auto found = labelled_image_stats_.find(dataset_id);
        if (found == labelled_image_stats_.end())
            return QString("%1/%2").arg(0).arg(datasets_.at(dataset_id)->imageIds().size());
        return QString("%1/%2").arg(found->second).arg(datasets_.at(dataset_id)->imageIds().size());
    }
    return QVariant();
}

QVariant DatasetsListModel::getProgress(const QModelIndex &index) const
{
    const int dataset_id = getDatasetId(index);
    if (dataset_id != -1)
    {
        auto found = labelled_image_stats_.find(dataset_id);
        if (found == labelled_image_stats_.end())
            return 0;
        return found->second / (datasets_.at(dataset_id)->imageIds().size() + 1e-9);
    }
    return QVariant();
}

QVariant DatasetsListModel::getSelected(const QModelIndex &index) const
{
    return selection_ && index.isValid() && selection_->isSelected(index);
}

void DatasetsListModel::onStatsChanged()
{
    if (rowCount() > 0)
        emit dataChanged(index(0), index(rowCount() - 1), {StatsRole, ProgressRole});
}

void DatasetsListModel::updateSelection(const QItemSelection &selected, const QItemSelection &deselected)
{
    const auto emitSelectionChanged = [this](const QItemSelection &selection)
    {
        const QModelIndexList items = selection.indexes();
        int                   top{-1};
        int                   bottom{-1};
        for (const QModelIndex &index : items)
        {
            const int row = index.row();
            if (row < 0 || row >= rowCount())
                continue;
            if (top == -1)
                top = row;
            else
                top = std::min(top, row);
            bottom = std::max(bottom, row);
        }
        if (top >= 0 && bottom >= top)
            emit dataChanged(index(top), index(bottom), {SelectedRole});
    };

    emitSelectionChanged(deselected);
    emitSelectionChanged(selected);
}

} // namespace dltool::data
