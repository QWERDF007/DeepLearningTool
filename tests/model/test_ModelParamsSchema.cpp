#include "../test_runner.h"

#include "model/ModelParamDefs.h"
#include "model/ModelParamsSchema.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTest>
#include <QTemporaryDir>

namespace {

class RuntimeRootGuard
{
public:
    explicit RuntimeRootGuard(const QString &root)
        : was_set_(qEnvironmentVariableIsSet("DLT_RUNTIME_ROOT"))
        , previous_value_(qgetenv("DLT_RUNTIME_ROOT"))
    {
        qputenv("DLT_RUNTIME_ROOT", root.toUtf8());
    }

    ~RuntimeRootGuard()
    {
        if (was_set_)
            qputenv("DLT_RUNTIME_ROOT", previous_value_);
        else
            qunsetenv("DLT_RUNTIME_ROOT");
    }

    RuntimeRootGuard(const RuntimeRootGuard &) = delete;
    RuntimeRootGuard &operator=(const RuntimeRootGuard &) = delete;

private:
    bool       was_set_;
    QByteArray previous_value_;
};

QTemporaryDir makeTemporaryRuntimeRoot()
{
    return QTemporaryDir(QDir(QCoreApplication::applicationDirPath()).filePath(
        QStringLiteral("model-params-schema-XXXXXX")));
}

bool writeConfig(const QString &runtime_root, const QString &framework, const QString &architecture,
                 const QByteArray &content, QString *path_out = nullptr)
{
    const QString directory = QDir(runtime_root).filePath(QStringLiteral("config/models/%1").arg(framework));
    if (!QDir().mkpath(directory))
        return false;

    const QString path = QDir(directory).filePath(architecture + QStringLiteral(".yaml"));
    QFile         file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    if (file.write(content) != content.size())
        return false;
    file.close();
    if (path_out != nullptr)
        *path_out = QFileInfo(path).absoluteFilePath();
    return true;
}

} // namespace

using namespace dltool::model;

class ModelParamsSchemaTest : public QObject
{
    Q_OBJECT

private slots:
    void parameterFactoriesSetTypesRangesAndDefaults()
    {
        const ParamDefinition integer = makeIntegerParam(QStringLiteral("epochs"), QStringLiteral("Epochs"), 10, 1,
                                                         100, 1);
        QCOMPARE(integer.name_en, QStringLiteral("epochs"));
        QCOMPARE(integer.value_type, QStringLiteral("int"));
        QCOMPARE(integer.value.toInt(), 10);
        QCOMPARE(integer.value_range, QVariantList({1, 100, 1}));

        const ParamDefinition slider = makeSliderParam(QStringLiteral("lr"), QStringLiteral("LR"), 0.1, 0.0, 1.0,
                                                        0.01);
        QCOMPARE(slider.display_type, QStringLiteral("slider"));
        QCOMPARE(slider.value.toDouble(), 0.1);
        const ParamDefinition check = makeCheckParam(QStringLiteral("enabled"), QStringLiteral("Enabled"), true);
        QCOMPARE(check.value_type, QStringLiteral("bool"));
        QVERIFY(check.value.toBool());
        const ParamDefinition combo = makeComboParam(QStringLiteral("mode"), QStringLiteral("Mode"), QStringLiteral("a"),
                                                     {QStringLiteral("a"), QStringLiteral("b")});
        QCOMPARE(combo.options, QVariantList({QStringLiteral("a"), QStringLiteral("b")}));
    }

    void missingOrInvalidSchemaReturnsSafeEmptyValueObject()
    {
        const ModelParamsSchema empty = loadModelParamsSchema({}, {});
        QVERIFY(empty.framework_name.isEmpty());
        QVERIFY(empty.train_groups.empty());
        const ModelParamsSchema missing
            = loadModelParamsSchema(QStringLiteral("missing-framework"), QStringLiteral("missing-model"));
        QCOMPARE(missing.framework_name, QStringLiteral("missing-framework"));
        QCOMPARE(missing.model_architecture, QStringLiteral("missing-model"));
        QVERIFY(missing.config_path.isEmpty());
        QVERIFY(missing.test_groups.empty());
    }

    void loadsDirectModelNodeFromIsolatedRuntimeRoot()
    {
        QTemporaryDir temp = makeTemporaryRuntimeRoot();
        QVERIFY(temp.isValid());
        RuntimeRootGuard runtime_root(temp.path());

        QString config_path;
        QVERIFY(writeConfig(
            temp.path(), QStringLiteral("fixture-framework"), QStringLiteral("DirectModel"),
            QByteArrayLiteral(
                "framework: fixture-framework\n"
                "model_architecture: DirectModel\n"
                "model_name: Direct Display\n"
                "method: Detection\n"
                "train_params:\n"
                "  - name_en: training\n"
                "    name_cn: Training\n"
                "    description: Main training settings\n"
                "    enabled: false\n"
                "    part_index: 2\n"
                "    params:\n"
                "      - name_en: epochs\n"
                "        name_cn: Epochs\n"
                "        value: 12\n"
                "        value_type: int\n"
                "        value_range: [1, 100, 1]\n"
                "        display_type: spin\n"
                "test_params:\n"
                "  - name_en: inference\n"
                "    params:\n"
                "      - name_en: threshold\n"
                "        value: 0.75\n"
                "        value_type: double\n"
                "        display_type: slider\n"),
            &config_path));

        const ModelParamsSchema schema
            = loadModelParamsSchema(QStringLiteral("fixture-framework"), QStringLiteral("DirectModel"));
        QCOMPARE(schema.framework_name, QStringLiteral("fixture-framework"));
        QCOMPARE(schema.model_architecture, QStringLiteral("DirectModel"));
        QCOMPARE(schema.model_name, QStringLiteral("Direct Display"));
        QCOMPARE(schema.method, QStringLiteral("Detection"));
        QCOMPARE(schema.config_path, config_path);
        QCOMPARE(schema.train_groups.size(), size_t(1));
        QCOMPARE(schema.test_groups.size(), size_t(1));

        const ParamGroupDefinition &train_group = schema.train_groups.front();
        QCOMPARE(train_group.name_en, QStringLiteral("training"));
        QCOMPARE(train_group.name_cn, QStringLiteral("Training"));
        QCOMPARE(train_group.description, QStringLiteral("Main training settings"));
        QVERIFY(!train_group.enabled);
        QCOMPARE(train_group.part_index, 2);
        QCOMPARE(train_group.params.size(), size_t(1));
        QCOMPARE(train_group.params.front().value.toInt(), 12);
        QCOMPARE(train_group.params.front().default_value.toInt(), 12);
        QCOMPARE(train_group.params.front().value_range, QVariantList({1, 100, 1}));
        QCOMPARE(schema.test_groups.front().params.front().value.toDouble(), 0.75);
        QCOMPARE(schema.test_groups.front().params.front().display_type, QStringLiteral("slider"));
    }

    void loadsNestedModelNodeByArchitectureName()
    {
        QTemporaryDir temp = makeTemporaryRuntimeRoot();
        QVERIFY(temp.isValid());
        RuntimeRootGuard runtime_root(temp.path());

        QVERIFY(writeConfig(
            temp.path(), QStringLiteral("fixture-framework"), QStringLiteral("NestedModel"),
            QByteArrayLiteral(
                "NestedModel:\n"
                "  framework: nested-framework\n"
                "  model_architecture: NestedArchitecture\n"
                "  model_name: Nested Display\n"
                "  method: Segmentation\n"
                "  train_params:\n"
                "    - name_en: network\n"
                "      params:\n"
                "        - name_en: depth\n"
                "          value: 4\n"
                "          value_type: int\n"
                "  test_params: []\n")));

        const ModelParamsSchema schema
            = loadModelParamsSchema(QStringLiteral("fixture-framework"), QStringLiteral("NestedModel"));
        QCOMPARE(schema.framework_name, QStringLiteral("nested-framework"));
        QCOMPARE(schema.model_architecture, QStringLiteral("NestedArchitecture"));
        QCOMPARE(schema.model_name, QStringLiteral("Nested Display"));
        QCOMPARE(schema.method, QStringLiteral("Segmentation"));
        QVERIFY(!schema.config_path.isEmpty());
        QCOMPARE(schema.train_groups.size(), size_t(1));
        QCOMPARE(schema.train_groups.front().params.front().value.toInt(), 4);
        QVERIFY(schema.test_groups.empty());
    }

    void invalidYamlReturnsSafeSchemaAndRestoresRuntimeRoot()
    {
        const QByteArray previous_value = qgetenv("DLT_RUNTIME_ROOT");
        const bool       was_set       = qEnvironmentVariableIsSet("DLT_RUNTIME_ROOT");

        {
            QTemporaryDir temp = makeTemporaryRuntimeRoot();
            QVERIFY(temp.isValid());
            RuntimeRootGuard runtime_root(temp.path());

            QVERIFY(writeConfig(temp.path(), QStringLiteral("fixture-framework"), QStringLiteral("BrokenModel"),
                                QByteArrayLiteral("framework: fixture-framework\ntrain_params: [\n")));
            const ModelParamsSchema schema
                = loadModelParamsSchema(QStringLiteral("fixture-framework"), QStringLiteral("BrokenModel"));
            QCOMPARE(schema.framework_name, QStringLiteral("fixture-framework"));
            QCOMPARE(schema.model_architecture, QStringLiteral("BrokenModel"));
            QCOMPARE(schema.model_name, QStringLiteral("BrokenModel"));
            QVERIFY(schema.config_path.isEmpty());
            QVERIFY(schema.train_groups.empty());
            QVERIFY(schema.test_groups.empty());
        }

        if (was_set)
            QCOMPARE(qgetenv("DLT_RUNTIME_ROOT"), previous_value);
        else
            QVERIFY(!qEnvironmentVariableIsSet("DLT_RUNTIME_ROOT"));
    }
};

REGISTER_TEST(ModelParamsSchemaTest)

#include "test_ModelParamsSchema.moc"
