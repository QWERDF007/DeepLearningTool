#include "settings/SettingsObjects.h"

#include "settings/SettingsSchema.h"

#include <utility>

namespace dltool::settings {

namespace {

QString rangePropertyName(const QString &property_name, const QString &suffix)
{
    return property_name.isEmpty() ? QString() : property_name + suffix;
}

} // namespace

SettingsGroup::SettingsGroup(QObject *parent)
    : QQmlPropertyMap(this, parent)
{
}

SettingsGroup::~SettingsGroup() = default;

QString SettingsGroup::accessorPath() const
{
    return accessor_path_;
}

QString SettingsGroup::groupKey() const
{
    return field_model_ != nullptr ? field_model_->groupKey() : QString();
}

SettingsFieldModel *SettingsGroup::fieldModel() const
{
    return field_model_;
}

void SettingsGroup::bindModel(QString accessor_path, SettingsFieldModel *model)
{
    accessor_path_ = std::move(accessor_path);
    field_model_   = model;
    if (field_model_ != nullptr)
        reloadFromModel();
}

void SettingsGroup::clearValues()
{
    const QStringList existing_keys = keys();
    for (const QString &key : existing_keys) clear(key);
}

void SettingsGroup::reloadFromModel()
{
    updating_ = true;

    if (field_model_ != nullptr)
    {
        for (int row = 0; row < field_model_->rowCount(); ++row)
        {
            const QVariantMap field         = field_model_->fieldMap(row);
            const QString     property_name = field.value(QStringLiteral("property_name")).toString();
            if (property_name.isEmpty())
                continue;

            insertValue(property_name, field.value(QStringLiteral("value")));
            insertRangeValues(property_name, field.value(QStringLiteral("value_range")).toList());
        }
    }

    updating_ = false;
}

void SettingsGroup::updateFromFieldName(const QString &name, const QVariant &value)
{
    if (field_model_ == nullptr)
        return;

    const QString property_name = field_model_->propertyForName(name);
    if (property_name.isEmpty())
        return;

    updating_ = true;
    insertValue(property_name, value);
    updating_ = false;
}

QVariant SettingsGroup::valueOr(const QString &property_name, const QVariant &fallback) const
{
    const QVariant current = value(property_name);
    return current.isValid() ? current : fallback;
}

bool SettingsGroup::setValue(const QString &property_name, const QVariant &value)
{
    if (field_model_ == nullptr)
        return false;
    return field_model_->setValueForProperty(property_name, value);
}

QVariant SettingsGroup::updateValue(const QString &key, const QVariant &input)
{
    if (!updating_ && field_model_ != nullptr && field_model_->setValueForProperty(key, input))
        return field_model_->valueForProperty(key);
    return input;
}

void SettingsGroup::insertValue(const QString &key, const QVariant &value)
{
    if (!key.isEmpty())
        insert(key, value);
}

void SettingsGroup::insertRangeValues(const QString &property_name, const QVariantList &range)
{
    if (range.size() > 0)
        insertValue(rangePropertyName(property_name, QStringLiteral("From")), range.at(0));
    if (range.size() > 1)
        insertValue(rangePropertyName(property_name, QStringLiteral("To")), range.at(1));
    if (range.size() > 2)
        insertValue(rangePropertyName(property_name, QStringLiteral("StepSize")), range.at(2));
}

SettingsNamespace::SettingsNamespace(QObject *parent)
    : QQmlPropertyMap(this, parent)
{
}

SettingsNamespace::~SettingsNamespace() = default;

QString SettingsNamespace::accessorPath() const
{
    return accessor_path_;
}

void SettingsNamespace::setAccessorPath(QString accessor_path)
{
    accessor_path_ = std::move(accessor_path);
}

void SettingsNamespace::clearValues()
{
    const QStringList existing_keys = keys();
    for (const QString &key : existing_keys) clear(key);
}

void SettingsNamespace::insertObject(const QString &accessor, QObject *object)
{
    if (!accessor.isEmpty() && object != nullptr)
        insert(accessor, QVariant::fromValue(object));
}

QObject *SettingsNamespace::object(const QString &accessor) const
{
    return value(accessor).value<QObject *>();
}

} // namespace dltool::settings
