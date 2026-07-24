#include "data/Datasets.h"

#include "data/Images.h"
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
    addDatasetFromMemory(dataset_id, name);
    spdlog::info("添加数据集: {}", name.toUtf8().constData());
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

    addDatasetsFromMemory(dataset_ids, names);

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
    updateDatasetFromMemory(dataset_id, name);
    return true;
}

void DatasetsListModel::addDatasetFromMemory(const int64_t dataset_id, const QString &name)
{
    addDatasetsFromMemory({dataset_id}, {name});
}

void DatasetsListModel::addDatasetsFromMemory(const std::vector<int64_t> &dataset_ids,
                                              const std::vector<QString> &names)
{
    if (dataset_ids.empty() || dataset_ids.size() != names.size())
    {
        return;
    }

    std::vector<std::pair<int64_t, QString>> pending;
    pending.reserve(dataset_ids.size());
    for (size_t i = 0; i < dataset_ids.size(); ++i)
    {
        if (dataset_ids[i] >= 0 && !names[i].isEmpty() && datasets_.find(dataset_ids[i]) == datasets_.end())
        {
            pending.emplace_back(dataset_ids[i], names[i]);
        }
    }
    if (pending.empty())
    {
        return;
    }

    const int row = rowCount();
    beginInsertRows(QModelIndex(), row, row + static_cast<int>(pending.size()) - 1);
    for (const auto &[dataset_id, name] : pending)
    {
        datasets_.emplace(dataset_id, new Dataset(dataset_id, name, this));
    }
    endInsertRows();
}

void DatasetsListModel::updateDatasetFromMemory(const int64_t dataset_id, const QString &name)
{
    const auto found = datasets_.find(dataset_id);
    if (found == datasets_.end() || found->second == nullptr || found->second->name() == name)
    {
        return;
    }

    int row = 0;
    for (const auto &[id, dataset] : datasets_)
    {
        if (id == dataset_id)
        {
            dataset->setName(name);
            emit dataChanged(index(row), index(row), {NameRole});
            return;
        }
        ++row;
    }
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

void DatasetsListModel::removeDatasetsFromMemory(const std::vector<int64_t> &dataset_ids)
{
    if (dataset_ids.empty())
    {
        return;
    }

    const std::set<int64_t> deleted_dataset_ids(dataset_ids.begin(), dataset_ids.end());
    bool                    contains_deleted_dataset = false;
    for (const int64_t dataset_id : deleted_dataset_ids)
    {
        if (datasets_.find(dataset_id) != datasets_.end())
        {
            contains_deleted_dataset = true;
            break;
        }
    }
    if (!contains_deleted_dataset)
    {
        return;
    }

    beginResetModel();
    for (const int64_t dataset_id : deleted_dataset_ids)
    {
        const auto found = datasets_.find(dataset_id);
        if (found == datasets_.end())
        {
            continue;
        }
        delete found->second;
        datasets_.erase(found);
    }
    endResetModel();

    if (selection_ != nullptr)
    {
        selection_->clear();
    }
    setLastIndex(-1);
    emit statsChanged();
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

void DatasetsListModel::addImagesFromSource(const ImageInstancesListModel *images,
                                            const std::vector<int64_t> &image_ids)
{
    if (images == nullptr)
    {
        return;
    }

    bool changed = false;
    for (const int64_t image_id : image_ids)
    {
        const ImageInstance *image = images->getImageInstance(image_id);
        if (image == nullptr)
        {
            continue;
        }
        const auto dataset = datasets_.find(image->datasetId());
        if (dataset == datasets_.end() || !dataset->second->addImageId(image_id))
        {
            continue;
        }
        dataset->second->setImageLabelled(image_id, image->hasLabels());
        changed = true;
    }
    if (changed)
    {
        emit statsChanged();
    }
}

void DatasetsListModel::addImagesFromSource(const ImageInstancesListModel *images,
                                            const std::vector<LoadedImageInstance> &loaded_images)
{
    if (images == nullptr)
    {
        return;
    }

    bool changed = false;
    for (const LoadedImageInstance &loaded : loaded_images)
    {
        const ImageInstance *image = images->getImageInstance(loaded.image_id);
        if (image == nullptr)
        {
            continue;
        }
        const auto dataset = datasets_.find(image->datasetId());
        if (dataset == datasets_.end() || !dataset->second->addImageId(loaded.image_id))
        {
            continue;
        }
        dataset->second->setImageLabelled(loaded.image_id, image->hasLabels());
        changed = true;
    }
    if (changed)
    {
        emit statsChanged();
    }
}

void DatasetsListModel::removeImagesFromSource(const ImageInstancesListModel *images,
                                               const std::vector<int64_t> &image_ids)
{
    if (images == nullptr)
    {
        return;
    }

    bool changed = false;
    for (const int64_t image_id : image_ids)
    {
        const ImageInstance *image = images->getImageInstance(image_id);
        if (image == nullptr)
        {
            continue;
        }
        const auto dataset = datasets_.find(image->datasetId());
        if (dataset != datasets_.end())
        {
            changed |= dataset->second->removeImageId(image_id);
        }
    }
    if (changed)
    {
        emit statsChanged();
    }
}

void DatasetsListModel::moveImagesFromSource(const ImageInstancesListModel *images,
                                             const std::vector<int64_t> &image_ids,
                                             const int64_t target_dataset_id)
{
    if (images == nullptr)
    {
        return;
    }

    const auto target = datasets_.find(target_dataset_id);
    if (target == datasets_.end())
    {
        spdlog::error("移动图像失败: 目标数据集不存在: {}", target_dataset_id);
        return;
    }

    bool changed = false;
    for (const int64_t image_id : image_ids)
    {
        const ImageInstance *image = images->getImageInstance(image_id);
        if (image == nullptr || image->datasetId() == target_dataset_id)
        {
            continue;
        }
        const auto source = datasets_.find(image->datasetId());
        if (source == datasets_.end())
        {
            continue;
        }

        const bool labelled = image->hasLabels();
        source->second->removeImageId(image_id);
        if (target->second->addImageId(image_id))
        {
            target->second->setImageLabelled(image_id, labelled);
        }
        changed = true;
    }
    if (changed)
    {
        emit statsChanged();
    }
}

void DatasetsListModel::syncImageLabelState(const ImageInstancesListModel *images,
                                            const std::vector<int64_t> &image_ids)
{
    if (images == nullptr)
    {
        return;
    }

    bool changed = false;
    for (const int64_t image_id : image_ids)
    {
        const ImageInstance *image = images->getImageInstance(image_id);
        if (image == nullptr)
        {
            continue;
        }
        const auto dataset = datasets_.find(image->datasetId());
        if (dataset != datasets_.end())
        {
            changed |= dataset->second->setImageLabelled(image_id, image->hasLabels());
        }
    }
    if (changed)
    {
        emit statsChanged();
    }
}

void DatasetsListModel::rebuildImageStats(const ImageInstancesListModel *images)
{
    if (images == nullptr)
    {
        return;
    }

    for (auto &[_, dataset] : datasets_)
    {
        dataset->clearImages();
    }
    // 图像模型可能正在批量发布：新图像已经存在于完整内存索引中，
    // 但尚未进入 QAbstractListModel 的可见行列表。统计必须遍历完整实体，
    // 否则导入结束前重建统计会得到 0/0，后续标注同步也无法命中图像。
    for (const auto &[image_id, image] : images->getAllImageInstances())
    {
        if (image == nullptr)
        {
            continue;
        }
        const auto dataset = datasets_.find(image->datasetId());
        if (dataset == datasets_.end())
        {
            continue;
        }
        dataset->second->addImageId(image_id);
        dataset->second->setImageLabelled(image_id, image->hasLabels());
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
        const Dataset *dataset = datasets_.at(dataset_id);
        return QString("%1/%2").arg(dataset->labelledImageCount()).arg(dataset->imageIds().size());
    }
    return QVariant();
}

QVariant DatasetsListModel::getProgress(const QModelIndex &index) const
{
    const int dataset_id = getDatasetId(index);
    if (dataset_id != -1)
    {
        const Dataset *dataset = datasets_.at(dataset_id);
        return dataset->labelledImageCount() / (dataset->imageIds().size() + 1e-9);
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
