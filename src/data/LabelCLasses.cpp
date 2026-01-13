#include "data/LabelClasses.h"

#include "data/DataBase.h"

#include <spdlog/spdlog.h>

namespace dltool::data {

LabelClass::LabelClass(const int64_t id, const QString &name, const QString &color, const QString &shortcut,
                       const int64_t ordinal_index, QObject *parent)
    : QObject(parent)
    , id_(id)
    , ordinal_index_(ordinal_index)
    , name_(name)
    , color_(color)
    , shortcut_(shortcut)

{
}

LabelClass::~LabelClass() {}

bool LabelClass::setName(const QString &name)
{
    if (name_ == name)
        return false;
    name_ = name;
    return true;
}

bool LabelClassesListModel::reorderLabelClass(const int64_t label_class_id, const int64_t new_ordinal_index)
{
    if (new_ordinal_index < 0 || new_ordinal_index >= rowCount())
        return false;

    // 找到当前项的 ordinal_index
    int64_t current_ordinal{-1};
    for (const auto &[id, label_class] : label_classes_)
    {
        if (label_class->id() == label_class_id)
        {
            current_ordinal = label_class->ordinalIndex();
            break;
        }
    }
    if (current_ordinal == -1)
        return false;
    if (current_ordinal == new_ordinal_index)
        return true;

    const int from = static_cast<int>(current_ordinal);
    const int to   = static_cast<int>(new_ordinal_index);

    // 计算所有需要更新的 label_class_id 和对应的新 ordinal_index
    std::vector<int64_t> ids_to_update;
    std::vector<int64_t> new_ordinals;

    for (const auto &[id, label_class] : label_classes_)
    {
        int64_t ord = label_class->ordinalIndex();
        if (id == label_class_id)
        {
            ids_to_update.push_back(id);
            new_ordinals.push_back(new_ordinal_index);
        }
        else if (from < to)
        {
            // 向下移动：区间 (from, to] 的项 ordinal_index 全部减 1
            if (ord > from && ord <= to)
            {
                ids_to_update.push_back(id);
                new_ordinals.push_back(ord - 1);
            }
        }
        else if (from > to)
        {
            // 向上移动：区间 [to, from) 的项 ordinal_index 全部加 1
            if (ord >= to && ord < from)
            {
                ids_to_update.push_back(id);
                new_ordinals.push_back(ord + 1);
            }
        }
    }

    // 保存当前选中的 label_class_id
    int64_t     selectedClassId = -1;
    QModelIndex currentIdx      = selection_->currentIndex();
    if (currentIdx.isValid())
    {
        selectedClassId = getLabelClassId(currentIdx);
    }

    // 批量更新数据库（这会同时更新内存中的 ordinalIndex）
    if (!updateLabelClass(ids_to_update, new_ordinals))
        return false;

    // 计算受影响的行范围
    int minRow = std::min(from, to);
    int maxRow = std::max(from, to);

    // 通知视图所有受影响行的数据已更改
    // 注意：由于 ordinalIndex 改变后，行与数据的映射关系改变了
    // 需要通知整个受影响范围的所有角色
    emit dataChanged(index(minRow), index(maxRow),
                     {LabelClassIdRole, NameRole, ColorRole, ShortcutRole, OrdinalIndexRole, SelectedRole});

    // 恢复选中状态
    if (selectedClassId != -1)
    {
        for (const auto &[id, label_class] : label_classes_)
        {
            if (id == selectedClassId)
            {
                int         newRow   = static_cast<int>(label_class->ordinalIndex());
                QModelIndex newIndex = index(newRow);
                selection_->select(newIndex, QItemSelectionModel::ClearAndSelect);
                selection_->setCurrentIndex(newIndex, QItemSelectionModel::Select);
                break;
            }
        }
    }

    return true;
}

bool LabelClass::setColor(const QString &color)
{
    if (color_ == color)
        return false;
    color_ = color;
    return true;
}

bool LabelClass::setShortcut(const QString &shortcut)
{
    if (shortcut_ == shortcut)
        return false;
    shortcut_ = shortcut;
    return true;
}

bool LabelClass::setOrdinalIndex(const int64_t ordinal_index)
{
    if (ordinal_index_ == ordinal_index)
        return false;
    ordinal_index_ = ordinal_index;
    return true;
}

LabelClassesListModel::LabelClassesListModel(data::ProjectDataBase *database, QObject *parent)
    : QAbstractListModel(parent)
    , database_(database)
    , selection_(new QItemSelectionModel(this))
{
    init();
}

LabelClassesListModel::~LabelClassesListModel() {}

void LabelClassesListModel::init()
{
    connect(selection_, &QItemSelectionModel::selectionChanged, this, &LabelClassesListModel::updateSelection);
    connect(selection_, &QItemSelectionModel::currentChanged, this, &LabelClassesListModel::currentLabelClassChanged);

    if (database_ == nullptr)
    {
        return;
    }
    QString              err_msg;
    std::vector<int64_t> label_class_ids, ordinal_indices;
    std::vector<QString> names, colors, shortcuts;
    if (database_->getAllLabelClasses(label_class_ids, names, colors, shortcuts, ordinal_indices, err_msg))
    {
        for (size_t i = 0; i < label_class_ids.size(); ++i)
        {
            label_classes_.emplace(label_class_ids[i], new LabelClass(label_class_ids[i], names[i], colors[i],
                                                                      shortcuts[i], ordinal_indices[i], this));
        }
        if (label_class_ids.size() > 0)
        {
            selection_->select(index(0), QItemSelectionModel::ClearAndSelect);
            selection_->setCurrentIndex(index(0), QItemSelectionModel::Select);
        }
    }
    else
    {
        spdlog::error("查询所有标签类别失败: {}", err_msg.toUtf8().constData());
    }
}

int LabelClassesListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(label_classes_.size());
}

QVariant LabelClassesListModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= rowCount())
        return QVariant();
    switch (role)
    {
    case LabelClassIdRole:
        return getLabelClassId(index);
    case NameRole:
        return getLabelClassName(index);
    case ColorRole:
        return getLabelClassColor(index);
    case ShortcutRole:
        return getLabelClassShortcut(index);
    case SelectedRole:
        return getLabelClassSelected(index);
    case OrdinalIndexRole:
        return getLabelClassOrdinalIndex(index);
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> LabelClassesListModel::roleNames() const
{
    return {
        {LabelClassIdRole, "label_class_id"},
        {        NameRole,           "name"},
        {       ColorRole,          "color"},
        {    ShortcutRole,       "shortcut"},
        {OrdinalIndexRole,  "ordinal_index"},
        {    SelectedRole,       "selected"}
    };
}

bool LabelClassesListModel::addLabelClass(const QString &name, const QString &color, const QString &shortcut)
{
    if (database_ == nullptr)
    {
        spdlog::error("添加标签类别 [{}] 失败, 数据库未初始化", name.toUtf8().constData());
        return false;
    }
    const int row = static_cast<int>(label_classes_.size());
    QString   err_msg;
    int64_t   label_class_id{-1};
    bool      ok = database_->addLabelClass(name, color, shortcut, row, label_class_id, err_msg);
    if (!ok)
    {
        spdlog::error("添加标签类别 [{}] 失败: {}", name.toUtf8().constData(), err_msg.toUtf8().constData());
        return false;
    }
    // 添加到队列尾部
    const int count = 1;
    beginInsertRows(QModelIndex(), row, row + count - 1);
    // beginInsertRows(QModelIndex(), row, row);
    label_classes_.emplace(label_class_id, new LabelClass(label_class_id, name, color, shortcut, row, this));
    endInsertRows();
    QModelIndex index = this->index(row);
    selection_->select(index, QItemSelectionModel::ClearAndSelect);
    selection_->setCurrentIndex(index, QItemSelectionModel::Select);
    spdlog::info("添加标签类别 [{}] 成功", name.toUtf8().constData());
    return true;
}

bool LabelClassesListModel::updateLabelClass(const int64_t label_class_id, const QString &name, const QString &color,
                                             const QString &shortcut, const int64_t ordinal_index)
{
    if (database_ == nullptr)
    {
        spdlog::error("更新标签类别 [{}] 失败, 数据库未初始化", name.toUtf8().constData());
        return false;
    }
    QString err_msg;
    bool    ok = database_->updateLabelClass(label_class_id, name, color, shortcut, ordinal_index, err_msg);
    if (!ok)
    {
        spdlog::error("更新标签类别 [{}] 失败: {}", name.toUtf8().constData(), err_msg.toUtf8().constData());
        return false;
    }

    int idx{0};
    for (const auto &[_, label_class] : label_classes_)
    {
        if (label_class->id() == label_class_id)
        {
            label_class->setName(name);
            label_class->setColor(color);
            label_class->setShortcut(shortcut);
            label_class->setOrdinalIndex(ordinal_index);
            emit dataChanged(index(idx), index(idx), {NameRole, ColorRole, ShortcutRole, OrdinalIndexRole});
            break;
        }
        ++idx;
    }
    LabelClass *it = label_classes_[label_class_id];
    spdlog::info("更新标签类别 {} -> {}, {} -> {}, {} -> {}, {} -> {} 成功", it->name().toUtf8().constData(),
                 name.toUtf8().constData(), it->color().toUtf8().constData(), color.toUtf8().constData(),
                 it->shortcut().toUtf8().constData(), shortcut.toUtf8().constData(), it->ordinalIndex(), ordinal_index);
    return true;
}

bool dltool::data::LabelClassesListModel::updateLabelClass(const std::vector<int64_t> &label_class_ids,
                                                           const std::vector<int64_t> &ordinal_indexes)
{
    if (database_ == nullptr)
    {
        spdlog::error("更新 [{}] 个标签类别序号索引失败, 数据库未初始化", label_class_ids.size());
        return false;
    }
    QString err_msg;
    bool    ok = database_->updateLabelClass(label_class_ids, ordinal_indexes, err_msg);
    if (!ok)
    {
        spdlog::error("更新 [{}] 个标签类别序号索引失败: {}", label_class_ids.size(), err_msg.toUtf8().constData());
        return false;
    }
    for (size_t i = 0; i < label_class_ids.size(); ++i)
    {
        auto found = label_classes_.find(label_class_ids[i]);
        if (found == label_classes_.end())
            continue;
        found->second->setOrdinalIndex(ordinal_indexes[i]);
    }
    return true;
}

bool LabelClassesListModel::deleteLabelClass(const int64_t label_class_id)
{
    if (database_ == nullptr)
    {
        spdlog::error("删除标签类别 [{}] 失败, 数据库未初始化", label_class_id);
        return false;
    }
    QString name = getLabelClassName(label_class_id);
    QString err_msg;
    bool    ok = database_->deleteLabelClass(label_class_id, err_msg);
    if (!ok)
    {
        spdlog::error("删除标签类别 [{}] 失败: {}", name.toUtf8().constData(), err_msg.toUtf8().constData());
        return false;
    }
    int idx{0};
    for (const auto &[_, label_class] : label_classes_)
    {
        if (label_class->id() == label_class_id)
        {
            beginRemoveRows(QModelIndex(), idx, idx);
            label_classes_.erase(label_class_id);
            endRemoveRows();
            break;
        }
        ++idx;
    }
    spdlog::info("删除标签类别 [{}] 成功", name.toUtf8().constData());
    return true;
}

int LabelClassesListModel::getLabelClassId(const QString &name) const
{
    for (const auto &[label_class_id, label_class] : label_classes_)
    {
        if (label_class->name() == name)
            return label_class_id;
    }
    return -1;
}

int LabelClassesListModel::getLabelClassId(const QModelIndex &index) const
{
    for (const auto &[id, label_class] : label_classes_)
    {
        if (index.row() == static_cast<int>(label_class->ordinalIndex()))
        {
            return id;
        }
    }
    return -1;
}

QString LabelClassesListModel::getLabelClassName(const int label_class_id) const
{
    auto found = label_classes_.find(label_class_id);
    if (found != label_classes_.end())
        return found->second->name();
    return QString();
}

QString LabelClassesListModel::getLabelClassColor(const int label_class_id) const
{
    auto found = label_classes_.find(label_class_id);
    if (found != label_classes_.end())
        return found->second->color();
    return QString();
}

int LabelClassesListModel::getCurrentLabelClassId() const
{
    QModelIndex index = selection_->currentIndex();
    if (index.row() < 0 || index.row() >= rowCount())
        return -1;
    return getLabelClassId(index);
}

QString LabelClassesListModel::getCurrentLabelClassColor() const
{
    QModelIndex index = selection_->currentIndex();
    if (index.row() < 0 || index.row() >= rowCount())
        return "red";
    return getLabelClassColor(index).toString();
}

QString LabelClassesListModel::isValid(const int label_class_id, const QString &name, const QString &shortcut,
                                       const int ordinal_index) const
{
    if (ordinal_index > static_cast<int>(label_classes_.size()) - 1)
        return "标签序号索引超出范围";
    for (const auto &[_, label_class] : label_classes_)
    {
        if (label_class->id() == label_class_id)
            continue;
        if (label_class->name() == name)
            return "标签名称已存在";
        if (!shortcut.isEmpty() && label_class->shortcut() == shortcut)
            return "标签快捷键已存在";
        if (label_class->ordinalIndex() == ordinal_index)
            return "标签序号索引已存在";
    }
    return QString();
}

QVariant LabelClassesListModel::getLabelClassName(const QModelIndex &index) const
{
    const int id = getLabelClassId(index);
    if (id != -1)
        return label_classes_.at(id)->name();
    return QString();
}

QVariant LabelClassesListModel::getLabelClassColor(const QModelIndex &index) const
{
    const int id = getLabelClassId(index);
    if (id != -1)
        return label_classes_.at(id)->color();
    return QString();
}

QVariant LabelClassesListModel::getLabelClassShortcut(const QModelIndex &index) const
{
    const int id = getLabelClassId(index);
    if (id != -1)
        return label_classes_.at(id)->shortcut();
    return QString();
}

QVariant LabelClassesListModel::getLabelClassOrdinalIndex(const QModelIndex &index) const
{
    const int id = getLabelClassId(index);
    if (id != -1)
        return label_classes_.at(id)->ordinalIndex();
    return -1;
}

QVariant LabelClassesListModel::getLabelClassSelected(const QModelIndex &index) const
{
    return selection_->isSelected(index);
}

void LabelClassesListModel::updateSelection(const QItemSelection &selected, const QItemSelection &deselected)
{
    const QModelIndexList &dselected_items = deselected.indexes();
    int                    top{-1};
    int                    bottom{-1};
    for (const QModelIndex &index : dselected_items)
    {
        const int row = index.row();
        if (top == -1)
            top = row;
        else
            top = std::min(top, row);
        bottom = std::max(bottom, row);
    }
    emit dataChanged(index(top), index(bottom), {SelectedRole});

    top    = -1;
    bottom = -1;

    const QModelIndexList &selected_items = selected.indexes();
    for (const QModelIndex &index : selected_items)
    {
        const int row = index.row();
        if (top == -1)
            top = row;
        else
            top = std::min(top, row);
        bottom = std::max(bottom, row);
    }
    emit dataChanged(index(top), index(bottom), {SelectedRole});
}

} // namespace dltool::data
