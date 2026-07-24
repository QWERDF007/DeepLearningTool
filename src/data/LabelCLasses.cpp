#include "data/LabelClasses.h"

#include "database/DataBase.h"

#include <spdlog/spdlog.h>

#include <QColor>
#include <QJsonDocument>
#include <QJsonObject>

namespace dltool::data {

namespace {

constexpr const char *kUnlabeledGroup = "unlabeled";
constexpr const char *kGoodGroup      = "good";
constexpr const char *kAnomalyGroup   = "anomaly";
constexpr const char *kGroupKey       = "group";

QString normalizedColorName(const QString &color)
{
    const QColor parsed(color.trimmed());
    return parsed.isValid() ? parsed.name(QColor::HexRgb).toLower() : QString();
}

std::vector<uint8_t> extraDataForGroup(const QString &group)
{
    const QJsonObject object{
        {QString::fromUtf8(kGroupKey), normalizeLabelClassGroup(group)}
    };
    const QByteArray     json = QJsonDocument(object).toJson(QJsonDocument::Compact);
    std::vector<uint8_t> blob;
    blob.reserve(static_cast<size_t>(json.size()));
    for (char ch : json)
    {
        blob.push_back(static_cast<uint8_t>(ch));
    }
    return blob;
}

QString groupFromExtraData(const std::vector<uint8_t> &blob)
{
    if (blob.empty())
    {
        return defaultLabelClassGroup();
    }

    const QByteArray    bytes(reinterpret_cast<const char *>(blob.data()), static_cast<qsizetype>(blob.size()));
    QJsonParseError     parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        return defaultLabelClassGroup();
    }

    return normalizeLabelClassGroup(document.object().value(QString::fromUtf8(kGroupKey)).toString());
}

} // namespace

QString normalizeLabelClassGroup(const QString &group)
{
    const QString normalized = group.trimmed().toLower();
    if (normalized == QString::fromUtf8(kUnlabeledGroup) || normalized == QString("未标注")
        || normalized == QStringLiteral("unlabelled"))
    {
        return QString::fromUtf8(kUnlabeledGroup);
    }
    if (normalized == QString::fromUtf8(kGoodGroup) || normalized == QString("良好")
        || normalized == QStringLiteral("good"))
    {
        return QString::fromUtf8(kGoodGroup);
    }
    return QString::fromUtf8(kAnomalyGroup);
}

QString labelClassGroupDisplayName(const QString &group)
{
    const QString normalized = normalizeLabelClassGroup(group);
    if (normalized == QString::fromUtf8(kUnlabeledGroup))
        return QString("未标注");
    return normalized == QString::fromUtf8(kGoodGroup) ? QString("良好") : QString("异常");
}

QString defaultLabelClassGroup()
{
    return anomalyLabelClassGroup();
}

QString unlabeledLabelClassGroup()
{
    return QString::fromUtf8(kUnlabeledGroup);
}

QString anomalyLabelClassGroup()
{
    return QString::fromUtf8(kAnomalyGroup);
}

QString goodLabelClassGroup()
{
    return QString::fromUtf8(kGoodGroup);
}

LabelClass::LabelClass(const int64_t id, const QString &name, const QString &color, const QString &shortcut,
                       const int64_t ordinal_index, const QString &group, QObject *parent)
    : QObject(parent)
    , id_(id)
    , ordinal_index_(ordinal_index)
    , name_(name)
    , color_(color)
    , shortcut_(shortcut)
    , group_(normalizeLabelClassGroup(group))

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
    if (mutation_blocked_)
    {
        return false;
    }
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
                     {LabelClassIdRole, NameRole, ColorRole, ShortcutRole, GroupRole, GroupNameRole, OrdinalIndexRole,
                      SelectedRole});

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

bool LabelClass::setGroup(const QString &group)
{
    const QString normalized = normalizeLabelClassGroup(group);
    if (group_ == normalized)
        return false;
    group_ = normalized;
    return true;
}

LabelClassesListModel::LabelClassesListModel(dltool::database::ProjectDataBase *database, QObject *parent)
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
    QString                           err_msg;
    std::vector<int64_t>              label_class_ids, ordinal_indices;
    std::vector<QString>              names, colors, shortcuts;
    std::vector<std::vector<uint8_t>> extra_data;
    if (database_->getAllLabelClasses(label_class_ids, names, colors, shortcuts, ordinal_indices, extra_data, err_msg))
    {
        for (size_t i = 0; i < label_class_ids.size(); ++i)
        {
            label_classes_.emplace(
                label_class_ids[i],
                new LabelClass(label_class_ids[i], names[i], colors[i], shortcuts[i], ordinal_indices[i],
                               i < extra_data.size() ? groupFromExtraData(extra_data[i]) : defaultLabelClassGroup(),
                               this));
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
    case GroupRole:
        return getLabelClassGroup(index);
    case GroupNameRole:
        return getLabelClassGroupName(index);
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
        {       GroupRole,          "group"},
        {   GroupNameRole,     "group_name"},
        {OrdinalIndexRole,  "ordinal_index"},
        {    SelectedRole,       "selected"}
    };
}

bool LabelClassesListModel::addLabelClass(const QString &name, const QString &color, const QString &shortcut,
                                          const QString &group)
{
    if (database_ == nullptr)
    {
        spdlog::error("添加标签类别 [{}] 失败, 数据库未初始化", name.toUtf8().constData());
        return false;
    }
    const int     row = static_cast<int>(label_classes_.size());
    QString       err_msg;
    int64_t       label_class_id{-1};
    const QString normalized_group = normalizeLabelClassGroup(group);
    bool ok = database_->addLabelClass(name, color, shortcut, row, extraDataForGroup(normalized_group), label_class_id,
                                       err_msg);
    if (!ok)
    {
        spdlog::error("添加标签类别 [{}] 失败: {}", name.toUtf8().constData(), err_msg.toUtf8().constData());
        return false;
    }
    // 添加到队列尾部
    const int count = 1;
    beginInsertRows(QModelIndex(), row, row + count - 1);
    // beginInsertRows(QModelIndex(), row, row);
    label_classes_.emplace(label_class_id,
                           new LabelClass(label_class_id, name, color, shortcut, row, normalized_group, this));
    endInsertRows();
    QModelIndex index = this->index(row);
    selection_->select(index, QItemSelectionModel::ClearAndSelect);
    selection_->setCurrentIndex(index, QItemSelectionModel::Select);
    spdlog::info("添加标签类别 [{}] 成功", name.toUtf8().constData());
    return true;
}

bool LabelClassesListModel::updateLabelClass(const int64_t label_class_id, const QString &name, const QString &color,
                                             const QString &shortcut, const int64_t ordinal_index, const QString &group)
{
    if (mutation_blocked_)
    {
        return false;
    }
    if (database_ == nullptr)
    {
        spdlog::error("更新标签类别 [{}] 失败, 数据库未初始化", name.toUtf8().constData());
        return false;
    }
    QString       err_msg;
    const QString normalized_group = normalizeLabelClassGroup(group);
    bool          ok               = database_->updateLabelClass(label_class_id, name, color, shortcut, ordinal_index,
                                                                 extraDataForGroup(normalized_group), err_msg);
    if (!ok)
    {
        spdlog::error("更新标签类别 [{}] 失败: {}", name.toUtf8().constData(), err_msg.toUtf8().constData());
        return false;
    }

    auto found = label_classes_.find(label_class_id);
    if (found != label_classes_.end())
    {
        LabelClass *label_class = found->second;
        int         old_row     = static_cast<int>(label_class->ordinalIndex());
        int         new_row     = static_cast<int>(ordinal_index);

        // 先记录旧值用于日志
        QString old_name     = label_class->name();
        QString old_color    = label_class->color();
        QString old_shortcut = label_class->shortcut();
        QString old_group    = label_class->group();
        int64_t old_ordinal  = label_class->ordinalIndex();

        // 更新内存中的数据
        label_class->setName(name);
        label_class->setColor(color);
        label_class->setShortcut(shortcut);
        label_class->setOrdinalIndex(ordinal_index);
        label_class->setGroup(normalized_group);

        // 使用 ordinalIndex 作为行索引发出 dataChanged 信号
        // 如果 ordinalIndex 改变了，需要通知两个位置
        if (old_row != new_row)
        {
            emit dataChanged(index(old_row), index(old_row),
                             {NameRole, ColorRole, ShortcutRole, GroupRole, GroupNameRole, OrdinalIndexRole});
            emit dataChanged(index(new_row), index(new_row),
                             {NameRole, ColorRole, ShortcutRole, GroupRole, GroupNameRole, OrdinalIndexRole});
        }
        else
        {
            emit dataChanged(index(old_row), index(old_row),
                             {NameRole, ColorRole, ShortcutRole, GroupRole, GroupNameRole, OrdinalIndexRole});
        }

        spdlog::info("更新标签类别 {} -> {}, {} -> {}, {} -> {}, {} -> {}, {} -> {} 成功",
                     old_name.toUtf8().constData(), name.toUtf8().constData(), old_color.toUtf8().constData(),
                     color.toUtf8().constData(), old_shortcut.toUtf8().constData(), shortcut.toUtf8().constData(),
                     old_group.toUtf8().constData(), normalized_group.toUtf8().constData(), old_ordinal, ordinal_index);
    }
    return true;
}

bool dltool::data::LabelClassesListModel::updateLabelClass(const std::vector<int64_t> &label_class_ids,
                                                           const std::vector<int64_t> &ordinal_indexes)
{
    if (mutation_blocked_)
    {
        return false;
    }
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
    if (mutation_blocked_)
    {
        return false;
    }
    if (database_ == nullptr)
    {
        spdlog::error("删除标签类别 [{}] 失败, 数据库未初始化", label_class_id);
        return false;
    }

    // 查找要删除的项并获取其 ordinal_index
    auto found = label_classes_.find(label_class_id);
    if (found == label_classes_.end())
    {
        spdlog::error("删除标签类别 [{}] 失败, 未找到该类别", label_class_id);
        return false;
    }

    int64_t deleted_ordinal = found->second->ordinalIndex();
    QString name            = found->second->name();

    // 从数据库中删除
    QString err_msg;
    bool    ok = database_->deleteLabelClass(label_class_id, err_msg);
    if (!ok)
    {
        spdlog::error("删除标签类别 [{}] 失败: {}", name.toUtf8().constData(), err_msg.toUtf8().constData());
        return false;
    }

    // 从模型中移除
    int row = static_cast<int>(deleted_ordinal);
    beginRemoveRows(QModelIndex(), row, row);
    label_classes_.erase(label_class_id);
    endRemoveRows();

    // 更新所有在删除项之后的项的 ordinal_index
    std::vector<int64_t> ids_to_update;
    std::vector<int64_t> new_ordinals;

    for (const auto &[id, label_class] : label_classes_)
    {
        if (label_class->ordinalIndex() > deleted_ordinal)
        {
            ids_to_update.push_back(id);
            new_ordinals.push_back(label_class->ordinalIndex() - 1);
        }
    }

    // 批量更新数据库和内存中的 ordinal_index
    if (!ids_to_update.empty())
    {
        if (!updateLabelClass(ids_to_update, new_ordinals))
        {
            spdlog::error("更新删除后的序号索引失败");
            return false;
        }

        // 为所有受影响的行发出 dataChanged 信号
        // 删除后行会上移，因此从 deleted_ordinal 通知到末尾
        int minRow = static_cast<int>(deleted_ordinal);
        int maxRow = rowCount() - 1;
        if (maxRow >= minRow)
        {
            emit dataChanged(index(minRow), index(maxRow),
                             {LabelClassIdRole, NameRole, ColorRole, ShortcutRole, GroupRole, GroupNameRole,
                              OrdinalIndexRole, SelectedRole});
        }
    }

    // 自动选择逻辑：如果删除后没有选中项且列表不为空，则选中第一项
    if (rowCount() > 0 && !selection_->hasSelection())
    {
        QModelIndex firstIndex = index(0);
        selection_->select(firstIndex, QItemSelectionModel::ClearAndSelect);
        selection_->setCurrentIndex(firstIndex, QItemSelectionModel::Select);
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

std::vector<int64_t> LabelClassesListModel::getAllLabelClassIds() const
{
    std::vector<int64_t> ids;
    ids.reserve(label_classes_.size());
    for (const auto &[id, label_class] : label_classes_)
    {
        ids.push_back(id);
    }
    return ids;
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

QString LabelClassesListModel::getLabelClassGroup(const int label_class_id) const
{
    auto found = label_classes_.find(label_class_id);
    if (found != label_classes_.end())
        return found->second->group();
    return defaultLabelClassGroup();
}

QString LabelClassesListModel::getLabelClassGroupName(const int label_class_id) const
{
    return labelClassGroupDisplayName(getLabelClassGroup(label_class_id));
}

bool LabelClassesListModel::isUnlabeledLabelClass(const int label_class_id) const
{
    return getLabelClassGroup(label_class_id) == unlabeledLabelClassGroup();
}

bool LabelClassesListModel::isAnomalyLabelClass(const int label_class_id) const
{
    return getLabelClassGroup(label_class_id) == anomalyLabelClassGroup();
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

QString LabelClassesListModel::getCurrentLabelClassGroup() const
{
    QModelIndex index = selection_->currentIndex();
    if (index.row() < 0 || index.row() >= rowCount())
        return defaultLabelClassGroup();
    return getLabelClassGroup(index).toString();
}

bool LabelClassesListModel::updateLabelClassGroup(const int64_t label_class_id, const QString &group)
{
    if (mutation_blocked_)
    {
        return false;
    }
    auto found = label_classes_.find(label_class_id);
    if (found == label_classes_.end())
    {
        return false;
    }

    LabelClass *label_class = found->second;
    return updateLabelClass(label_class_id, label_class->name(), label_class->color(), label_class->shortcut(),
                            label_class->ordinalIndex(), group);
}

QString LabelClassesListModel::isValid(const int label_class_id, const QString &name, const QString &color,
                                       const QString &shortcut, const int ordinal_index) const
{
    // 新建类别时（label_class_id == -1），ordinal_index 为 -1 表示不验证序号
    if (label_class_id != -1 && (ordinal_index > static_cast<int>(label_classes_.size()) - 1 || ordinal_index < 0))
        return "error:标签序号索引超出范围";

    const QString normalized_color = normalizedColorName(color);
    if (normalized_color.isEmpty())
        return "error:标签颜色无效";
    if (shortcut.size() > 1)
        return "error:标签快捷键只能是单个字符";

    for (const auto &[_, label_class] : label_classes_)
    {
        if (label_class->id() == label_class_id)
            continue;
        if (label_class->name() == name)
            return "error:标签名称已存在";
        if (normalizedColorName(label_class->color()) == normalized_color)
            return "error:标签颜色已存在";
        if (!shortcut.isEmpty() && label_class->shortcut().compare(shortcut, Qt::CaseInsensitive) == 0)
            return "error:标签快捷键已存在";
        if (ordinal_index >= 0 && label_class->ordinalIndex() == ordinal_index)
            return "warning:修改序号将重新排序标签类别";
    }
    return QString();
}

int LabelClassesListModel::findByShortcut(const QString &shortcut) const
{
    if (shortcut.isEmpty())
        return -1;

    int     best_index   = -1;
    int64_t best_ordinal = std::numeric_limits<int64_t>::max();

    for (const auto &[_, label_class] : label_classes_)
    {
        if (label_class->shortcut().compare(shortcut, Qt::CaseInsensitive) == 0)
        {
            // 选择 ordinal_index 最小的类别（即列表中的第一个）
            if (label_class->ordinalIndex() < best_ordinal)
            {
                best_ordinal = label_class->ordinalIndex();
                best_index   = static_cast<int>(label_class->ordinalIndex());
            }
        }
    }

    return best_index;
}

bool LabelClassesListModel::selectByShortcut(const QString &shortcut)
{
    int match_index = findByShortcut(shortcut);
    if (match_index < 0)
        return false;

    QModelIndex model_index = index(match_index, 0);
    selection_->select(model_index, QItemSelectionModel::ClearAndSelect);
    selection_->setCurrentIndex(model_index, QItemSelectionModel::Select);
    return true;
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

QVariant LabelClassesListModel::getLabelClassGroup(const QModelIndex &index) const
{
    const int id = getLabelClassId(index);
    if (id != -1)
        return label_classes_.at(id)->group();
    return defaultLabelClassGroup();
}

QVariant LabelClassesListModel::getLabelClassGroupName(const QModelIndex &index) const
{
    return labelClassGroupDisplayName(getLabelClassGroup(index).toString());
}

QVariant LabelClassesListModel::getLabelClassOrdinalIndex(const QModelIndex &index) const
{
    const int id = getLabelClassId(index);
    if (id != -1)
        return static_cast<qlonglong>(label_classes_.at(id)->ordinalIndex());
    return -1;
}

QVariant LabelClassesListModel::getLabelClassSelected(const QModelIndex &index) const
{
    return selection_->isSelected(index);
}

void LabelClassesListModel::updateSelection(const QItemSelection &selected, const QItemSelection &deselected)
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
