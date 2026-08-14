#include "ui/Utils.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>
#include <QVariant>
#include <algorithm>
#include <cmath>

namespace dltool::ui {

namespace {

QVariantList toVariantList(const QVariant &value)
{
    if (!value.isValid() || value.isNull())
    {
        return {};
    }

    if (value.canConvert<QVariantList>())
    {
        return value.toList();
    }

    if (value.canConvert<QStringList>())
    {
        QVariantList result;
        for (const QString &entry : value.toStringList())
        {
            result.append(entry);
        }
        return result;
    }

    return {};
}

QStringList recommendedLabelColorList()
{
    return {
        QStringLiteral("#e6194b"), QStringLiteral("#3cb44b"), QStringLiteral("#4363d8"),
        QStringLiteral("#f58231"), QStringLiteral("#911eb4"), QStringLiteral("#46f0f0"),
        QStringLiteral("#f032e6"), QStringLiteral("#bcf60c"), QStringLiteral("#fabebe"),
        QStringLiteral("#008080"), QStringLiteral("#e6beff"), QStringLiteral("#9a6324"),
        QStringLiteral("#fffac8"), QStringLiteral("#800000"), QStringLiteral("#aaffc3"),
        QStringLiteral("#808000"), QStringLiteral("#ffd8b1"), QStringLiteral("#000075"),
        QStringLiteral("#808080"), QStringLiteral("#ffe119"), QStringLiteral("#469990"),
        QStringLiteral("#dcbeff"), QStringLiteral("#a9a9a9"), QStringLiteral("#ff6f61"),
    };
}

QString normalizedColorName(const QVariant &color)
{
    const QColor parsed(color.toString().trimmed());
    return parsed.isValid() ? parsed.name(QColor::HexRgb).toLower() : QString();
}

QString generatedLabelColor(const int index)
{
    constexpr double kGoldenRatioConjugate = 0.618033988749895;
    const double     hue                   = std::fmod(index * kGoldenRatioConjugate, 1.0);
    return QColor::fromHslF(hue, 0.72, 0.5).name(QColor::HexRgb);
}

int decimalCount(const QVariant &value)
{
    if (!value.isValid() || value.isNull())
    {
        return 0;
    }

    QString text;
    const int type_id = value.metaType().id();
    if (type_id == QMetaType::Double || type_id == QMetaType::Float)
    {
        // Avoid exposing binary floating-point artifacts such as 0.41999999999999998.
        text = QString::number(value.toDouble(), 'g', 15).toLower();
    }
    else
    {
        text = value.toString().trimmed().toLower();
    }

    if (text.isEmpty())
    {
        text = QString::number(value.toDouble(), 'g', 15).toLower();
    }

    const int exponent_index = text.indexOf(QLatin1Char('e'));
    if (exponent_index >= 0)
    {
        const QString mantissa = text.left(exponent_index);
        bool          ok       = false;
        const int     exponent = text.mid(exponent_index + 1).toInt(&ok);
        if (ok)
        {
            const int dot_index         = mantissa.indexOf(QLatin1Char('.'));
            const int mantissa_decimals = dot_index >= 0 ? mantissa.size() - dot_index - 1 : 0;
            return exponent < 0 ? mantissa_decimals - exponent : std::max(0, mantissa_decimals - exponent);
        }
    }

    const int dot_index = text.indexOf(QLatin1Char('.'));
    return dot_index >= 0 ? text.size() - dot_index - 1 : 0;
}

} // namespace

Utils::Utils(QObject *parent)
    : QObject(parent)
{
}

Utils::~Utils() {}

QColor Utils::withOpacity(const QColor &color, qreal opacity) const
{
    int alpha = qRound(opacity * 255) & 0xff;
    return QColor::fromRgba((alpha << 24) | (color.rgba() & 0xffffff));
}

QString Utils::getCleanPath(const QString &path) const
{
#ifdef _WIN32
    return path.sliced(8);
#else
    return path.sliced(7);
#endif
}

void Utils::openInFileExplorer(const QString &path)
{
    // QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    const QString explorer = "explorer";
    QStringList   param{"/select,", QDir::toNativeSeparators(path)};
    QProcess::startDetached(explorer, param);
}

QString Utils::stringValue(const QVariant &value) const
{
    return value.isValid() && !value.isNull() ? value.toString() : QString();
}

double Utils::numberValue(const QVariant &value, const double fallback_value) const
{
    if (!value.isValid() || value.isNull())
    {
        return fallback_value;
    }

    bool         ok     = false;
    const double number = value.toDouble(&ok);
    return ok ? number : fallback_value;
}

bool Utils::boolValue(const QVariant &value, const bool fallback_value) const
{
    if (!value.isValid() || value.isNull())
    {
        return fallback_value;
    }

    const QString text = value.toString().trimmed().toLower();
    if (text == QStringLiteral("true"))
    {
        return true;
    }
    if (text == QStringLiteral("false"))
    {
        return false;
    }

    bool         ok     = false;
    const double number = text.toDouble(&ok);
    if (ok)
    {
        return number != 0.0;
    }

    if (value.metaType().id() == QMetaType::QString)
    {
        return fallback_value;
    }

    if (value.canConvert<bool>())
    {
        return value.toBool();
    }
    return fallback_value;
}

bool Utils::isIntegerValueType(const QString &value_type) const
{
    const QString normalized = value_type.trimmed().toLower();
    return normalized == QStringLiteral("int") || normalized == QStringLiteral("integer")
        || normalized == QStringLiteral("long");
}

QVariant Utils::valueRangeAt(const QVariant &value_range, const int index, const QVariant &fallback_value) const
{
    const QVariantList range = toVariantList(value_range);
    return index >= 0 && index < range.size() ? range.at(index) : fallback_value;
}

int Utils::paramDecimals(const QString &value_type, const QVariant &value_range, const QVariant &value,
                         const QVariant &default_value) const
{
    if (isIntegerValueType(value_type))
    {
        return 0;
    }

    int                result = std::max(decimalCount(value), decimalCount(default_value));
    const QVariantList range  = toVariantList(value_range);
    for (const QVariant &entry : range)
    {
        result = std::max(result, decimalCount(entry));
    }
    return result;
}

QVariantList Utils::recommendedLabelColors() const
{
    QVariantList result;
    const auto   colors = recommendedLabelColorList();
    result.reserve(colors.size());
    for (const QString &color : colors)
    {
        result.append(color);
    }
    return result;
}

QString Utils::nextRecommendedColor(const QVariant &used_colors) const
{
    const QVariantList used_list = toVariantList(used_colors);
    QSet<QString>      used;
    for (const QVariant &color : used_list)
    {
        const QString normalized = normalizedColorName(color);
        if (!normalized.isEmpty())
        {
            used.insert(normalized);
        }
    }

    for (const QString &color : recommendedLabelColorList())
    {
        const QString normalized = normalizedColorName(color);
        if (!used.contains(normalized))
        {
            return color;
        }
    }

    const int offset = used.size();
    for (int i = 0; i < 256; ++i)
    {
        const QString color      = generatedLabelColor(offset + i);
        const QString normalized = normalizedColorName(color);
        if (!used.contains(normalized))
        {
            return color;
        }
    }

    return QStringLiteral("#808080");
}

} // namespace dltool::ui
