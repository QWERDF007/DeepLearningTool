#include "../test_runner.h"

#include "TestFixture.h"

#include "database/DataBase.h"
#include "model/IModel.h"
#include "model/ModelManager.h"
#include "model/ModelTaskController.h"
#include "model/ModelRegistry.h"
#include "model/TaskManager.h"

#include <QTest>

using namespace dltool::model;
using namespace dltool::model::testsupport;

namespace {

constexpr int kControllerTestMethod = 902;

class ControllerTestModel final : public IModel
{
public:
    ControllerTestModel()
        : IModel(nullptr)
    {
    }

    int method() const override { return kControllerTestMethod; }
    QString frameworkName() const override { return QStringLiteral("controller-test"); }
    QString modelArchitecture() const override { return QStringLiteral("ControllerModel"); }
    QString typeName() const override { return QStringLiteral("ControllerTestModel"); }
    std::unique_ptr<IModel> clone() const override { return std::make_unique<ControllerTestModel>(); }
};

} // namespace

class ModelTaskControllerTest : public QObject
{
    Q_OBJECT

private slots:
    void validatesInputsAndTransitionsInternalTask()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        FrameworkDefinition framework;
        framework.method = kControllerTestMethod;
        framework.name   = QStringLiteral("controller-test");
        QVERIFY(registerFramework(kControllerTestMethod, framework));
        QVERIFY(registerModel(kControllerTestMethod, framework.name, QStringLiteral("ControllerModel"),
                              []() { return std::make_unique<ControllerTestModel>(); }));

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager                         model_manager(kControllerTestMethod, &database, nullptr);
        QString                              error;
        const auto record = model_manager.addModelRecord(QStringLiteral("Controller_model"), framework.name,
                                                         QStringLiteral("ControllerModel"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        TaskManager *task_manager = TaskManager::getInstance();
        QVERIFY(task_manager != nullptr);
        task_manager->clearTasks();

        ModelTaskController controller(kControllerTestMethod, fixture.rootPath(), &model_manager, nullptr,
                                       task_manager);
        QCOMPARE(controller.addModelTask({}, ModelTaskType::Train), -1);
        QCOMPARE(controller.addModelTask(record.uuid, ModelTaskType::Unknown), -1);
        QCOMPARE(controller.startModelTestTask(record.uuid, QStringLiteral("missing-task")), -1);
        QVERIFY(!controller.stopModelTask(record.uuid, ModelTaskType::Train));
        QVERIFY(!controller.deleteModelTask(record.uuid, ModelTaskType::Train));

        const int task_id = controller.startModelTask(record.uuid, ModelTaskType::Train);
        QVERIFY(task_id > 0);
        QTRY_VERIFY_WITH_TIMEOUT(task_manager->findTask(task_id) != nullptr
                                     && task_manager->findTask(task_id)->status == TaskManager::Running,
                                 2000);

        QVERIFY(controller.stopModelTask(record.uuid, ModelTaskType::Train));
        QCOMPARE(task_manager->findTask(task_id)->status, TaskManager::Stopped);
        QVERIFY(controller.deleteModelTask(record.uuid, ModelTaskType::Train));
        QVERIFY(task_manager->findTask(task_id) == nullptr);

        controller.shutdown();
        controller.shutdown();
        task_manager->clearTasks();
    }
};

REGISTER_TEST(ModelTaskControllerTest)

#include "test_ModelTaskController.moc"
