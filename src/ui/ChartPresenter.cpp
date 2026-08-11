#include "ui/ChartPresenter.h"

#include <QJsonDocument>

namespace dltool::ui {

namespace {

QVariantMap jsonObjectCopy(const QVariant &value)
{
    const QVariantMap source = value.toMap();
    if (source.isEmpty() && value.isValid() && !value.isNull())
    {
        const QJsonDocument document = QJsonDocument::fromVariant(value);
        return document.isObject() ? document.toVariant().toMap() : QVariantMap{};
    }

    const QJsonDocument document = QJsonDocument::fromVariant(source);
    return document.isObject() ? document.toVariant().toMap() : QVariantMap{};
}

void setFontColor(QVariantMap &map, const QString &key, const QString &font_color)
{
    if (font_color.isEmpty())
        return;

    QVariantMap value = map.value(key).toMap();
    value.insert(QStringLiteral("fontColor"), font_color);
    map.insert(key, value);
}

void setAxisFontColor(QVariantMap &axis, const QString &font_color)
{
    if (font_color.isEmpty())
        return;

    QVariantMap ticks = axis.value(QStringLiteral("ticks")).toMap();
    ticks.insert(QStringLiteral("fontColor"), font_color);
    axis.insert(QStringLiteral("ticks"), ticks);

    QVariantMap scale_label = axis.value(QStringLiteral("scaleLabel")).toMap();
    scale_label.insert(QStringLiteral("fontColor"), font_color);
    axis.insert(QStringLiteral("scaleLabel"), scale_label);
}

void setAxesFontColor(QVariantMap &scales, const QString &key, const QString &font_color)
{
    if (font_color.isEmpty())
        return;

    QVariantList axes = scales.value(key).toList();
    for (int index = 0; index < axes.size(); ++index)
    {
        QVariantMap axis = axes.at(index).toMap();
        setAxisFontColor(axis, font_color);
        axes[index] = axis;
    }
    scales.insert(key, axes);
}

}

ChartPresenter::ChartPresenter(QObject *parent)
    : QObject(parent)
{
}

ChartPresenter::~ChartPresenter() = default;

QVariantMap ChartPresenter::prepareData(const QVariant &chart_data) const
{
    /**
     * @brief 保持 QVariantMap 结构交给 QML 转换，避免 JSON 深拷贝丢失嵌套值。
     */
    const QVariantMap result = chart_data.toMap();
    if (!result.isEmpty())
        return result;

    return {{QStringLiteral("labels"), QVariantList{}}, {QStringLiteral("datasets"), QVariantList{}}};
}

QVariantMap ChartPresenter::prepareOptions(const QVariant &options, const QString &font_color) const
{
    QVariantMap result = jsonObjectCopy(options);
    if (font_color.isEmpty())
        return result;

    QVariantMap legend = result.value(QStringLiteral("legend")).toMap();
    QVariantMap legend_labels = legend.value(QStringLiteral("labels")).toMap();
    legend_labels.insert(QStringLiteral("fontColor"), font_color);
    legend.insert(QStringLiteral("labels"), legend_labels);
    result.insert(QStringLiteral("legend"), legend);

    setFontColor(result, QStringLiteral("title"), font_color);
    setFontColor(result, QStringLiteral("tooltips"), font_color);

    QVariantMap tooltips = result.value(QStringLiteral("tooltips")).toMap();
    tooltips.insert(QStringLiteral("titleFontColor"), font_color);
    tooltips.insert(QStringLiteral("bodyFontColor"), font_color);
    tooltips.insert(QStringLiteral("footerFontColor"), font_color);
    result.insert(QStringLiteral("tooltips"), tooltips);

    QVariantMap scales = result.value(QStringLiteral("scales")).toMap();
    setAxesFontColor(scales, QStringLiteral("xAxes"), font_color);
    setAxesFontColor(scales, QStringLiteral("yAxes"), font_color);
    result.insert(QStringLiteral("scales"), scales);
    return result;
}

}
