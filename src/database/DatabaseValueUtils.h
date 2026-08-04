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

QList<NamedValue> flattenValues(const QVariantMap &values);
QVariantMap       unflattenValues(const QList<NamedValue> &values);

QByteArray variantToJson(const QVariant &value, QString *err_msg = nullptr);
QVariant   jsonToVariant(const QByteArray &value, QString *err_msg = nullptr);

} // namespace dltool::database::detail

