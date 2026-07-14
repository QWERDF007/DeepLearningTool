#pragma once

#include "dltool/model/Export.h"

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>
#include <QtQml>
#include <memory>
#include <vector>

namespace dltool::model {

/**
 * @brief 单个参数定义，描述一个超参数的名称、类型、默认值、控件类型等元信息
 */
struct MODEL_API ParamDefinition
{
    QString      name_en;                              ///< 英文名称
    QString      name_cn;                              ///< 中文名称
    QString      description;                          ///< 描述
    QVariant     value;                                ///< 当前值
    QVariant     default_value;                        ///< 默认值
    QString      value_type{QStringLiteral("string")}; ///< 值类型（int/double/bool/string）
    QVariantList value_range;                          ///< 值范围
    QString      control_type{QStringLiteral("text")}; ///< UI 控件类型（spin/slider/checkbox/combo/text）
    QStringList  options;                              ///< 下拉选项列表
    bool         enabled{true};                        ///< 是否启用
    QString      unit;                                 ///< 单位
};

/**
 * @brief 参数组模型，管理一组相关参数并提供 QAbstractListModel 接口供 QML 使用
 */
class MODEL_API ParamGroupModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ParamGroupModel)
    QML_UNCREATABLE("ParamGroupModel is owned by IParams")
    Q_PROPERTY(QString nameEn READ nameEn CONSTANT FINAL)
    Q_PROPERTY(QString nameCn READ nameCn CONSTANT FINAL)
    Q_PROPERTY(QString description READ description CONSTANT FINAL)
    Q_PROPERTY(bool enabled READ isEnabled CONSTANT FINAL)
    Q_PROPERTY(int partIndex READ partIndex CONSTANT FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role
    {
        NameEnRole = Qt::UserRole + 1,
        NameCnRole,
        DescriptionRole,
        ValueRole,
        DefaultValueRole,
        ValueTypeRole,
        ValueRangeRole,
        ControlTypeRole,
        EnabledRole,
        OptionsRole,
        UnitRole,
    };
    Q_ENUM(Role)

    /**
     * @brief 构造参数组模型
     * @param parent 父对象
     */
    explicit ParamGroupModel(QObject *parent = nullptr);

    /**
     * @brief 构造参数组模型
     * @param name_en 英文名称
     * @param name_cn 中文名称
     * @param description 描述
     * @param enabled 是否启用
     * @param part_index 分组索引
     * @param params 参数定义列表
     * @param parent 父对象
     */
    ParamGroupModel(QString name_en, QString name_cn, QString description, bool enabled, int part_index,
                    std::vector<ParamDefinition> params, QObject *parent = nullptr);
    ~ParamGroupModel() override;

    /**
     * @brief 获取英文名称
     * @return 英文名称
     */
    QString nameEn() const;

    /**
     * @brief 获取中文名称
     * @return 中文名称
     */
    QString nameCn() const;

    /**
     * @brief 获取描述
     * @return 描述文本
     */
    QString description() const;

    /**
     * @brief 获取是否启用
     * @return 启用返回 true
     */
    bool isEnabled() const;

    /**
     * @brief 获取分组索引
     * @return 分组索引
     */
    int partIndex() const;

    /**
     * @brief 获取参数数量
     * @return 参数个数
     */
    int count() const;

    /**
     * @brief 获取行数
     * @param parent 父索引
     * @return 行数
     */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    /**
     * @brief 获取指定索引的数据
     * @param index 模型索引
     * @param role 数据角色
     * @return 数据值
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    /**
     * @brief 设置指定索引的数据
     * @param index 模型索引
     * @param value 新值
     * @param role 数据角色
     * @return 设置成功返回 true
     */
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

    /**
     * @brief 获取指定索引的 item 标志
     * @param index 模型索引
     * @return item 标志
     */
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    /**
     * @brief 获取角色名称映射
     * @return 角色名称哈希表
     */
    QHash<int, QByteArray> roleNames() const override;

    /**
     * @brief 设置指定行的参数值
     * @param row 行号
     * @param value 新值
     * @return 设置成功返回 true
     */
    Q_INVOKABLE bool setValue(int row, const QVariant &value);

    /**
     * @brief 获取指定行的参数值
     * @param row 行号
     * @return 参数值
     */
    Q_INVOKABLE QVariant valueAt(int row) const;

    /**
     * @brief 根据英文名获取参数值
     * @param name_en 参数英文名
     * @return 参数值
     */
    Q_INVOKABLE QVariant valueForName(const QString &name_en) const;

    /**
     * @brief 获取所有参数的键值对
     * @return 参数名-值映射
     */
    Q_INVOKABLE QVariantMap valuesMap() const;

    /**
     * @brief 从另一个参数组复制值
     * @param other 源参数组
     */
    void copyValuesFrom(const ParamGroupModel &other);

    /**
     * @brief 根据键值对批量设置参数值
     * @param values 参数名-值映射
     * @return 有变更返回 true
     */
    bool setValuesMap(const QVariantMap &values);

signals:
    void countChanged();
    void valueChanged(const QString &name_en, const QVariant &value);

private:
    /**
     * @brief 获取参数当前有效值
     * @param param 参数定义
     * @return 当前值
     */
    QVariant currentValue(const ParamDefinition &param) const;

    /**
     * @brief 根据英文名查找参数索引
     * @param name_en 参数英文名
     * @return 索引，未找到返回 -1
     */
    int indexOfParam(const QString &name_en) const;

    QString                      name_en_;
    QString                      name_cn_;
    QString                      description_;
    bool                         enabled_{true};
    int                          part_index_{0};
    std::vector<ParamDefinition> params_;
};

/**
 * @brief 参数集合抽象接口，管理多个参数组并提供 QAbstractListModel 接口
 */
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
        GroupNameEnRole = Qt::UserRole + 1,
        GroupNameCnRole,
        GroupDescriptionRole,
        GroupEnabledRole,
        GroupPartIndexRole,
        GroupCountRole,
        GroupModelRole,
    };
    Q_ENUM(Role)

    /**
     * @brief 构造参数集合
     * @param parent 父对象
     */
    explicit IParams(QObject *parent = nullptr);
    ~IParams() override;

    /**
     * @brief 获取类型名称
     * @return 类型名称
     */
    virtual QString typeName() const = 0;

    /**
     * @brief 获取所有参数组（可修改）
     * @return 参数组列表
     */
    QList<ParamGroupModel *> groups();

    /**
     * @brief 获取所有参数组（只读）
     * @return 参数组列表
     */
    QList<const ParamGroupModel *> groups() const;

    /**
     * @brief 获取所有参数组对象
     * @return 参数组 QObject 列表
     */
    QList<QObject *> groupObjects() const;

    /**
     * @brief 获取参数组数量
     * @return 参数组个数
     */
    int groupCount() const;

    /**
     * @brief 获取行数
     * @param parent 父索引
     * @return 行数
     */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    /**
     * @brief 获取指定索引的数据
     * @param index 模型索引
     * @param role 数据角色
     * @return 数据值
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    /**
     * @brief 获取角色名称映射
     * @return 角色名称哈希表
     */
    QHash<int, QByteArray> roleNames() const override;

    /**
     * @brief 获取指定行的参数组
     * @param row 行号
     * @return 参数组模型指针
     */
    Q_INVOKABLE ParamGroupModel *groupAt(int row) const;

    /**
     * @brief 获取所有参数组的键值对
     * @return 参数值映射
     */
    QVariantMap valuesMap() const;

    /**
     * @brief 从另一个参数集合复制值
     * @param other 源参数集合
     */
    void copyValuesFrom(const IParams &other);

    /**
     * @brief 根据键值对批量设置参数值
     * @param values 参数值映射
     * @return 有变更返回 true
     */
    bool setValuesMap(const QVariantMap &values);

signals:
    void groupCountChanged();

protected:
    /**
     * @brief 添加一个参数组
     * @param name_en 英文名称
     * @param name_cn 中文名称
     * @param params 参数定义列表
     * @param description 描述
     * @param enabled 是否启用
     * @param part_index 分组索引
     * @return 新创建的参数组模型指针
     */
    ParamGroupModel *addGroup(const QString &name_en, const QString &name_cn, std::vector<ParamDefinition> params,
                              const QString &description = {}, bool enabled = true, int part_index = 0);

    /**
     * @brief 清除所有参数组
     */
    void clearGroups();

private:
    std::vector<std::unique_ptr<ParamGroupModel>> groups_;
};

/**
 * @brief 训练参数抽象接口
 */
class MODEL_API ITrainParams : public IParams
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ITrainParams)
    QML_UNCREATABLE("ITrainParams is an abstract interface")

public:
    /**
     * @brief 构造训练参数
     * @param parent 父对象
     */
    explicit ITrainParams(QObject *parent = nullptr);
    ~ITrainParams() override;

    /**
     * @brief 克隆训练参数
     * @return 克隆后的训练参数实例
     */
    virtual std::unique_ptr<ITrainParams> cloneTrainParams() const = 0;
};

/**
 * @brief 测试参数抽象接口
 */
class MODEL_API ITestParams : public IParams
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ITestParams)
    QML_UNCREATABLE("ITestParams is an abstract interface")

public:
    /**
     * @brief 构造测试参数
     * @param parent 父对象
     */
    explicit ITestParams(QObject *parent = nullptr);
    ~ITestParams() override;

    /**
     * @brief 克隆测试参数
     * @return 克隆后的测试参数实例
     */
    virtual std::unique_ptr<ITestParams> cloneTestParams() const = 0;
};

} // namespace dltool::model
