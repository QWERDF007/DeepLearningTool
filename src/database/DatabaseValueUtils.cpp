#include "DatabaseValueUtils.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>

namespace dltool::database::detail {

namespace {

void flattenMap(const QVariantMap &map, const QString &prefix, QList<NamedValue> &result)
{
    for (auto it = map.cbegin(); it != map.cend(); ++it)
    {
        const QString name = prefix.isEmpty() ? it.key() : prefix + QLatin1Char('.') + it.key();
        const QVariant value = it.value();
        if (value.userType() == QMetaType::QVariantMap)
        {
            const QVariantMap nested = value.toMap();
            if (nested.isEmpty())
                result.push_back({name, value});
            else
                flattenMap(nested, name, result);
        }
        else
        {
            result.push_back({name, value});
        }
    }
}

void insertValue(QVariantMap &map, const QStringList &parts, int index, const QVariant &value)
{
    if (index < 0 || index >= parts.size())
        return;
    if (index == parts.size() - 1)
    {
        map.insert(parts.at(index), value);
        return;
    }

    QVariantMap nested = map.value(parts.at(index)).toMap();
    insertValue(nested, parts, index + 1, value);
    map.insert(parts.at(index), nested);
}

bool setError(QString *err_msg, const QString &message)
{
    if (err_msg != nullptr)
        *err_msg = message;
    return false;
}

} // namespace

QList<NamedValue> flattenValues(const QVariantMap &values)
{
    QList<NamedValue> result;
    flattenMap(values, {}, result);
    return result;
}

QVariantMap unflattenValues(const QList<NamedValue> &values)
{
    QVariantMap result;
    for (const NamedValue &entry : values)
    {
        const QString name = entry.name.trimmed();
        if (name.isEmpty())
            continue;
        const QStringList parts = name.split(QLatin1Char('.'), Qt::SkipEmptyParts);
        if (!parts.isEmpty())
            insertValue(result, parts, 0, entry.value);
    }
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
