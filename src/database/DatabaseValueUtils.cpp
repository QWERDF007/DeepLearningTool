#include "DatabaseValueUtils.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QMetaType>

namespace dltool::database::detail {

namespace {

bool setError(QString *err_msg, const QString &message)
{
    if (err_msg != nullptr)
        *err_msg = message;
    return false;
}

void flattenGroup(const QString &group_name, const QVariantMap &group, QList<ParamValue> &result)
{
    for (auto it = group.cbegin(); it != group.cend(); ++it)
    {
        if (it.key().trimmed().isEmpty())
            continue;
        result.push_back({group_name, it.key(), it.value()});
    }
}

bool parseTypeAndText(const QString &type, const QString &text, QVariant &result, QString *err_msg)
{
    const QString normalized = type.trimmed().toLower();
    if (normalized == QStringLiteral("bool") || normalized == QStringLiteral("boolean"))
    {
        result = text.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0
                 || text.compare(QStringLiteral("1")) == 0;
        return true;
    }
    if (normalized == QStringLiteral("int") || normalized == QStringLiteral("integer"))
    {
        bool ok = false;
        const qlonglong parsed = text.trimmed().toLongLong(&ok);
        if (!ok)
            return setError(err_msg, QString("int 参数值无效: %1").arg(text)), false;
        result = static_cast<int>(parsed);
        return true;
    }
    if (normalized == QStringLiteral("double") || normalized == QStringLiteral("float")
        || normalized == QStringLiteral("real"))
    {
        bool ok = false;
        const double parsed = text.trimmed().toDouble(&ok);
        if (!ok)
            return setError(err_msg, QString("double 参数值无效: %1").arg(text)), false;
        result = parsed;
        return true;
    }
    if (normalized == QStringLiteral("string"))
    {
        result = text;
        return true;
    }
    return setError(err_msg, QString("未知参数类型: %1").arg(type));
}

} // namespace

QList<ParamValue> flattenParamValues(const QVariantMap &params)
{
    QList<ParamValue> result;
    for (auto it = params.cbegin(); it != params.cend(); ++it)
    {
        const QVariant value = it.value();
        if (value.userType() == QMetaType::QVariantMap)
            flattenGroup(it.key(), value.toMap(), result);
        else if (!it.key().trimmed().isEmpty())
            result.push_back({it.key(), {}, value});
    }
    return result;
}

QVariantMap unflattenParamValues(const QList<ParamValue> &values)
{
    QVariantMap result;
    for (const ParamValue &entry : values)
    {
        if (entry.group.trimmed().isEmpty() || entry.name_en.trimmed().isEmpty())
            continue;
        QVariantMap group = result.value(entry.group).toMap();
        group.insert(entry.name_en, entry.value);
        result.insert(entry.group, group);
    }
    return result;
}

QString paramValueType(const QVariant &value)
{
    switch (value.userType())
    {
    case QMetaType::Bool:
        return QStringLiteral("bool");
    case QMetaType::Char:
    case QMetaType::SChar:
    case QMetaType::UChar:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::Long:
    case QMetaType::ULong:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        return QStringLiteral("int");
    case QMetaType::Float:
    case QMetaType::Double:
        return QStringLiteral("double");
    default:
        return QStringLiteral("string");
    }
}

QString paramValueText(const QVariant &value)
{
    if (value.userType() == QMetaType::Bool)
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    return value.toString();
}

QVariant paramValueFromText(const QString &type, const QString &text, QString *err_msg)
{
    QVariant result;
    if (!parseTypeAndText(type, text, result, err_msg))
        return {};
    return result;
}

QByteArray variantToJson(const QVariant &value, QString *err_msg)
{
    const QJsonValue json_value = QJsonValue::fromVariant(value);
    if (json_value.isUndefined())
        return setError(err_msg, QString("参数值无法转换为 JSON")), QByteArray();

    QJsonArray wrapper;
    wrapper.append(json_value);
    const QByteArray encoded = QJsonDocument(wrapper).toJson(QJsonDocument::Compact);
    if (encoded.size() < 2 || encoded.front() != '[' || encoded.back() != ']')
        return setError(err_msg, QString("参数值 JSON 编码失败")), QByteArray();
    return encoded.mid(1, encoded.size() - 2);
}

QVariant jsonToVariant(const QByteArray &value, QString *err_msg)
{
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray("[") + value + QByteArray("]"),
                                                             &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isArray() || document.array().size() != 1)
    {
        return setError(err_msg, QString("数据库中的 JSON 参数值无效")), QVariant();
    }
    return document.array().at(0).toVariant();
}

} // namespace dltool::database::detail
