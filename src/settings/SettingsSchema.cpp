#include "settings/SettingsSchema.h"

#include "database/DataBase.h"

#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QQmlEngine>

#include <algorithm>

namespace dltool::settings {

namespace {

QString camelToSnake(const QString &value)
{
    QString result;
    result.reserve(value.size() + 8);
    for (int i = 0; i < value.size(); ++i)
    {
        const QChar ch = value.at(i);
        if (ch.isUpper() && i > 0 && result.back() != QLatin1Char('_'))
            result.append(QLatin1Char('_'));
        result.append(ch.toLower());
    }
    return result;
}

QString defaultTableName(QString group_key)
{
    if (group_key.endsWith(QStringLiteral("Settings")))
        group_key.chop(8);
    return camelToSnake(group_key) + QStringLiteral("_settings");
}

QString nodeString(const YAML::Node &node, const QString &fallback = {})
{
    if (!node || node.IsNull())
        return fallback;
    return QString::fromUtf8(node.as<std::string>().c_str());
}

QVariant nodeVariant(const YAML::Node &node)
{
    if (!node || node.IsNull())
        return {};
    if (node.IsScalar())
    {
        const QString text = QString::fromUtf8(node.as<std::string>().c_str());
        const QString lower = text.toLower();
        if (lower == QStringLiteral("true"))
            return true;
        if (lower == QStringLiteral("false"))
            return false;

        bool ok = false;
        const qlonglong integer = text.toLongLong(&ok);
        if (ok)
            return integer;

        const double floating = text.toDouble(&ok);
        if (ok)
            return floating;

        return text;
    }
    if (node.IsSequence())
    {
        QVariantList list;
        for (const YAML::Node &entry : node)
            list.append(nodeVariant(entry));
        return list;
    }
    if (node.IsMap())
    {
        QVariantMap map;
        for (auto it = node.begin(); it != node.end(); ++it)
            map.insert(nodeString(it->first), nodeVariant(it->second));
        return map;
    }
    return {};
}

YAML::Node firstNode(const YAML::Node &node, std::initializer_list<const char *> keys)
{
    for (const char *key : keys)
    {
        const YAML::Node value = node[key];
        if (value)
            return value;
    }
    return {};
}

SettingsField parseField(const YAML::Node &node, const int ordinal_index)
{
    SettingsField field;
    field.name_en       = nodeString(firstNode(node, {"name_en", "key", "name"}));
    field.name_cn       = nodeString(firstNode(node, {"name_cn", "label"}));
    field.property_name = nodeString(firstNode(node, {"property_name", "property"}), field.name_en);
    field.value         = nodeVariant(firstNode(node, {"value", "default_value"}));
    field.default_value = nodeVariant(firstNode(node, {"default_value", "value"}));
    field.value_type    = nodeString(firstNode(node, {"value_type", "type"}), QStringLiteral("string"));
    field.value_range   = nodeVariant(firstNode(node, {"value_range", "range"})).toList();
    field.control_type  = nodeString(firstNode(node, {"control_type", "control"}), QStringLiteral("text"));
    field.options       = nodeVariant(node["options"]).toList();
    field.options_map   = nodeVariant(firstNode(node, {"options_map", "key_values", "values_map"})).toMap();
    field.section       = nodeString(node["section"]);
    field.description   = nodeString(node["description"]);
    field.visible       = node["visible"] ? node["visible"].as<bool>() : true;
    field.ordinal_index = node["ordinal_index"] ? node["ordinal_index"].as<int>() : ordinal_index;
    return field;
}

QVector<QString> settingsConfigDirs()
{
    const QDir app_dir(QCoreApplication::applicationDirPath());
    const QDir current_dir(QDir::currentPath());
    return {
        app_dir.filePath(QStringLiteral("config/settings")),
        app_dir.filePath(QStringLiteral("../config/settings")),
        current_dir.filePath(QStringLiteral("config/settings")),
    };
}

QVector<QFileInfo> settingsConfigFiles()
{
    QVector<QFileInfo> files;
    for (const QString &path : settingsConfigDirs())
    {
        const QDir dir(path);
        if (!dir.exists())
            continue;
        const QFileInfoList entries = dir.entryInfoList({QStringLiteral("*.yaml"), QStringLiteral("*.yml")},
                                                        QDir::Files, QDir::Name);
        for (const QFileInfo &entry : entries)
        {
            const bool exists = std::any_of(files.cbegin(), files.cend(),
                                            [&entry](const QFileInfo &file)
                                            { return file.absoluteFilePath() == entry.absoluteFilePath(); });
            if (!exists)
                files.append(entry);
        }
    }
    return files;
}

QVariant typedScalar(const QString &type, const QVariant &value)
{
    const QString normalized = type.trimmed().toLower();
    if (normalized == QStringLiteral("bool") || normalized == QStringLiteral("boolean"))
    {
        if (value.userType() == QMetaType::QString)
        {
            const QString text = value.toString().trimmed().toLower();
            return text == QStringLiteral("true") || text == QStringLiteral("1") || text == QStringLiteral("yes");
        }
        return value.toBool();
    }
    if (normalized == QStringLiteral("int") || normalized == QStringLiteral("integer"))
        return value.toInt();
    if (normalized == QStringLiteral("double") || normalized == QStringLiteral("float") || normalized == QStringLiteral("real"))
        return value.toDouble();
    return value.toString();
}

} // namespace

SettingsFieldModel::SettingsFieldModel(QObject *parent)
    : QAbstractListModel(parent)
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
}

SettingsFieldModel::SettingsFieldModel(QString group_key, QString table_name, QString label,
                                       std::vector<SettingsField> fields, QObject *parent)
    : QAbstractListModel(parent)
    , group_key_(std::move(group_key))
    , table_name_(std::move(table_name))
    , label_(std::move(label))
    , fields_(std::move(fields))
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
}

SettingsFieldModel::~SettingsFieldModel() = default;

QString SettingsFieldModel::groupKey() const { return group_key_; }
QString SettingsFieldModel::tableName() const { return table_name_; }
QString SettingsFieldModel::label() const { return label_; }
int SettingsFieldModel::count() const { return rowCount(); }

int SettingsFieldModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(fields_.size());
}

QVariant SettingsFieldModel::data(const QModelIndex &index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};

    const SettingsField &field = fields_.at(static_cast<size_t>(index.row()));
    switch (role)
    {
    case NameEnRole:
        return field.name_en;
    case NameCnRole:
        return field.name_cn;
    case PropertyNameRole:
        return field.property_name;
    case ValueRole:
    case Qt::EditRole:
        return field.value;
    case DefaultValueRole:
        return field.default_value;
    case ValueTypeRole:
        return field.value_type;
    case ValueRangeRole:
        return field.value_range;
    case ControlTypeRole:
        return field.control_type;
    case OptionsRole:
        return field.options;
    case OptionsMapRole:
        return field.options_map;
    case SectionRole:
        return field.section;
    case DescriptionRole:
        return field.description;
    case VisibleRole:
        return field.visible;
    case OrdinalIndexRole:
        return field.ordinal_index;
    default:
        return {};
    }
}

bool SettingsFieldModel::setData(const QModelIndex &index, const QVariant &value, const int role)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return false;
    if (role != ValueRole && role != Qt::EditRole)
        return false;

    SettingsField &field = fields_[static_cast<size_t>(index.row())];
    const QVariant next  = typedValue(field, value);
    if (field.value == next)
        return true;

    field.value = next;
    emit dataChanged(index, index, {ValueRole, Qt::EditRole});
    emit valueChanged(field.name_en, field.value);
    return true;
}

Qt::ItemFlags SettingsFieldModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags item_flags = QAbstractListModel::flags(index);
    if (index.isValid())
        item_flags |= Qt::ItemIsEditable;
    return item_flags;
}

QHash<int, QByteArray> SettingsFieldModel::roleNames() const
{
    return {
        {      NameEnRole,       "nameEn"},
        {      NameCnRole,       "nameCn"},
        {PropertyNameRole, "propertyName"},
        {       ValueRole,        "value"},
        {DefaultValueRole, "defaultValue"},
        {   ValueTypeRole,    "valueType"},
        {  ValueRangeRole,   "valueRange"},
        { ControlTypeRole,  "controlType"},
        {     OptionsRole,      "options"},
        {  OptionsMapRole,   "optionsMap"},
        {     SectionRole,      "section"},
        { DescriptionRole,  "description"},
        {     VisibleRole,      "visible"},
        {OrdinalIndexRole, "ordinalIndex"},
    };
}

QVariant SettingsFieldModel::valueForName(const QString &name) const
{
    const int row = indexOfName(name);
    return row >= 0 ? fields_.at(static_cast<size_t>(row)).value : QVariant();
}

QVariant SettingsFieldModel::valueForProperty(const QString &property_name) const
{
    const int row = indexOfProperty(property_name);
    return row >= 0 ? fields_.at(static_cast<size_t>(row)).value : QVariant();
}

bool SettingsFieldModel::setValueForName(const QString &name, const QVariant &value)
{
    const int row = indexOfName(name);
    return row >= 0 && setData(index(row), value, ValueRole);
}

QVariantMap SettingsFieldModel::fieldMap(const int row) const
{
    if (row < 0 || row >= rowCount())
        return {};
    return toMap(fields_.at(static_cast<size_t>(row)));
}

QVariantList SettingsFieldModel::optionsForKey(const QString &name, const QString &key) const
{
    const int row = indexOfName(name);
    if (row < 0)
        return {};
    return fields_.at(static_cast<size_t>(row)).options_map.value(key).toList();
}

QVariantMap SettingsFieldModel::valuesMap() const
{
    QVariantMap values;
    for (const SettingsField &field : fields_)
        values.insert(field.name_en, field.value);
    return values;
}

QVariantList SettingsFieldModel::schemaRows() const
{
    QVariantList rows;
    for (const SettingsField &field : fields_)
        rows.append(toMap(field));
    return rows;
}

void SettingsFieldModel::loadValues(const QVariantMap &values)
{
    if (fields_.empty())
        return;

    bool changed = false;
    for (SettingsField &field : fields_)
    {
        if (!values.contains(field.name_en))
            continue;
        const QVariant next = typedValue(field, values.value(field.name_en));
        if (field.value != next)
        {
            field.value = next;
            changed     = true;
        }
    }

    if (changed)
        emit dataChanged(index(0), index(rowCount() - 1), {ValueRole, Qt::EditRole});
}

void SettingsFieldModel::resetValues()
{
    if (fields_.empty())
        return;

    bool changed = false;
    for (SettingsField &field : fields_)
    {
        const QVariant next = typedValue(field, field.default_value);
        if (field.value != next)
        {
            field.value = next;
            changed     = true;
        }
    }
    if (changed)
        emit dataChanged(index(0), index(rowCount() - 1), {ValueRole, Qt::EditRole});
}

int SettingsFieldModel::indexOfName(const QString &name) const
{
    const auto found = std::find_if(fields_.cbegin(), fields_.cend(),
                                    [&name](const SettingsField &field) { return field.name_en == name; });
    return found == fields_.cend() ? -1 : static_cast<int>(std::distance(fields_.cbegin(), found));
}

int SettingsFieldModel::indexOfProperty(const QString &property_name) const
{
    const auto found = std::find_if(fields_.cbegin(), fields_.cend(),
                                    [&property_name](const SettingsField &field)
                                    { return field.property_name == property_name; });
    return found == fields_.cend() ? -1 : static_cast<int>(std::distance(fields_.cbegin(), found));
}

QVariant SettingsFieldModel::typedValue(const SettingsField &field, const QVariant &value) const
{
    return typedScalar(field.value_type, value);
}

QVariantMap SettingsFieldModel::toMap(const SettingsField &field) const
{
    return {
        {       QStringLiteral("name_en"), field.name_en},
        {       QStringLiteral("name_cn"), field.name_cn},
        { QStringLiteral("property_name"), field.property_name},
        {         QStringLiteral("value"), field.value},
        { QStringLiteral("default_value"), field.default_value},
        {    QStringLiteral("value_type"), field.value_type},
        {   QStringLiteral("value_range"), field.value_range},
        {  QStringLiteral("control_type"), field.control_type},
        {       QStringLiteral("options"), field.options},
        {   QStringLiteral("options_map"), field.options_map},
        {       QStringLiteral("section"), field.section},
        {   QStringLiteral("description"), field.description},
        {       QStringLiteral("visible"), field.visible},
        { QStringLiteral("ordinal_index"), field.ordinal_index},
    };
}

SettingsCatalog::SettingsCatalog(QObject *parent)
    : QAbstractListModel(parent)
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
}

SettingsCatalog::~SettingsCatalog() = default;

int SettingsCatalog::count() const { return rowCount(); }

int SettingsCatalog::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(groups_.size());
}

QVariant SettingsCatalog::data(const QModelIndex &index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};

    const SettingsFieldModel *group = groups_.at(static_cast<size_t>(index.row())).get();
    switch (role)
    {
    case GroupKeyRole:
        return group->groupKey();
    case TableNameRole:
        return group->tableName();
    case LabelRole:
        return group->label();
    case FieldModelRole:
        return QVariant::fromValue(static_cast<QObject *>(const_cast<SettingsFieldModel *>(group)));
    default:
        return {};
    }
}

QHash<int, QByteArray> SettingsCatalog::roleNames() const
{
    return {
        {   GroupKeyRole,    "groupKey"},
        {  TableNameRole,   "tableName"},
        {      LabelRole,       "label"},
        { FieldModelRole,  "fieldModel"},
    };
}

SettingsFieldModel *SettingsCatalog::group(const QString &group_key) const
{
    const int row = indexOfGroup(group_key);
    return row >= 0 ? groups_.at(static_cast<size_t>(row)).get() : nullptr;
}

SettingsFieldModel *SettingsCatalog::groupAt(const int row) const
{
    return row >= 0 && row < rowCount() ? groups_.at(static_cast<size_t>(row)).get() : nullptr;
}

QVariant SettingsCatalog::value(const QString &group_key, const QString &name, const QVariant &fallback) const
{
    const SettingsFieldModel *model = group(group_key);
    if (model == nullptr)
        return fallback;
    const QVariant value = model->valueForName(name);
    return value.isValid() ? value : fallback;
}

QVariantList SettingsCatalog::optionsForKey(const QString &group_key, const QString &name, const QString &key) const
{
    const SettingsFieldModel *model = group(group_key);
    return model != nullptr ? model->optionsForKey(name, key) : QVariantList{};
}

bool SettingsCatalog::loadFromConfig(QString &err_msg)
{
    const QVector<QFileInfo> files = settingsConfigFiles();
    if (files.isEmpty())
    {
        err_msg = QStringLiteral("settings config directory not found");
        return false;
    }

    beginResetModel();
    groups_.clear();
    try
    {
        for (const QFileInfo &file : files)
        {
            const YAML::Node root = YAML::LoadFile(file.absoluteFilePath().toStdString());
            if (!root.IsMap())
                continue;

            for (auto it = root.begin(); it != root.end(); ++it)
            {
                const QString group_key  = nodeString(it->first);
                const YAML::Node group   = it->second;
                YAML::Node fields_node   = group.IsSequence() ? group : group["fields"];
                const QString table_name = group.IsMap() ? nodeString(group["table"], defaultTableName(group_key))
                                                         : defaultTableName(group_key);
                const QString label      = group.IsMap() ? nodeString(group["label"], group_key) : group_key;
                if (!fields_node || !fields_node.IsSequence())
                    continue;

                std::vector<SettingsField> fields;
                int ordinal = 0;
                for (const YAML::Node &field_node : fields_node)
                {
                    SettingsField field = parseField(field_node, ordinal++);
                    if (!field.name_en.isEmpty())
                        fields.push_back(std::move(field));
                }
                addGroup(group_key, table_name, label, std::move(fields));
            }
        }
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        groups_.clear();
        endResetModel();
        emit countChanged();
        return false;
    }
    endResetModel();
    emit countChanged();
    return true;
}

void SettingsCatalog::syncAndLoad(database::SettingsDataBase *database)
{
    if (database == nullptr)
        return;

    QString err_msg;
    if (groups_.empty() && !loadFromConfig(err_msg))
    {
        spdlog::warn("Load settings config failed: {}", err_msg.toUtf8().constData());
        return;
    }

    for (const auto &group_ptr : groups_)
    {
        SettingsFieldModel *model = group_ptr.get();
        QString sync_err;
        if (!database->syncSettingsSchema(model->tableName(), model->schemaRows(), sync_err) && !sync_err.isEmpty())
            spdlog::warn("Sync settings schema failed for {}: {}", model->tableName().toUtf8().constData(),
                         sync_err.toUtf8().constData());

        QString load_err;
        model->loadValues(database->loadSettings(model->tableName(), load_err));
        if (!load_err.isEmpty())
            spdlog::warn("Load settings values failed for {}: {}", model->tableName().toUtf8().constData(),
                         load_err.toUtf8().constData());
    }
}

void SettingsCatalog::save(database::SettingsDataBase *database) const
{
    if (database == nullptr)
        return;

    for (const auto &group_ptr : groups_)
    {
        QString err_msg;
        if (!database->saveSettings(group_ptr->tableName(), group_ptr->valuesMap(), err_msg) && !err_msg.isEmpty())
            spdlog::error("Save settings group failed for {}: {}", group_ptr->tableName().toUtf8().constData(),
                          err_msg.toUtf8().constData());
    }
}

void SettingsCatalog::reset()
{
    for (const auto &group_ptr : groups_)
        group_ptr->resetValues();
}

SettingsFieldModel *SettingsCatalog::addGroup(QString group_key, QString table_name, QString label,
                                              std::vector<SettingsField> fields)
{
    auto group = std::make_unique<SettingsFieldModel>(std::move(group_key), std::move(table_name), std::move(label),
                                                      std::move(fields), this);
    SettingsFieldModel *ptr = group.get();
    connect(ptr, &SettingsFieldModel::valueChanged, this,
            [this, ptr](const QString &name, const QVariant &value)
            {
                emit fieldValueChanged(ptr->groupKey(), name, value);
                emit valueChanged();
            });
    groups_.push_back(std::move(group));
    return ptr;
}

int SettingsCatalog::indexOfGroup(const QString &group_key) const
{
    const auto found = std::find_if(groups_.cbegin(), groups_.cend(),
                                    [&group_key](const std::unique_ptr<SettingsFieldModel> &group)
                                    { return group->groupKey() == group_key; });
    return found == groups_.cend() ? -1 : static_cast<int>(std::distance(groups_.cbegin(), found));
}

} // namespace dltool::settings
