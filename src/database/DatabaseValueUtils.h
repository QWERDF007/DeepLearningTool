#pragma once

#include <QList>
#include <QString>
#include <QVariant>
#include <QVariantMap>

namespace dltool::database::detail {

struct NamedValue
{
    QString  name;
    QVariant value;
};

/** 参数表的单行值：组名 + 参数名 + 值。 */
struct ParamValue
{
    QString  group;   ///< 参数组名（如 network/training/augmentation）
    QString  name_en; ///< 参数名
    QVariant value;   ///< 参数值
};

/** 将嵌套参数映射（group -> {name: value}）展开为两级行列表。 */
QList<ParamValue> flattenParamValues(const QVariantMap &params);

/** 将两级行列表组装为嵌套参数映射（group -> {name: value}）。 */
QVariantMap unflattenParamValues(const QList<ParamValue> &values);

/** 推断参数值的类型名（bool/int/double/string）。 */
QString paramValueType(const QVariant &value);

/** 将参数值转为数据库文本（bool/int/double/string 的纯文本表示）。 */
QString paramValueText(const QVariant &value);

/** 按类型名将数据库文本还原为参数值。 */
QVariant paramValueFromText(const QString &type, const QString &text, QString *err_msg = nullptr);

QByteArray variantToJson(const QVariant &value, QString *err_msg = nullptr);
QVariant   jsonToVariant(const QByteArray &value, QString *err_msg = nullptr);

} // namespace dltool::database::detail
