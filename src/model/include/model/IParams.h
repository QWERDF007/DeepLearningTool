#pragma once

#include "dltool/model/Export.h"

#include <QAbstractListModel>
#include <QObject>
#include <QHash>
#include <QList>
#include <QVariant>
#include <QString>
#include <QStringList>
#include <QtQml>

#include <memory>
#include <vector>

namespace dltool::model {

enum class ParamEditorType
{
    Text,
    Integer,
    Double,
    Slider,
    CheckBox,
    ComboBox,
};

struct MODEL_API ParamDefinition
{
    QString         key;
    QString         label;
    QString         description;
    ParamEditorType editor_type{ParamEditorType::Text};
    QVariant        value;
    QVariant        default_value;
    QVariant        minimum_value;
    QVariant        maximum_value;
    QVariant        step_value;
    int             decimals{0};
    bool            enabled{true};
    QStringList     options;
    QString         unit;
};

class MODEL_API ParamGroupModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ParamGroupModel)
    QML_UNCREATABLE("ParamGroupModel is owned by IParams")
    Q_PROPERTY(QString key READ key CONSTANT FINAL)
    Q_PROPERTY(QString label READ label CONSTANT FINAL)
    Q_PROPERTY(QString description READ description CONSTANT FINAL)
    Q_PROPERTY(bool enabled READ isEnabled CONSTANT FINAL)
    Q_PROPERTY(int partIndex READ partIndex CONSTANT FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role
    {
        KeyRole = Qt::UserRole + 1,
        LabelRole,
        DescriptionRole,
        EditorTypeRole,
        ValueRole,
        DefaultValueRole,
        MinimumValueRole,
        MaximumValueRole,
        StepValueRole,
        DecimalsRole,
        EnabledRole,
        OptionsRole,
        UnitRole,
    };
    Q_ENUM(Role)

    explicit ParamGroupModel(QObject *parent = nullptr);
    ParamGroupModel(QString key, QString label, QString description, bool enabled,
                    int part_index, std::vector<ParamDefinition> params, QObject *parent = nullptr);
    ~ParamGroupModel() override;

    QString key() const;
    QString label() const;
    QString description() const;
    bool    isEnabled() const;
    int     partIndex() const;
    int     count() const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE bool setValue(int row, const QVariant &value);
    Q_INVOKABLE QVariant valueAt(int row) const;
    Q_INVOKABLE QVariant valueForKey(const QString &key) const;

    void copyValuesFrom(const ParamGroupModel &other);

signals:
    void countChanged();
    void valueChanged(const QString &key, const QVariant &value);

private:
    QVariant currentValue(const ParamDefinition &param) const;
    int      indexOfParam(const QString &key) const;

    QString                  key_;
    QString                  label_;
    QString                  description_;
    bool                     enabled_{true};
    int                      part_index_{0};
    std::vector<ParamDefinition> params_;
};

class MODEL_API IParams : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(IParams)
    QML_UNCREATABLE("IParams is an abstract interface")
    Q_PROPERTY(QString typeName READ typeName CONSTANT FINAL)
    Q_PROPERTY(int count READ groupCount NOTIFY groupCountChanged FINAL)

public:
    enum Role
    {
        GroupKeyRole = Qt::UserRole + 1,
        GroupLabelRole,
        GroupDescriptionRole,
        GroupEnabledRole,
        GroupPartIndexRole,
        GroupCountRole,
        GroupModelRole,
    };
    Q_ENUM(Role)

    explicit IParams(QObject *parent = nullptr);
    ~IParams() override;

    virtual QString typeName() const = 0;
    QList<ParamGroupModel *>       groups();
    QList<const ParamGroupModel *> groups() const;
    QList<QObject *>               groupObjects() const;
    int                            groupCount() const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE ParamGroupModel *groupAt(int row) const;
    void copyValuesFrom(const IParams &other);

signals:
    void groupCountChanged();

protected:
    ParamGroupModel *addGroup(const QString &key, const QString &label, std::vector<ParamDefinition> params,
                              const QString &description = {}, bool enabled = true, int part_index = 0);
    void clearGroups();

private:
    std::vector<std::unique_ptr<ParamGroupModel>> groups_;
};

class MODEL_API ITrainParams : public IParams
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ITrainParams)
    QML_UNCREATABLE("ITrainParams is an abstract interface")

public:
    explicit ITrainParams(QObject *parent = nullptr);
    ~ITrainParams() override;

    virtual std::unique_ptr<ITrainParams> cloneTrainParams() const = 0;
};

class MODEL_API ITestParams : public IParams
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ITestParams)
    QML_UNCREATABLE("ITestParams is an abstract interface")

public:
    explicit ITestParams(QObject *parent = nullptr);
    ~ITestParams() override;

    virtual std::unique_ptr<ITestParams> cloneTestParams() const = 0;
};

MODEL_API QString paramEditorTypeName(ParamEditorType editor_type);

} // namespace dltool::model
