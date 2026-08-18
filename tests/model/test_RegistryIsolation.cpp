#include "../test_runner.h"

#include "model/EvaluationEngineRegistry.h"
#include "model/EvaluationViewModelRegistry.h"
#include "model/DetectionEvaluationViewModel.h"
#include "model/IModel.h"
#include "model/ModelRegistry.h"

#include <QTest>

using namespace dltool::model;

namespace {

class RegistryTestModel final : public IModel
{
public:
    RegistryTestModel()
        : IModel(nullptr)
    {
    }

    int method() const override { return 901; }
    QString frameworkName() const override { return QStringLiteral("RegistryTestFramework"); }
    QString modelArchitecture() const override { return QStringLiteral("RegistryTestArchitecture"); }
    QString typeName() const override { return QStringLiteral("RegistryTestModel"); }
    std::unique_ptr<IModel> clone() const override { return std::make_unique<RegistryTestModel>(); }
};

class RegistryTestEngine final : public IEvaluationEngine
{
public:
    evaluation::Method method() const override { return evaluation::Method::Unknown; }

protected:
    bool computeInstanceCounts(const QMap<qint64, EvaluationImageData> &, const QMap<int, QString> &,
                               QMap<int, EvaluationCounts> &, EvaluationCounts &, QString *) override
    {
        return true;
    }
    bool computeImageCounts(const QMap<qint64, EvaluationImageData> &, EvaluationCounts &, QString *) override
    {
        return true;
    }
    bool buildEvents(const QMap<qint64, EvaluationImageData> &, QList<EvaluationInstanceRecord> &, QString *) override
    {
        return true;
    }
    QList<QVariantMap> buildCharts(const QMap<qint64, EvaluationImageData> &, const QMap<int, QString> &,
                                   const EvaluationCounts &, const EvaluationCounts &,
                                   const QMap<int, EvaluationCounts> &, const QMap<QString, qint64> &,
                                   const QList<EvaluationInstanceRecord> &, QString *) override
    {
        return {};
    }
    QVector<EvaluationConfusionCell> buildConfusionMatrix(const QMap<int, QString> &, const QMap<QString, qint64> &)
        override
    {
        return {};
    }
    bool hasConfusionMatrix() const override { return false; }
    QStringList chartKinds() const override { return {}; }
};

} // namespace

class RegistryIsolationTest : public QObject
{
    Q_OBJECT

private slots:
    void customEngineAndViewModelFactoriesAreIsolatedToThisProcess()
    {
        auto &engines = EvaluationEngineRegistry::instance();
        engines.registerEngine(evaluation::Method::Unknown, []() { return std::make_unique<RegistryTestEngine>(); });
        QVERIFY(dynamic_cast<RegistryTestEngine *>(engines.createEngine(evaluation::Method::Unknown).get()) != nullptr);

        auto &view_models = EvaluationViewModelRegistry::instance();
        view_models.registerViewModel(evaluation::Method::Unknown,
                                      [](QObject *parent) { return new DetectionEvaluationViewModel(parent); });
        QVERIFY(view_models.createViewModel(evaluation::Method::Unknown) != nullptr);
        QVERIFY(view_models.createViewModel(evaluation::Method::Detection) != nullptr);

        FrameworkDefinition framework;
        framework.name = QStringLiteral("RegistryTestFramework");
        framework.method = 901;
        framework.train_script = QStringLiteral("train.py");
        QVERIFY(registerFramework(901, framework));
        QVERIFY(registeredFrameworkNames(901).contains(framework.name));
        QVERIFY(registerModel(901, framework.name, QStringLiteral("RegistryTestArchitecture"),
                              []() { return std::make_unique<RegistryTestModel>(); }));
        QCOMPARE(registeredModelArchitectures(901, framework.name), QStringList({QStringLiteral("RegistryTestArchitecture")}));
        auto model = createRegisteredModel(901, framework.name, QStringLiteral("RegistryTestArchitecture"));
        QVERIFY(model != nullptr);
        QCOMPARE(model->typeName(), QStringLiteral("RegistryTestModel"));
        QVERIFY(createRegisteredModel(901, framework.name, QStringLiteral("missing")) == nullptr);
    }
};

REGISTER_TEST(RegistryIsolationTest)

#include "test_RegistryIsolation.moc"
