#include "model/IParams.h"

#include <QQmlEngine>
#include <algorithm>
#include <utility>

namespace dltool::model {

namespace {

QVariant normalizedValue(const ParamDefinition &param)
{
    return param.value.isValid() ? param.value : param.default_value;
}

} // namespace

ParamGroupModel::ParamGroupModel(QObject *parent)
    : QAbstractListModel(parent)
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
}

ParamGroupModel::ParamGroupModel(QString name_en, QString name_cn, QString description, const bool enabled,
                                 const int part_index, std::vector<ParamDefinition> params, QObject *parent)
    : QAbstractListModel(parent)
    , name_en_(std::move(name_en))
    , name_cn_(std::move(name_cn))
    , description_(std::move(description))
    , enabled_(enabled)
    , part_index_(part_index)
    , params_(std::move(params))
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    for (ParamDefinition &param : params_)
    {
        if (!param.value.isValid())
        {
            param.value = param.default_value;
        }
    }
}

ParamGroupModel::~ParamGroupModel() = default;

QString ParamGroupModel::nameEn() const
{
    return name_en_;
}

QString ParamGroupModel::nameCn() const
{
    return name_cn_;
}

QString ParamGroupModel::description() const
{
    return description_;
}

bool ParamGroupModel::isEnabled() const
{
    return enabled_;
}

int ParamGroupModel::partIndex() const
{
    return part_index_;
}

int ParamGroupModel::count() const
{
    return rowCount();
}

int ParamGroupModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
    {
        return 0;
    }
    return static_cast<int>(params_.size());
}

QVariant ParamGroupModel::data(const QModelIndex &index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
    {
        return {};
    }

    const ParamDefinition &param = params_.at(index.row());
    switch (role)
    {
    case NameEnRole:
        return param.name_en;
    case NameCnRole:
        return param.name_cn;
    case DescriptionRole:
        return param.description;
    case ValueRole:
    case Qt::EditRole:
        return currentValue(param);
    case DefaultValueRole:
        return param.default_value;
    case ValueTypeRole:
        return param.value_type;
    case ValueRangeRole:
        return param.value_range;
    case ControlTypeRole:
        return param.control_type;
    case EnabledRole:
        return enabled_ && param.enabled;
    case OptionsRole:
        return param.options;
    case UnitRole:
        return param.unit;
    default:
        return {};
    }
}

bool ParamGroupModel::setData(const QModelIndex &index, const QVariant &value, const int role)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
    {
        return false;
    }

    if (role != ValueRole && role != Qt::EditRole)
    {
        return false;
    }

    ParamDefinition &param = params_[index.row()];
    if (param.value == value)
    {
        return true;
    }

    param.value = value;
    emit dataChanged(index, index, {ValueRole, Qt::EditRole});
    emit valueChanged(param.name_en, param.value);
    return true;
}

Qt::ItemFlags ParamGroupModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags item_flags = QAbstractListModel::flags(index);
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
    {
        return item_flags;
    }

    if (enabled_ && params_.at(index.row()).enabled)
    {
        item_flags |= Qt::ItemIsEditable;
    }
    return item_flags;
}

QHash<int, QByteArray> ParamGroupModel::roleNames() const
{
    return {
        {      NameEnRole,       "nameEn"},
        {      NameCnRole,       "nameCn"},
        { DescriptionRole,  "description"},
        {       ValueRole,        "value"},
        {DefaultValueRole, "defaultValue"},
        {   ValueTypeRole,    "valueType"},
        {  ValueRangeRole,   "valueRange"},
        { ControlTypeRole,  "controlType"},
        {     EnabledRole,      "enabled"},
        {     OptionsRole,      "options"},
        {        UnitRole,         "unit"},
    };
}

bool ParamGroupModel::setValue(const int row, const QVariant &value)
{
    return setData(index(row), value, ValueRole);
}

QVariant ParamGroupModel::valueAt(const int row) const
{
    if (row < 0 || row >= rowCount())
    {
        return {};
    }
    return currentValue(params_.at(row));
}

QVariant ParamGroupModel::valueForName(const QString &name_en) const
{
    const int row = indexOfParam(name_en);
    if (row < 0)
    {
        return {};
    }
    return currentValue(params_.at(row));
}

QVariantMap ParamGroupModel::valuesMap() const
{
    QVariantMap values;
    for (const ParamDefinition &param : params_)
    {
        if (!param.name_en.isEmpty())
        {
            values.insert(param.name_en, currentValue(param));
        }
    }
    return values;
}

void ParamGroupModel::copyValuesFrom(const ParamGroupModel &other)
{
    if (params_.empty())
    {
        return;
    }

    bool changed = false;
    for (ParamDefinition &param : params_)
    {
        const QVariant other_value = other.valueForName(param.name_en);
        if (other_value.isValid() && param.value != other_value)
        {
            param.value = other_value;
            changed     = true;
        }
    }

    if (changed)
    {
        emit dataChanged(index(0), index(rowCount() - 1), {ValueRole, Qt::EditRole});
    }
}

bool ParamGroupModel::setValuesMap(const QVariantMap &values)
{
    if (params_.empty() || values.isEmpty())
    {
        return false;
    }

    int first_changed = -1;
    int last_changed  = -1;
    for (int i = 0; i < static_cast<int>(params_.size()); ++i)
    {
        ParamDefinition &param = params_[static_cast<size_t>(i)];
        if (param.name_en.isEmpty() || !values.contains(param.name_en))
        {
            continue;
        }

        const QVariant next_value = values.value(param.name_en);
        if (!next_value.isValid() || param.value == next_value)
        {
            continue;
        }

        param.value = next_value;
        if (first_changed < 0)
        {
            first_changed = i;
        }
        last_changed = i;
        emit valueChanged(param.name_en, param.value);
    }

    if (first_changed < 0)
    {
        return false;
    }

    emit dataChanged(index(first_changed), index(last_changed), {ValueRole, Qt::EditRole});
    return true;
}

QVariant ParamGroupModel::currentValue(const ParamDefinition &param) const
{
    return normalizedValue(param);
}

int ParamGroupModel::indexOfParam(const QString &name_en) const
{
    const auto found = std::find_if(params_.cbegin(), params_.cend(),
                                    [&name_en](const ParamDefinition &param) { return param.name_en == name_en; });
    if (found == params_.cend())
    {
        return -1;
    }
    return static_cast<int>(std::distance(params_.cbegin(), found));
}

IParams::IParams(QObject *parent)
    : QAbstractListModel(parent)
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
}

IParams::~IParams() = default;

QList<ParamGroupModel *> IParams::groups()
{
    QList<ParamGroupModel *> result;
    result.reserve(static_cast<int>(groups_.size()));
    for (const auto &group : groups_)
    {
        result.append(group.get());
    }
    return result;
}

QList<const ParamGroupModel *> IParams::groups() const
{
    QList<const ParamGroupModel *> result;
    result.reserve(static_cast<int>(groups_.size()));
    for (const auto &group : groups_)
    {
        result.append(group.get());
    }
    return result;
}

QList<QObject *> IParams::groupObjects() const
{
    QList<QObject *> result;
    result.reserve(static_cast<int>(groups_.size()));
    for (const auto &group : groups_)
    {
        result.append(group.get());
    }
    return result;
}

int IParams::groupCount() const
{
    return static_cast<int>(groups_.size());
}

int IParams::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
    {
        return 0;
    }
    return groupCount();
}

QVariant IParams::data(const QModelIndex &index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
    {
        return {};
    }

    const ParamGroupModel *group = groups_.at(static_cast<size_t>(index.row())).get();
    if (group == nullptr)
    {
        return {};
    }

    switch (role)
    {
    case GroupNameEnRole:
        return group->nameEn();
    case GroupNameCnRole:
        return group->nameCn();
    case GroupDescriptionRole:
        return group->description();
    case GroupEnabledRole:
        return group->isEnabled();
    case GroupPartIndexRole:
        return group->partIndex();
    case GroupCountRole:
        return group->count();
    case GroupModelRole:
        return QVariant::fromValue(static_cast<QObject *>(const_cast<ParamGroupModel *>(group)));
    default:
        return {};
    }
}

QHash<int, QByteArray> IParams::roleNames() const
{
    return {
        {     GroupNameEnRole,      "nameEn"},
        {     GroupNameCnRole,      "nameCn"},
        {GroupDescriptionRole, "description"},
        {    GroupEnabledRole,     "enabled"},
        {  GroupPartIndexRole,   "partIndex"},
        {      GroupCountRole,       "count"},
        {      GroupModelRole,  "groupModel"},
    };
}

ParamGroupModel *IParams::groupAt(const int row) const
{
    if (row < 0 || row >= groupCount())
    {
        return nullptr;
    }
    return groups_.at(static_cast<size_t>(row)).get();
}

QVariantMap IParams::valuesMap() const
{
    QVariantMap values;
    for (const auto &group : groups_)
    {
        if (group == nullptr)
        {
            continue;
        }
        values.insert(group->nameEn(), group->valuesMap());
    }
    return values;
}

ParamGroupModel *IParams::addGroup(const QString &name_en, const QString &name_cn, std::vector<ParamDefinition> params,
                                   const QString &description, const bool enabled, const int part_index)
{
    const int row = groupCount();
    beginInsertRows(QModelIndex(), row, row);
    auto  group = std::make_unique<ParamGroupModel>(name_en, name_cn, description, enabled, part_index,
                                                    std::move(params), this);
    auto *ptr   = group.get();
    groups_.push_back(std::move(group));
    endInsertRows();
    emit groupCountChanged();
    return ptr;
}

void IParams::copyValuesFrom(const IParams &other)
{
    const QList<ParamGroupModel *>       local_groups = groups();
    const QList<const ParamGroupModel *> other_groups = other.groups();
    for (ParamGroupModel *group : local_groups)
    {
        const auto found
            = std::find_if(other_groups.cbegin(), other_groups.cend(), [group](const ParamGroupModel *other_group)
                           { return other_group != nullptr && other_group->nameEn() == group->nameEn(); });
        if (found != other_groups.cend() && *found != nullptr)
        {
            group->copyValuesFrom(**found);
        }
    }
}

bool IParams::setValuesMap(const QVariantMap &values)
{
    if (values.isEmpty())
    {
        return false;
    }

    bool changed = false;
    for (ParamGroupModel *group : groups())
    {
        if (group == nullptr || group->nameEn().isEmpty() || !values.contains(group->nameEn()))
        {
            continue;
        }

        const QVariantMap group_values = values.value(group->nameEn()).toMap();
        if (!group_values.isEmpty())
        {
            changed = group->setValuesMap(group_values) || changed;
        }
    }
    return changed;
}

void IParams::clearGroups()
{
    beginResetModel();
    groups_.clear();
    endResetModel();
    emit groupCountChanged();
}

ITrainParams::ITrainParams(QObject *parent)
    : IParams(parent)
{
}

ITrainParams::~ITrainParams() = default;

ITestParams::ITestParams(QObject *parent)
    : IParams(parent)
{
}

ITestParams::~ITestParams() = default;

} // namespace dltool::model
