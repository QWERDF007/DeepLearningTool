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
            emit dataChanged(index(idx), index(idx), {NameRole, ColorRole, ShortcutRole});
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
    int idx = 0;
    for (const auto &[id, label_class] : label_classes_)
    {
        if (index.row() == idx)
        {
            return id;
        }
        ++idx;
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
    return QVariant();
}

QVariant LabelClassesListModel::getLabelClassColor(const QModelIndex &index) const
{
    const int id = getLabelClassId(index);
    if (id != -1)
        return label_classes_.at(id)->color();
    return QVariant();
}

QVariant LabelClassesListModel::getLabelClassShortcut(const QModelIndex &index) const
{
    const int id = getLabelClassId(index);
    if (id != -1)
        return label_classes_.at(id)->shortcut();
    return QVariant();
}

QVariant LabelClassesListModel::getLabelClassOrdinalIndex(const QModelIndex &index) const
{
    const int id = getLabelClassId(index);
    if (id != -1)
        return label_classes_.at(id)->ordinalIndex();
    return QVariant();
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
