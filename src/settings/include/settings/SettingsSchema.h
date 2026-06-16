#pragma once

#include "dltool/settings/Export.h"

#include <QAbstractListModel>
#include <QHash>
#include <QObject>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml>

#include <memory>
#include <vector>

namespace dltool::database {
class SettingsDataBase;
}

namespace dltool::settings {

struct SETTINGS_API SettingsField
{
    QString     name_en;
    QString     name_cn;
    QString     property_name;
    QVariant    value;
    QVariant    default_value;
    QString     value_type;
    QVariantList value_range;
    QString     control_type;
    QVariantList options;
    QVariantMap options_map;
    QString     section;
    QString     description;
    bool        visible{true};
    int         ordinal_index{0};
};

class SETTINGS_API SettingsFieldModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SettingsFieldModel)
    QML_UNCREATABLE("SettingsFieldModel is owned by SettingsCatalog")
    Q_PROPERTY(QString groupKey READ groupKey CONSTANT FINAL)
    Q_PROPERTY(QString tableName READ tableName CONSTANT FINAL)
    Q_PROPERTY(QString label READ label CONSTANT FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role
    {
        NameEnRole = Qt::UserRole + 1,
        NameCnRole,
        PropertyNameRole,
        ValueRole,
        DefaultValueRole,
        ValueTypeRole,
        ValueRangeRole,
        ControlTypeRole,
        OptionsRole,
        OptionsMapRole,
        SectionRole,
        DescriptionRole,
        VisibleRole,
        OrdinalIndexRole,
    };
    Q_ENUM(Role)

    explicit SettingsFieldModel(QObject *parent = nullptr);
    SettingsFieldModel(QString group_key, QString table_name, QString label, std::vector<SettingsField> fields,
                       QObject *parent = nullptr);
    ~SettingsFieldModel() override;

    QString groupKey() const;
    QString tableName() const;
    QString label() const;
    int     count() const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QVariant valueForName(const QString &name) const;
    Q_INVOKABLE QVariant valueForProperty(const QString &property_name) const;
    Q_INVOKABLE bool     setValueForName(const QString &name, const QVariant &value);
    Q_INVOKABLE QVariantMap fieldMap(int row) const;
    Q_INVOKABLE QVariantList optionsForKey(const QString &name, const QString &key) const;

    QVariantMap valuesMap() const;
    QVariantList schemaRows() const;
    void loadValues(const QVariantMap &values);
    void resetValues();

signals:
    void countChanged();
    void valueChanged(const QString &name, const QVariant &value);

private:
    int         indexOfName(const QString &name) const;
    int         indexOfProperty(const QString &property_name) const;
    QVariant    typedValue(const SettingsField &field, const QVariant &value) const;
    QVariantMap toMap(const SettingsField &field) const;

    QString                    group_key_;
    QString                    table_name_;
    QString                    label_;
    std::vector<SettingsField> fields_;
};

class SETTINGS_API SettingsCatalog : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SettingsCatalog)
    QML_UNCREATABLE("SettingsCatalog is owned by GlobalSettings")
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role
    {
        GroupKeyRole = Qt::UserRole + 1,
        TableNameRole,
        LabelRole,
        FieldModelRole,
    };
    Q_ENUM(Role)

    explicit SettingsCatalog(QObject *parent = nullptr);
    ~SettingsCatalog() override;

    int count() const;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE SettingsFieldModel *group(const QString &group_key) const;
    Q_INVOKABLE SettingsFieldModel *groupAt(int row) const;
    Q_INVOKABLE QVariant value(const QString &group_key, const QString &name, const QVariant &fallback = {}) const;
    Q_INVOKABLE QVariantList optionsForKey(const QString &group_key, const QString &name, const QString &key) const;

    bool loadFromConfig(QString &err_msg);
    void syncAndLoad(database::SettingsDataBase *database);
    void save(database::SettingsDataBase *database) const;
    void reset();

signals:
    void countChanged();
    void valueChanged();
    void fieldValueChanged(const QString &group_key, const QString &name, const QVariant &value);

private:
    SettingsFieldModel *addGroup(QString group_key, QString table_name, QString label, std::vector<SettingsField> fields);
    int                 indexOfGroup(const QString &group_key) const;

    std::vector<std::unique_ptr<SettingsFieldModel>> groups_;
};

} // namespace dltool::settings
