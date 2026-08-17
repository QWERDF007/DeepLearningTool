#include "../test_runner.h"

#include "model/ModelParamDefs.h"

#include <QTest>

using namespace dltool::model;

class ModelParamDefsTest : public QObject
{
    Q_OBJECT

private slots:
    void makersFillTypeAndDefaults()
    {
        const ParamDefinition integer = makeIntegerParam("epochs", "轮数", 3, 1, 100, 1);
        QCOMPARE(integer.name_en, QStringLiteral("epochs"));
        QCOMPARE(integer.name_cn, QStringLiteral("轮数"));
        QCOMPARE(integer.value.toInt(), 3);
        QCOMPARE(integer.default_value.toInt(), 3);
        QCOMPARE(integer.value_type, QStringLiteral("int"));
        QCOMPARE(integer.display_type, QStringLiteral("spin"));
        QCOMPARE(integer.value_range, QVariantList({QVariant(1), QVariant(100), QVariant(1)}));

        const ParamDefinition slider = makeSliderParam("lr", "学习率", 0.01, 0.0001, 1.0, 0.001);
        QCOMPARE(slider.value_type, QStringLiteral("double"));
        QCOMPARE(slider.display_type, QStringLiteral("slider"));

        const ParamDefinition check = makeCheckParam("use_amp", "混合精度", true);
        QCOMPARE(check.value.toBool(), true);
        QCOMPARE(check.value_type, QStringLiteral("bool"));
        QCOMPARE(check.display_type, QStringLiteral("checkbox"));

        const ParamDefinition combo
            = makeComboParam("optimizer", "优化器", QStringLiteral("adam"),
                             QVariantList{QStringLiteral("adam"), QStringLiteral("sgd")});
        QCOMPARE(combo.value_type, QStringLiteral("string"));
        QCOMPARE(combo.display_type, QStringLiteral("combo"));
        QCOMPARE(combo.options.size(), 2);
    }

    void groupModelExposesParams()
    {
        std::vector<ParamDefinition> params;
        params.push_back(makeIntegerParam("epochs", "轮数", 3, 1, 100, 1));
        params.push_back(makeDoubleParam("lr", "学习率", 0.01, 0.0, 1.0, 0.01));
        params.push_back(makeCheckParam("use_amp", "混合精度", false));

        ParamGroupModel group(QStringLiteral("train"), QStringLiteral("训练"), {}, true, 0, std::move(params));
        QCOMPARE(group.rowCount(), 3);
        QCOMPARE(group.valueForName(QStringLiteral("epochs")).toInt(), 3);
        QCOMPARE(group.valueForName(QStringLiteral("lr")).toDouble(), 0.01);
        QCOMPARE(group.valueForName(QStringLiteral("use_amp")).toBool(), false);

        QVERIFY(group.setValueForName(QStringLiteral("epochs"), 9));
        QCOMPARE(group.valueForName(QStringLiteral("epochs")).toInt(), 9);
        QCOMPARE(group.valuesMap().value(QStringLiteral("epochs")).toInt(), 9);
        QVERIFY(!group.setValueForName(QStringLiteral("missing"), 1));
    }
};

REGISTER_TEST(ModelParamDefsTest)

#include "test_ModelParamDefs.moc"
