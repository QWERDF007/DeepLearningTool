#include "../test_runner.h"

#include "TestFixture.h"

#include "data/DataManager.h"
#include "database/DataBase.h"
#include "database/ModelTaskDataBase.h"
#include "model/IModel.h"
#include "model/ModelManager.h"
#include "model/ModelStorageService.h"
#include "model/ModelTaskController.h"
#include "model/ModelRegistry.h"
#include "model/TaskManager.h"
#include "settings/GlobalSettings.h"
#include "settings/SettingsKeys.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QTextStream>

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
    void initTestCase()
    {
        FrameworkDefinition framework;
        framework.method = kControllerTestMethod;
        framework.name   = QStringLiteral("controller-test");
        framework.weight_extensions = {QStringLiteral(".pt")};
        registerFramework(kControllerTestMethod, framework);
        registerModel(kControllerTestMethod, framework.name, QStringLiteral("ControllerModel"),
                      []() { return std::make_unique<ControllerTestModel>(); });

        FrameworkDefinition external_framework;
        external_framework.method = kControllerTestMethod;
        external_framework.name   = QStringLiteral("controller-external");
        external_framework.task_capabilities.push_back({ModelTaskType::Train, QStringLiteral("mock_train.py")});
        external_framework.task_capabilities.push_back({ModelTaskType::Test, QStringLiteral("mock_test.py")});
        external_framework.weight_extensions = {QStringLiteral(".pt")};
        registerFramework(kControllerTestMethod, external_framework);
        registerModel(kControllerTestMethod, external_framework.name, QStringLiteral("ControllerModel"),
                      []() { return std::make_unique<ControllerTestModel>(); });
    }

    void validatesInputsAndTransitionsInternalTask()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager                         model_manager(kControllerTestMethod, &database, nullptr);
        QString                              error;
        const auto record = model_manager.addModelRecord(QStringLiteral("Controller_model"), QStringLiteral("controller-test"),
                                                         QStringLiteral("ControllerModel"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        TaskManager task_manager_instance;
        TaskManager *task_manager = &task_manager_instance;

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

        QTRY_VERIFY_WITH_TIMEOUT(
            model_manager.modelRecordForUuid(record.uuid)
                    .value(QStringLiteral("extra_data"))
                    .toMap()
                    .value(QStringLiteral("train"))
                    .toMap()
                    .value(QStringLiteral("elapsed"))
                    .toString()
                != QStringLiteral("00:00:00"),
            3000);
        const QVariantMap persisted_train
            = model_manager.modelRecordForUuid(record.uuid)
                  .value(QStringLiteral("extra_data"))
                  .toMap()
                  .value(QStringLiteral("train"))
                  .toMap();
        QVERIFY(persisted_train.value(QStringLiteral("elapsed")).toString() != QStringLiteral("00:00:00"));

        QVERIFY(controller.stopModelTask(record.uuid, ModelTaskType::Train));
        QCOMPARE(task_manager->findTask(task_id)->status, TaskManager::Stopped);
        const QString stopped_elapsed = model_manager.modelRecordForUuid(record.uuid)
                                            .value(QStringLiteral("extra_data"))
                                            .toMap()
                                            .value(QStringLiteral("train"))
                                            .toMap()
                                            .value(QStringLiteral("elapsed"))
                                            .toString();
        QVERIFY(!stopped_elapsed.isEmpty());
        QVERIFY(controller.deleteModelTask(record.uuid, ModelTaskType::Train));
        QVERIFY(task_manager->findTask(task_id) == nullptr);

        const int pending_task_id = controller.addModelTask(record.uuid, ModelTaskType::BoxToMask);
        QVERIFY(pending_task_id > 0);
        QCOMPARE(task_manager->findTask(pending_task_id)->status, TaskManager::Pending);
        QVERIFY(controller.stopModelTask(record.uuid, ModelTaskType::BoxToMask));
        QCOMPARE(task_manager->findTask(pending_task_id)->status, TaskManager::Stopped);
        QVERIFY(controller.deleteModelTask(record.uuid, ModelTaskType::BoxToMask));

        controller.shutdown();
        controller.shutdown();
        task_manager->clearTasks();

        ModelManager reloaded_model_manager(kControllerTestMethod, &database, nullptr);
        QCOMPARE(reloaded_model_manager.modelRecordForUuid(record.uuid)
                     .value(QStringLiteral("extra_data"))
                     .toMap()
                     .value(QStringLiteral("train"))
                     .toMap()
                     .value(QStringLiteral("elapsed"))
                     .toString(),
                 stopped_elapsed);
    }

    void reportsFinishedThenNonZeroExitFails()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager                         model_manager(kControllerTestMethod, &database, nullptr);
        QString                              error;
        const auto record = model_manager.addModelRecord(QStringLiteral("Exit1_model"), QStringLiteral("controller-test"),
                                                         QStringLiteral("ControllerModel"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        TaskManager task_manager;
        ModelTaskController controller(kControllerTestMethod, fixture.rootPath(), &model_manager, nullptr, &task_manager);

        const int task_id = task_manager.addTask(record.uuid, record.name, ModelTaskType::Train);
        QVERIFY(task_id > 0);
        QVERIFY(task_manager.startTask(task_id));
        QCOMPARE(task_manager.findTask(task_id)->status, TaskManager::Running);

        const TaskIdentity identity = task_manager.findTask(task_id)->identity;
        QVERIFY(identity.isValid());

        // 脚本通过 TCP 上报 finished 消息
        TaskMessage msg;
        msg.identity = identity;
        msg.type     = TaskMessageType::Status;
        msg.status   = TaskProtocolStatus::Finished;
        msg.progress = 100;
        QMetaObject::invokeMethod(&task_manager, "handleTaskMessage", Qt::DirectConnection, Q_ARG(TaskMessage, msg));

        // 协议 finished 消息只更新进度，终态必须等待真实进程退出
        QCOMPARE(task_manager.findTask(task_id)->status, TaskManager::Running);
        QCOMPARE(task_manager.findTask(task_id)->progress, 100);

        // 真实进程非零退出（退出码 1）
        QMetaObject::invokeMethod(&controller, "handleExternalTaskFinished", Qt::DirectConnection,
                                  Q_ARG(TaskIdentity, identity), Q_ARG(int, 1), Q_ARG(bool, true), Q_ARG(bool, false));

        // 验收条件 1: 先上报 finished 后非零退出必须失败
        const TaskManager::Task *task = task_manager.findTask(task_id);
        QVERIFY(task != nullptr);
        QCOMPARE(task->status, TaskManager::Failed);

        const QVariantMap extra = model_manager.modelRecordForUuid(record.uuid).value(QStringLiteral("extra_data")).toMap();
        const QVariantMap train_section = extra.value(QStringLiteral("train")).toMap();
        QCOMPARE(train_section.value(QStringLiteral("status")).toString(), QStringLiteral("failed"));
        QCOMPARE(train_section.value(QStringLiteral("started")).toBool(), false);
    }

    void zeroExitCodeWithoutArtifactsFails()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager                         model_manager(kControllerTestMethod, &database, nullptr);
        QString                              error;
        const auto record = model_manager.addModelRecord(QStringLiteral("NoArtifact_model"), QStringLiteral("controller-test"),
                                                         QStringLiteral("ControllerModel"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        TaskManager task_manager;
        ModelTaskController controller(kControllerTestMethod, fixture.rootPath(), &model_manager, nullptr, &task_manager);

        const int task_id = task_manager.addTask(record.uuid, record.name, ModelTaskType::Train);
        QVERIFY(task_id > 0);
        QVERIFY(task_manager.startTask(task_id));
        QCOMPARE(task_manager.findTask(task_id)->status, TaskManager::Running);

        const TaskIdentity identity = task_manager.findTask(task_id)->identity;
        QVERIFY(identity.isValid());

        // 确保权重目录不存在任何文件
        ModelStorageService storage(fixture.rootPath());
        const QString weights_dir = storage.trainWeightsPath(record.name);
        if (QDir(weights_dir).exists())
            QDir(weights_dir).removeRecursively();

        // 进程零退出码退出，但无权重产物
        QMetaObject::invokeMethod(&controller, "handleExternalTaskFinished", Qt::DirectConnection,
                                  Q_ARG(TaskIdentity, identity), Q_ARG(int, 0), Q_ARG(bool, true), Q_ARG(bool, false));

        // 验收条件 1: 零退出码缺产物不能成功
        const TaskManager::Task *task = task_manager.findTask(task_id);
        QVERIFY(task != nullptr);
        QCOMPARE(task->status, TaskManager::Failed);

        const QVariantMap extra = model_manager.modelRecordForUuid(record.uuid).value(QStringLiteral("extra_data")).toMap();
        const QVariantMap train_section = extra.value(QStringLiteral("train")).toMap();
        QCOMPARE(train_section.value(QStringLiteral("status")).toString(), QStringLiteral("failed"));
        QCOMPARE(train_section.value(QStringLiteral("started")).toBool(), false);
    }

    void zeroExitCodeWithValidArtifactsSucceeds()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager                         model_manager(kControllerTestMethod, &database, nullptr);
        QString                              error;
        const auto record = model_manager.addModelRecord(QStringLiteral("ValidArtifact_model"), QStringLiteral("controller-test"),
                                                         QStringLiteral("ControllerModel"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        TaskManager task_manager;
        ModelTaskController controller(kControllerTestMethod, fixture.rootPath(), &model_manager, nullptr, &task_manager);

        const int task_id = task_manager.addTask(record.uuid, record.name, ModelTaskType::Train);
        QVERIFY(task_id > 0);
        QVERIFY(task_manager.startTask(task_id));
        QCOMPARE(task_manager.findTask(task_id)->status, TaskManager::Running);

        const TaskIdentity identity = task_manager.findTask(task_id)->identity;
        QVERIFY(identity.isValid());

        // 创建有效权重文件
        ModelStorageService storage(fixture.rootPath());
        const QString weights_dir = storage.trainWeightsPath(record.name);
        QVERIFY(QDir().mkpath(weights_dir));
        QFile weight_file(QDir(weights_dir).filePath(QStringLiteral("best.pt")));
        QVERIFY(weight_file.open(QIODevice::WriteOnly));
        weight_file.write("simulated_weights");
        weight_file.close();

        // 进程正常 0 退出
        QMetaObject::invokeMethod(&controller, "handleExternalTaskFinished", Qt::DirectConnection,
                                  Q_ARG(TaskIdentity, identity), Q_ARG(int, 0), Q_ARG(bool, true), Q_ARG(bool, false));

        // 成功终态
        const TaskManager::Task *task = task_manager.findTask(task_id);
        QVERIFY(task != nullptr);
        QCOMPARE(task->status, TaskManager::Finished);

        const QVariantMap extra = model_manager.modelRecordForUuid(record.uuid).value(QStringLiteral("extra_data")).toMap();
        const QVariantMap train_section = extra.value(QStringLiteral("train")).toMap();
        QCOMPARE(train_section.value(QStringLiteral("status")).toString(), QStringLiteral("finished"));
        QCOMPARE(train_section.value(QStringLiteral("started")).toBool(), false);
        QCOMPARE(train_section.value(QStringLiteral("progress")).toInt(), 100);
    }

    void testTaskRequiresValidPredictionsToSucceed()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager                         model_manager(kControllerTestMethod, &database, nullptr);
        QString                              error;
        const auto record = model_manager.addModelRecord(QStringLiteral("TestArtifact_model"), QStringLiteral("controller-test"),
                                                         QStringLiteral("ControllerModel"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        ModelTestTaskRepository repo(fixture.rootPath());
        repo.setProjectDatabasePath(fixture.projectDatabasePath());
        ModelTestTaskDefinition test_def;
        QVERIFY2(repo.createTask(record.name, record.uuid, QStringLiteral("Test Task 1"), {}, {}, test_def, &error),
                 qPrintable(error));

        TaskManager task_manager;
        ModelTaskController controller(kControllerTestMethod, fixture.rootPath(), &model_manager, nullptr, &task_manager);

        const int task_id = task_manager.addTask(record.uuid, record.name, ModelTaskType::Test,
                                                 test_def.uuid, test_def.name);
        QVERIFY(task_id > 0);
        QVERIFY(task_manager.startTask(task_id));
        QCOMPARE(task_manager.findTask(task_id)->status, TaskManager::Running);
        const TaskIdentity identity = task_manager.findTask(task_id)->identity;

        // 子情况 1: 零退出码但无预测产物 -> 失败
        QMetaObject::invokeMethod(&controller, "handleExternalTaskFinished", Qt::DirectConnection,
                                  Q_ARG(TaskIdentity, identity), Q_ARG(int, 0), Q_ARG(bool, true), Q_ARG(bool, false));
        QCOMPARE(task_manager.findTask(task_id)->status, TaskManager::Failed);

        // 重启任务为 Running，并创建有效预测文件
        QVERIFY(task_manager.startTask(task_id));
        QCOMPARE(task_manager.findTask(task_id)->status, TaskManager::Running);
        const TaskIdentity restarted_identity = task_manager.findTask(task_id)->identity;

        ModelStorageService storage(fixture.rootPath());
        const QString pred_dir = storage.testTaskPredictionPath(record.name, test_def.directory_name);
        QVERIFY(QDir().mkpath(pred_dir));
        QFile pred_file(QDir(pred_dir).filePath(QStringLiteral("1.tiff")));
        QVERIFY(pred_file.open(QIODevice::WriteOnly));
        pred_file.write("prediction_bytes");
        pred_file.close();

        // 子情况 2: 零退出码且有预测产物 -> 成功
        QMetaObject::invokeMethod(&controller, "handleExternalTaskFinished", Qt::DirectConnection,
                                  Q_ARG(TaskIdentity, restarted_identity), Q_ARG(int, 0), Q_ARG(bool, true), Q_ARG(bool, false));
        QCOMPARE(task_manager.findTask(task_id)->status, TaskManager::Finished);
    }

    void stopWaitsForProcessExitAndDuplicateStopPublishesTerminalOnce()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager                         model_manager(kControllerTestMethod, &database, nullptr);
        QString                              error;
        const auto record = model_manager.addModelRecord(QStringLiteral("Stop_model"), QStringLiteral("controller-external"),
                                                         QStringLiteral("ControllerModel"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        TaskManager task_manager;
        ModelTaskController controller(kControllerTestMethod, fixture.rootPath(), &model_manager, nullptr, &task_manager);

        const int task_id = task_manager.addTask(record.uuid, record.name, ModelTaskType::Train);
        QVERIFY(task_id > 0);
        QVERIFY(task_manager.markTaskRunning(task_id));
        QCOMPARE(task_manager.findTask(task_id)->status, TaskManager::Running);
        const TaskIdentity identity = task_manager.findTask(task_id)->identity;

        // 首次停止
        QVERIFY(controller.stopModelTask(record.uuid, ModelTaskType::Train));
        QCOMPARE(task_manager.findTask(task_id)->status, TaskManager::Stopping);

        // 重复停止必须被安全忽略且返回 false，不重复触发终态
        QVERIFY(!controller.stopModelTask(record.uuid, ModelTaskType::Train));
        QCOMPARE(task_manager.findTask(task_id)->status, TaskManager::Stopping);

        // 执行者进程最终退出（带 stop_requested 标志）
        QMetaObject::invokeMethod(&controller, "handleExternalTaskFinished", Qt::DirectConnection,
                                  Q_ARG(TaskIdentity, identity), Q_ARG(int, 0), Q_ARG(bool, true), Q_ARG(bool, true));

        // 验收条件 2: 停止等待实际执行者退出，重复停止只发布一次终态并恢复编辑
        QCOMPARE(task_manager.findTask(task_id)->status, TaskManager::Stopped);
        const QVariantMap extra = model_manager.modelRecordForUuid(record.uuid).value(QStringLiteral("extra_data")).toMap();
        const QVariantMap train_section = extra.value(QStringLiteral("train")).toMap();
        QCOMPARE(train_section.value(QStringLiteral("status")).toString(), QStringLiteral("stopped"));
        QCOMPARE(train_section.value(QStringLiteral("started")).toBool(), false);
    }

    void handlesPendingStartFailedCrashAndLateMessages()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager                         model_manager(kControllerTestMethod, &database, nullptr);
        QString                              error;
        const auto record = model_manager.addModelRecord(QStringLiteral("Lifecycle_model"), QStringLiteral("controller-test"),
                                                         QStringLiteral("ControllerModel"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        TaskManager task_manager;
        ModelTaskController controller(kControllerTestMethod, fixture.rootPath(), &model_manager, nullptr, &task_manager);

        // 1. Pending 任务可直接停止
        const int pending_id = controller.addModelTask(record.uuid, ModelTaskType::Train);
        QVERIFY(pending_id > 0);
        QCOMPARE(task_manager.findTask(pending_id)->status, TaskManager::Pending);
        QVERIFY(controller.stopModelTask(record.uuid, ModelTaskType::Train));
        QCOMPARE(task_manager.findTask(pending_id)->status, TaskManager::Stopped);
        QVERIFY(controller.deleteModelTask(record.uuid, ModelTaskType::Train));

        // 2. 启动失败处理并恢复编辑
        const int fail_task_id = task_manager.addTask(record.uuid, record.name, ModelTaskType::Train);
        QVERIFY(task_manager.startTask(fail_task_id));
        const TaskIdentity fail_identity = task_manager.findTask(fail_task_id)->identity;
        QMetaObject::invokeMethod(&controller, "handleExternalTaskStartFailed", Qt::DirectConnection,
                                  Q_ARG(TaskIdentity, fail_identity), Q_ARG(QString, QStringLiteral("启动失败错误")));
        QCOMPARE(task_manager.findTask(fail_task_id)->status, TaskManager::Failed);
        QCOMPARE(model_manager.modelRecordForUuid(record.uuid)
                     .value(QStringLiteral("extra_data"))
                     .toMap()
                     .value(QStringLiteral("train"))
                     .toMap()
                     .value(QStringLiteral("started"))
                     .toBool(),
                 false);
        QVERIFY(controller.deleteModelTask(record.uuid, ModelTaskType::Train));

        // 3. 进程崩溃处理（normal_exit = false）
        const int crash_task_id = task_manager.addTask(record.uuid, record.name, ModelTaskType::Train);
        QVERIFY(task_manager.startTask(crash_task_id));
        QCOMPARE(task_manager.findTask(crash_task_id)->status, TaskManager::Running);
        const TaskIdentity crash_identity = task_manager.findTask(crash_task_id)->identity;
        QMetaObject::invokeMethod(&controller, "handleExternalTaskFinished", Qt::DirectConnection,
                                  Q_ARG(TaskIdentity, crash_identity), Q_ARG(int, 255), Q_ARG(bool, false), Q_ARG(bool, false));
        QCOMPARE(task_manager.findTask(crash_task_id)->status, TaskManager::Failed);
        QCOMPARE(model_manager.modelRecordForUuid(record.uuid)
                     .value(QStringLiteral("extra_data"))
                     .toMap()
                     .value(QStringLiteral("train"))
                     .toMap()
                     .value(QStringLiteral("started"))
                     .toBool(),
                 false);

        // 4. 迟到消息必须被丢弃，不能将终态改回 Running 或改变进度
        TaskMessage late_msg;
        late_msg.identity = crash_identity;
        late_msg.type     = TaskMessageType::Progress;
        late_msg.status   = TaskProtocolStatus::Running;
        late_msg.progress = 50;
        QMetaObject::invokeMethod(&task_manager, "handleTaskMessage", Qt::DirectConnection,
                                  Q_ARG(TaskMessage, late_msg));
        QCOMPARE(task_manager.findTask(crash_task_id)->status, TaskManager::Failed);
        QCOMPARE(task_manager.findTask(crash_task_id)->progress, 0);
    }
};

REGISTER_TEST(ModelTaskControllerTest)

#include "test_ModelTaskController.moc"
