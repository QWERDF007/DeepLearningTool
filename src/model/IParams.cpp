#include "model/IParams.h"

#include "model/ModelRegistry.h"
#include "model/ModelNestedOptions.h"
#include "parameter/DynamicOptions.h"
#include "parameter/ParameterSchema.h"

#include <QQmlEngine>
#include <algorithm>
#include <functional>
#include <utility>

namespace dltool::model {

namespace {

/**
 * @brief 获取参数当前有效值（优先使用设定值，否则使用默认值）
 * @param param 参数定义
 * @return 有效值
 */
QVariant normalizedValue(const ParamDefinition &param)
{
    return param.value.isValid() ? param.value : param.default_value;
}

QVariant normalizeDynamicValue(const ParamDefinition &param, const QVariant &value)
{
    return dltool::parameter::normalizeParameterValue(param, value);
}

enum class CondTokenType
{
    Ident,
    String,
    Number,
    Eq,
    Ne,
    And,
    Or,
    Not,
    LParen,
    RParen,
    LBracket,
    RBracket,
    Comma,
    In,
    End,
};

struct CondToken
{
    CondTokenType type{CondTokenType::End};
    QString       text;
};

QVector<CondToken> tokenizeCondition(const QString &input)
{
    QVector<CondToken> tokens;
    int                i  = 0;
    const QString      special = QStringLiteral("()[]=!&|,'\"");
    while (i < input.size())
    {
        const QChar ch = input.at(i);
        if (ch.isSpace())
        {
            ++i;
            continue;
        }
        if (ch == '(')
        {
            tokens.push_back({CondTokenType::LParen, QStringLiteral("(")});
            ++i;
            continue;
        }
        if (ch == ')')
        {
            tokens.push_back({CondTokenType::RParen, QStringLiteral(")")});
            ++i;
            continue;
        }
        if (ch == '[')
        {
            tokens.push_back({CondTokenType::LBracket, QStringLiteral("[")});
            ++i;
            continue;
        }
        if (ch == ']')
        {
            tokens.push_back({CondTokenType::RBracket, QStringLiteral("]")});
            ++i;
            continue;
        }
        if (ch == ',')
        {
            tokens.push_back({CondTokenType::Comma, QStringLiteral(",")});
            ++i;
            continue;
        }
        if (ch == '!')
        {
            if (i + 1 < input.size() && input.at(i + 1) == '=')
            {
                tokens.push_back({CondTokenType::Ne, QStringLiteral("!=")});
                i += 2;
            }
            else
            {
                tokens.push_back({CondTokenType::Not, QStringLiteral("!")});
                ++i;
            }
            continue;
        }
        if (ch == '=' && i + 1 < input.size() && input.at(i + 1) == '=')
        {
            tokens.push_back({CondTokenType::Eq, QStringLiteral("==")});
            i += 2;
            continue;
        }
        if (ch == '&' && i + 1 < input.size() && input.at(i + 1) == '&')
        {
            tokens.push_back({CondTokenType::And, QStringLiteral("&&")});
            i += 2;
            continue;
        }
        if (ch == '|' && i + 1 < input.size() && input.at(i + 1) == '|')
        {
            tokens.push_back({CondTokenType::Or, QStringLiteral("||")});
            i += 2;
            continue;
        }
        if (ch == '\'' || ch == '"')
        {
            const QChar quote = ch;
            ++i;
            QString text;
            while (i < input.size() && input.at(i) != quote)
                text += input.at(i++);
            ++i;
            tokens.push_back({CondTokenType::String, text});
            continue;
        }

        const int start = i;
        while (i < input.size() && !input.at(i).isSpace() && !special.contains(input.at(i)))
            ++i;
        const QString word = input.mid(start, i - start);
        if (word.compare(QStringLiteral("in"), Qt::CaseInsensitive) == 0)
        {
            tokens.push_back({CondTokenType::In, word});
        }
        else
        {
            bool number_ok = false;
            word.toDouble(&number_ok);
            tokens.push_back({number_ok ? CondTokenType::Number : CondTokenType::Ident, word});
        }
    }
    tokens.push_back({CondTokenType::End, {}});
    return tokens;
}

bool valuesEqual(const QVariant &lhs, const QVariant &rhs)
{
    bool lhs_number = false;
    bool rhs_number = false;
    const double lhs_double = lhs.toString().toDouble(&lhs_number);
    const double rhs_double = rhs.toString().toDouble(&rhs_number);
    if (lhs_number && rhs_number)
        return qFuzzyCompare(lhs_double, rhs_double);
    return lhs.toString().compare(rhs.toString(), Qt::CaseInsensitive) == 0;
}

class ConditionParser
{
public:
    ConditionParser(QVector<CondToken> tokens, std::function<QVariant(const QString &)> lookup)
        : tokens_(std::move(tokens))
        , lookup_(std::move(lookup))
    {
    }

    bool parse()
    {
        pos_ = 0;
        return parseOr() && peek().type == CondTokenType::End;
    }

private:
    const CondToken &peek() const
    {
        return tokens_.at(qMin(pos_, tokens_.size() - 1));
    }

    const CondToken &advance()
    {
        const CondToken &token = tokens_.at(qMin(pos_, tokens_.size() - 1));
        if (pos_ < tokens_.size())
            ++pos_;
        return token;
    }

    bool match(CondTokenType type)
    {
        if (peek().type != type)
            return false;
        advance();
        return true;
    }

    bool parseOr()
    {
        bool value = parseAnd();
        while (match(CondTokenType::Or))
            value = parseAnd() || value;
        return value;
    }

    bool parseAnd()
    {
        bool value = parseUnary();
        while (match(CondTokenType::And))
            value = parseUnary() && value;
        return value;
    }

    bool parseUnary()
    {
        if (match(CondTokenType::Not))
            return !parseUnary();
        return parsePrimary();
    }

    bool parsePrimary()
    {
        if (match(CondTokenType::LParen))
        {
            const bool value = parseOr();
            match(CondTokenType::RParen);
            return value;
        }

        QVariant lhs;
        if (peek().type == CondTokenType::Ident)
            lhs = lookup_(advance().text);
        else if (peek().type == CondTokenType::String || peek().type == CondTokenType::Number)
            lhs = advance().text;

        if (peek().type == CondTokenType::Eq || peek().type == CondTokenType::Ne)
        {
            const bool negate = advance().type == CondTokenType::Ne;
            QVariant rhs;
            if (peek().type == CondTokenType::Ident)
                rhs = lookup_(advance().text);
            else if (peek().type == CondTokenType::String || peek().type == CondTokenType::Number)
                rhs = advance().text;
            const bool equal = valuesEqual(lhs, rhs);
            return negate ? !equal : equal;
        }
        if (peek().type == CondTokenType::In)
        {
            advance();
            return parseInList(lhs, false);
        }
        if (peek().type == CondTokenType::Ident && peek().text.compare(QStringLiteral("not"), Qt::CaseInsensitive) == 0)
        {
            const int saved = pos_;
            advance();
            if (match(CondTokenType::In))
                return parseInList(lhs, true);
            pos_ = saved;
        }
        if (lhs.userType() == QMetaType::Bool)
            return lhs.toBool();
        return !lhs.toString().isEmpty();
    }

    bool parseInList(const QVariant &lhs, const bool negate)
    {
        match(CondTokenType::LBracket);
        bool found = false;
        while (peek().type != CondTokenType::RBracket && peek().type != CondTokenType::End)
        {
            QVariant item;
            if (peek().type == CondTokenType::Ident)
                item = lookup_(advance().text);
            else if (peek().type == CondTokenType::String || peek().type == CondTokenType::Number)
                item = advance().text;
            if (valuesEqual(lhs, item))
                found = true;
            match(CondTokenType::Comma);
        }
        match(CondTokenType::RBracket);
        return negate ? !found : found;
    }

    QVector<CondToken>                        tokens_;
    std::function<QVariant(const QString &)> lookup_;
    int                                       pos_{0};
};

bool evaluateCondition(const QString &expression, const std::function<QVariant(const QString &)> &lookup)
{
    if (expression.trimmed().isEmpty())
        return true;
    ConditionParser parser(tokenizeCondition(expression), lookup);
    return parser.parse();
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
    case EnabledRole:
        return enabled_ && param.enabled && evaluateEnabledWhen(param);
    case OptionsRole:
        return paramOptions(param);
    case OptionsValueMapRole:
        return paramOptionsValueMap(param);
    case ParamKindRole:
        return static_cast<int>(param.kind);
    case DisplayTypeRole:
        return param.display_type;
    case BackendKeyRole:
        return param.backend_key;
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

    ParamDefinition &param      = params_[index.row()];
    const QVariant  next_value = normalizeDynamicValue(param, value);
    if (param.value == next_value)
    {
        return true;
    }

    param.value = next_value;
    emit dataChanged(index, index, {ValueRole, Qt::EditRole});
    emit valueChanged(param.name_en, param.value);

    emit dataChanged(this->index(0), this->index(rowCount() - 1), {OptionsRole, OptionsValueMapRole});

    const QVector<int> dependent = dependentRows(param.name_en);
    for (const int row : dependent)
    {
        if (row != index.row())
            emit dataChanged(this->index(row), this->index(row), {EnabledRole});
    }
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
        {     EnabledRole,      "enabled"},
        {     OptionsRole,      "options"},
        {OptionsValueMapRole, "optionsValueMap"},
        {  ParamKindRole,      "paramKind"},
        {DisplayTypeRole,      "displayType"},
        { BackendKeyRole,      "backendKey"},
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
        const QVariant next_value = normalizeDynamicValue(param, other_value);
        if (other_value.isValid() && param.value != next_value)
        {
            param.value = next_value;
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

        const QVariant next_value = normalizeDynamicValue(param, values.value(param.name_en));
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

bool ParamGroupModel::evaluateEnabledWhen(const ParamDefinition &param) const
{
    if (param.enabled_when.trimmed().isEmpty())
        return true;

    const auto lookup = [this](const QString &name) -> QVariant
    {
        const int row = indexOfParam(name);
        return row >= 0 ? currentValue(params_.at(static_cast<size_t>(row))) : QVariant();
    };
    return evaluateCondition(param.enabled_when, lookup);
}

QVector<int> ParamGroupModel::dependentRows(const QString &name_en) const
{
    QVector<int> rows;
    for (int i = 0; i < static_cast<int>(params_.size()); ++i)
    {
        const QString expression = params_.at(static_cast<size_t>(i)).enabled_when;
        if (expression.trimmed().isEmpty())
            continue;
        for (const CondToken &token : tokenizeCondition(expression))
        {
            if (token.type == CondTokenType::Ident && token.text == name_en)
            {
                rows.append(i);
                break;
            }
        }
    }
    return rows;
}

QString ParamGroupModel::resolveWeightSize(const QString &size_hint, const ParamDefinition &param) const
{
    QString size = size_hint.trimmed();
    if (!size.isEmpty())
        return size;

    const QString size_param = param.variant_param.trimmed();
    if (size_param.isEmpty())
        return {};

    const int row = indexOfParam(size_param);
    if (row >= 0)
        return currentValue(params_.at(static_cast<size_t>(row))).toString().trimmed();

    if (weight_size_source_)
        return weight_size_source_->weightSizeValue(size_param);
    return {};
}

QStringList ParamGroupModel::weightExtensions() const
{
    if (weight_framework_.trimmed().isEmpty())
        return {};
    const FrameworkDefinition definition = registeredFramework(-1, weight_framework_);
    return definition.weight_extensions;
}

bool ParamGroupModel::isWeightParam(const ParamDefinition &param) const
{
    return param.kind == dltool::parameter::ParameterKind::Dynamic
        && param.backend_key.compare(QStringLiteral("model.checkpoints"), Qt::CaseInsensitive) == 0;
}

QVariantList ParamGroupModel::resolveWeightOptions(const ParamDefinition &param, QVariantMap &value_map) const
{
    QVariantList labels;
    value_map.clear();

    QVariantMap context;
    context.insert(QStringLiteral("model_name"), weight_model_name_);
    context.insert(QStringLiteral("project_dir"), weight_project_dir_);
    QStringList extensions = weightExtensions();
    if (extensions.isEmpty())
        extensions = {QStringLiteral("pt")};
    context.insert(QStringLiteral("extensions"), QVariant::fromValue(extensions));

    const auto result = parameter::DynamicOptionsRegistry::instance().resolve(param.backend_key, context);
    if (!result.provider_found)
        return labels;

    const QVariant current_value = currentValue(param);
    bool           found_current = false;
    for (const auto &option : result.options)
    {
        const QString label = option.display_value.trimmed();
        if (label.isEmpty())
            continue;
        labels.append(label);
        value_map.insert(label, option.actual_value);
        if (current_value.isValid() && option.actual_value == current_value)
            found_current = true;
    }

    if (current_value.isValid() && !current_value.toString().trimmed().isEmpty() && !found_current)
    {
        const QString label = QStringLiteral("当前值 (%1)").arg(current_value.toString());
        labels.append(label);
        value_map.insert(label, current_value);
    }
    return labels;
}

QVariantList ParamGroupModel::paramOptions(const ParamDefinition &param) const
{
    if (isWeightParam(param))
    {
        QVariantMap value_map;
        return resolveWeightOptions(param, value_map);
    }
    return param.options;
}

QVariantMap ParamGroupModel::paramOptionsValueMap(const ParamDefinition &param) const
{
    if (isWeightParam(param))
    {
        QVariantMap value_map;
        resolveWeightOptions(param, value_map);
        return value_map;
    }
    return param.options_value_map;
}

QVariantList ParamGroupModel::nestedOptions(const int row, const QString &size_hint) const
{
    if (row < 0 || row >= rowCount())
        return {};
    const ParamDefinition &param = params_.at(static_cast<size_t>(row));
    if (param.display_type.compare(QStringLiteral("group_combo"), Qt::CaseInsensitive) != 0)
        return {};

    QStringList extensions = weightExtensions();
    if (extensions.isEmpty())
        extensions = {QStringLiteral("pt")};

    return modelNestedOptions(weight_project_dir_, weight_project_db_, weight_framework_, weight_architecture_,
                              extensions, param.variants, param.variant_name_template, param.variant_param,
                              resolveWeightSize(size_hint, param), currentValue(param));
}

void ParamGroupModel::setWeightContext(const QString &project_dir, const QString &project_db,
                                       const QString &framework_name, const QString &architecture,
                                       const QString &model_name)
{
    weight_project_dir_  = project_dir;
    weight_project_db_   = project_db;
    weight_framework_    = framework_name;
    weight_architecture_ = architecture;
    weight_model_name_   = model_name;
    emit dataChanged(index(0), index(rowCount() - 1), {OptionsRole, OptionsValueMapRole});
}

void ParamGroupModel::setWeightSizeSource(IParams *source)
{
    weight_size_source_ = source;
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

void IParams::setWeightContext(const QString &project_dir, const QString &project_db, const QString &framework_name,
                               const QString &architecture, const QString &model_name)
{
    for (ParamGroupModel *group : groups())
    {
        if (group != nullptr)
            group->setWeightContext(project_dir, project_db, framework_name, architecture, model_name);
    }
}

void IParams::setWeightSizeSource(IParams *source)
{
    for (ParamGroupModel *group : groups())
    {
        if (group != nullptr)
            group->setWeightSizeSource(source);
    }
}

QString IParams::weightSizeValue(const QString &name_en) const
{
    for (const ParamGroupModel *group : groups())
    {
        if (group == nullptr)
            continue;
        const QString value = group->valueForName(name_en).toString().trimmed();
        if (!value.isEmpty())
            return value;
    }
    return {};
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




