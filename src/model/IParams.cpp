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

ParamGroupModel::ParamGroupModel(QString key, QString label, QString description, const bool enabled,
                                 std::vector<ParamDefinition> params, QObject *parent)
    : QAbstractListModel(parent)
    , key_(std::move(key))
    , label_(std::move(label))
    , description_(std::move(description))
    , enabled_(enabled)
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

QString ParamGroupModel::key() const
{
    return key_;
}

QString ParamGroupModel::label() const
{
    return label_;
}

QString ParamGroupModel::description() const
{
    return description_;
}

bool ParamGroupModel::isEnabled() const
{
    return enabled_;
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
    case KeyRole:
        return param.key;
    case LabelRole:
        return param.label;
    case DescriptionRole:
        return param.description;
    case EditorTypeRole:
        return paramEditorTypeName(param.editor_type);
    case ValueRole:
    case Qt::EditRole:
        return currentValue(param);
    case DefaultValueRole:
        return param.default_value;
    case MinimumValueRole:
        return param.minimum_value;
    case MaximumValueRole:
        return param.maximum_value;
    case StepValueRole:
        return param.step_value;
    case DecimalsRole:
        return param.decimals;
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
    emit valueChanged(param.key, param.value);
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
        {            KeyRole,             "key"},
        {          LabelRole,           "label"},
        {    DescriptionRole,     "description"},
        {     EditorTypeRole,      "editorType"},
        {          ValueRole,           "value"},
        {   DefaultValueRole,    "defaultValue"},
        {   MinimumValueRole,    "minimumValue"},
        {   MaximumValueRole,    "maximumValue"},
        {      StepValueRole,       "stepValue"},
        {       DecimalsRole,        "decimals"},
        {        EnabledRole,         "enabled"},
        {        OptionsRole,         "options"},
        {           UnitRole,            "unit"},
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

QVariant ParamGroupModel::valueForKey(const QString &key) const
{
    const int row = indexOfParam(key);
    if (row < 0)
    {
        return {};
    }
    return currentValue(params_.at(row));
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
        const QVariant other_value = other.valueForKey(param.key);
        if (other_value.isValid() && param.value != other_value)
        {
            param.value = other_value;
            changed = true;
        }
    }

    if (changed)
    {
        emit dataChanged(index(0), index(rowCount() - 1), {ValueRole, Qt::EditRole});
    }
}

QVariant ParamGroupModel::currentValue(const ParamDefinition &param) const
{
    return normalizedValue(param);
}

int ParamGroupModel::indexOfParam(const QString &key) const
{
    const auto found = std::find_if(params_.cbegin(), params_.cend(),
                                    [&key](const ParamDefinition &param) { return param.key == key; });
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
    case GroupKeyRole:
        return group->key();
    case GroupLabelRole:
        return group->label();
    case GroupDescriptionRole:
        return group->description();
    case GroupEnabledRole:
        return group->isEnabled();
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
        {          GroupKeyRole,           "key"},
        {        GroupLabelRole,         "label"},
        {  GroupDescriptionRole,   "description"},
        {      GroupEnabledRole,       "enabled"},
        {        GroupCountRole,         "count"},
        {        GroupModelRole,    "groupModel"},
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

ParamGroupModel *IParams::addGroup(const QString &key, const QString &label, std::vector<ParamDefinition> params,
                                   const QString &description, const bool enabled)
{
    const int row = groupCount();
    beginInsertRows(QModelIndex(), row, row);
    auto group = std::make_unique<ParamGroupModel>(key, label, description, enabled, std::move(params), this);
    auto *ptr = group.get();
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
        const auto found = std::find_if(other_groups.cbegin(), other_groups.cend(),
                                        [group](const ParamGroupModel *other_group)
                                        { return other_group != nullptr && other_group->key() == group->key(); });
        if (found != other_groups.cend() && *found != nullptr)
        {
            group->copyValuesFrom(**found);
        }
    }
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

QString paramEditorTypeName(const ParamEditorType editor_type)
{
    switch (editor_type)
    {
    case ParamEditorType::Text:
        return QStringLiteral("text");
    case ParamEditorType::Integer:
        return QStringLiteral("integer");
    case ParamEditorType::Double:
        return QStringLiteral("double");
    case ParamEditorType::Slider:
        return QStringLiteral("slider");
    case ParamEditorType::CheckBox:
        return QStringLiteral("checkbox");
    case ParamEditorType::ComboBox:
        return QStringLiteral("comboBox");
    }

    return QStringLiteral("text");
}

} // namespace dltool::model
