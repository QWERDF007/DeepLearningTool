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

        FrameworkDefinition fewshot_framework;
        fewshot_framework.method = kControllerTestMethod;
        fewshot_framework.name   = QStringLiteral("controller-fewshot");
        fewshot_framework.few_shot = true;
        fewshot_framework.task_capabilities.push_back({ModelTaskType::Train, QStringLiteral("mock_train.py")});
        fewshot_framework.task_capabilities.push_back({ModelTaskType::Test, QStringLiteral("mock_test.py")});
        fewshot_framework.weight_extensions = {QStringLiteral(".pt")};
        registerFramework(kControllerTestMethod, fewshot_framework);
        registerModel(kControllerTestMethod, fewshot_framework.name, QStringLiteral("ControllerModel"),
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

    void trainStateStoresNumericElapsedAndRunIdentityWithoutParallelElapsed()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager                         model_manager(kControllerTestMethod, &database, nullptr);
        QString                              error;
        const auto record = model_manager.addModelRecord(QStringLiteral("NumericElapsedModel"), QStringLiteral("controller-test"),
                                                         QStringLiteral("ControllerModel"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        TaskManager task_manager;
        ModelTaskController controller(kControllerTestMethod, fixture.rootPath(), &model_manager, nullptr, &task_manager);

        const int task_id = controller.startModelTask(record.uuid, ModelTaskType::Train);
        QVERIFY(task_id > 0);
        QTRY_VERIFY(task_manager.findTask(task_id) != nullptr
                    && task_manager.findTask(task_id)->status == TaskManager::Running);

        const TaskIdentity identity = task_manager.findTask(task_id)->identity;
        QVERIFY(identity.isValid());

        // Python 上报带有虚假 elapsed 字符串的消息
        TaskMessage msg;
        msg.identity = identity;
        msg.type     = TaskMessageType::Progress;
        msg.status   = TaskProtocolStatus::Running;
        msg.progress = 40;
        msg.payload.insert(QStringLiteral("elapsed"), QStringLiteral("99:99:99"));
        msg.payload.insert(QStringLiteral("loss"), QStringLiteral("0.123"));
        QMetaObject::invokeMethod(&controller, "handleTaskMessage", Qt::DirectConnection, Q_ARG(TaskMessage, msg));

        // 冲刷状态
        QMetaObject::invokeMethod(&controller, "syncTaskModelState", Qt::DirectConnection, Q_ARG(int, task_id));

        const QVariantMap extra = model_manager.modelRecordForUuid(record.uuid).value(QStringLiteral("extra_data")).toMap();
        const QVariantMap train_sec = extra.value(QStringLiteral("train")).toMap();

        // 验收条件 1: 删除对应平行写入（Python 的 99:99:99 不能覆盖 C++ 本地计时），保存数值耗时与运行身份
        QVERIFY(train_sec.value(QStringLiteral("elapsed")).toString() != QStringLiteral("99:99:99"));
        QVERIFY(train_sec.contains(QStringLiteral("elapsed_seconds")));
        QCOMPARE(train_sec.value(QStringLiteral("run_id")).toString(), identity.run_id);
        QCOMPARE(train_sec.value(QStringLiteral("project_id")).toString(), identity.project_id);
        QCOMPARE(train_sec.value(QStringLiteral("task_id")).toInt(), identity.task_id);
        QCOMPARE(train_sec.value(QStringLiteral("loss")).toString(), QStringLiteral("0.123"));

        controller.stopModelTask(record.uuid, ModelTaskType::Train);
    }

    void pythonSilentRuntimeAdvancesAndReopenRestoresTerminalStateAndDuration()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager                         model_manager(kControllerTestMethod, &database, nullptr);
        QString                              error;
        const auto record = model_manager.addModelRecord(QStringLiteral("SilentModel"), QStringLiteral("controller-test"),
                                                         QStringLiteral("ControllerModel"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        TaskManager task_manager;
        ModelTaskController controller(kControllerTestMethod, fixture.rootPath(), &model_manager, nullptr, &task_manager);

        const int task_id = controller.startModelTask(record.uuid, ModelTaskType::Train);
        QVERIFY(task_id > 0);
        QTRY_VERIFY(task_manager.findTask(task_id) != nullptr
                    && task_manager.findTask(task_id)->status == TaskManager::Running);

        const TaskIdentity identity = task_manager.findTask(task_id)->identity;

        // Python 长时间不发任何消息，本地计时推进
        QTest::qWait(1100);
        QMetaObject::invokeMethod(&task_manager, "refreshRunningTasks", Qt::DirectConnection);
        QMetaObject::invokeMethod(&controller, "syncTaskModelState", Qt::DirectConnection, Q_ARG(int, task_id));

        const qint64 running_sec = task_manager.taskRunningTimeSeconds(task_id);
        QVERIFY(running_sec >= 1);

        // 停止任务并落库
        QVERIFY(controller.stopModelTask(record.uuid, ModelTaskType::Train));
        QCOMPARE(task_manager.findTask(task_id)->status, TaskManager::Stopped);

        const QVariantMap extra_before = model_manager.modelRecordForUuid(record.uuid).value(QStringLiteral("extra_data")).toMap();
        const QVariantMap train_before = extra_before.value(QStringLiteral("train")).toMap();
        const qint64 stopped_seconds = train_before.value(QStringLiteral("elapsed_seconds")).toLongLong();
        const QString stopped_elapsed = train_before.value(QStringLiteral("elapsed")).toString();
        QVERIFY(stopped_seconds >= 1);
        QVERIFY(!stopped_elapsed.isEmpty() && stopped_elapsed != QStringLiteral("00:00:00"));
        QCOMPARE(train_before.value(QStringLiteral("status")).toString(), QStringLiteral("stopped"));
        QCOMPARE(train_before.value(QStringLiteral("started")).toBool(), false);

        // 模拟关闭项目并重开
        controller.shutdown();
        task_manager.clearTasks();

        ModelManager reloaded_model_manager(kControllerTestMethod, &database, nullptr);
        TaskManager reopened_task_manager;
        ModelTaskController reopened_controller(kControllerTestMethod, fixture.rootPath(),
                                                &reloaded_model_manager, nullptr, &reopened_task_manager);

        // 验收条件 2: 完成/停止/失败重开后正确显示终态和准确耗时
        const auto *restored = reopened_task_manager.findModelTaskRecord(record.uuid, ModelTaskType::Train, true);
        QVERIFY(restored != nullptr);
        QCOMPARE(restored->status, TaskManager::Stopped);
        QCOMPARE(restored->elapsed_seconds, stopped_seconds);
        QCOMPARE(reopened_task_manager.taskRunningTimeSeconds(restored->identity.task_id), stopped_seconds);
        QCOMPARE(reopened_task_manager.taskRunningTime(restored->identity.task_id), stopped_elapsed);
        QCOMPARE(restored->identity.run_id, identity.run_id);
        QVERIFY(reopened_task_manager.canStartTask(restored->identity.task_id));
    }

    void internalSubtasksStoreNumericElapsedAndRestoreWithoutTestEvaluationStructure()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager                         model_manager(kControllerTestMethod, &database, nullptr);
        QString                              error;
        const auto record = model_manager.addModelRecord(QStringLiteral("SubtaskModel"), QStringLiteral("controller-fewshot"),
                                                         QStringLiteral("ControllerModel"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        TaskManager task_manager;
        ModelTaskController controller(kControllerTestMethod, fixture.rootPath(), &model_manager, nullptr, &task_manager);

        // 启动内部子任务 BoxToMask
        const int b2m_id = controller.startModelTask(record.uuid, ModelTaskType::BoxToMask);
        QVERIFY(b2m_id > 0);
        QTRY_VERIFY(task_manager.findTask(b2m_id) != nullptr
                    && task_manager.findTask(b2m_id)->status == TaskManager::Running);

        const TaskIdentity b2m_identity = task_manager.findTask(b2m_id)->identity;
        QTest::qWait(1100);
        QMetaObject::invokeMethod(&task_manager, "refreshRunningTasks", Qt::DirectConnection);
        QVERIFY(controller.stopModelTask(record.uuid, ModelTaskType::BoxToMask));
        QCOMPARE(task_manager.findTask(b2m_id)->status, TaskManager::Stopped);

        const QVariantMap extra = model_manager.modelRecordForUuid(record.uuid).value(QStringLiteral("extra_data")).toMap();
        // 验收条件 3: 训练与内部子任务不强套普通测试评估结构，保存在 extra_data.box_to_mask
        QVERIFY(extra.contains(QStringLiteral("box_to_mask")));
        const QVariantMap b2m_sec = extra.value(QStringLiteral("box_to_mask")).toMap();
        QCOMPARE(b2m_sec.value(QStringLiteral("status")).toString(), QStringLiteral("stopped"));
        QCOMPARE(b2m_sec.value(QStringLiteral("started")).toBool(), false);
        QVERIFY(b2m_sec.value(QStringLiteral("elapsed_seconds")).toLongLong() >= 1);
        QCOMPARE(b2m_sec.value(QStringLiteral("run_id")).toString(), b2m_identity.run_id);

        // 重开后恢复内部任务终态和准确耗时
        controller.shutdown();
        task_manager.clearTasks();

        ModelManager reloaded_model_manager(kControllerTestMethod, &database, nullptr);
        TaskManager reopened_task_manager;
        ModelTaskController reopened_controller(kControllerTestMethod, fixture.rootPath(),
                                                &reloaded_model_manager, nullptr, &reopened_task_manager);

        const auto *restored = reopened_task_manager.findModelTaskRecord(record.uuid, ModelTaskType::BoxToMask,
                                                                         QStringLiteral("box_to_mask"), true);
        QVERIFY(restored != nullptr);
        QCOMPARE(restored->status, TaskManager::Stopped);
        QCOMPARE(restored->elapsed_seconds, b2m_sec.value(QStringLiteral("elapsed_seconds")).toLongLong());
        QCOMPARE(restored->identity.run_id, b2m_identity.run_id);
    }

    void legacyDurationTextMigrationParsesNumericElapsed()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager                         model_manager(kControllerTestMethod, &database, nullptr);
        QString                              error;
        const auto record = model_manager.addModelRecord(QStringLiteral("LegacyModel"), QStringLiteral("controller-test"),
                                                         QStringLiteral("ControllerModel"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        // 模拟旧版本仅有文本 elapsed 的 extra_data
        QVariantMap legacy_train;
        legacy_train.insert(QStringLiteral("status"), QStringLiteral("finished"));
        legacy_train.insert(QStringLiteral("started"), false);
        legacy_train.insert(QStringLiteral("progress"), 100);
        legacy_train.insert(QStringLiteral("elapsed"), QStringLiteral("00:03:45"));
        legacy_train.insert(QStringLiteral("phase"), QStringLiteral("train"));
        QVERIFY(model_manager.updateModelExtraData(record.uuid, {{QStringLiteral("train"), legacy_train}}, &error));

        // 重开并恢复
        TaskManager task_manager;
        ModelTaskController controller(kControllerTestMethod, fixture.rootPath(), &model_manager, nullptr, &task_manager);

        const auto *restored = task_manager.findModelTaskRecord(record.uuid, ModelTaskType::Train, true);
        QVERIFY(restored != nullptr);
        QCOMPARE(restored->status, TaskManager::Finished);
        QCOMPARE(restored->progress, 100);
        // 验收条件 3: 仅迁移支持的当前结构，文本耗时自动解析为准确数值 (3*60 + 45 = 225 秒)
        QCOMPARE(restored->elapsed_seconds, static_cast<qint64>(225));
        QCOMPARE(task_manager.taskRunningTimeSeconds(restored->identity.task_id), static_cast<qint64>(225));
        QCOMPARE(task_manager.taskRunningTime(restored->identity.task_id), QStringLiteral("00:03:45"));
    }

    void testTaskStateAndDurationPersistedInTaskDbAndRestoredOnReopenWithoutExtraData()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager model_manager(kControllerTestMethod, &database, nullptr);
        QString error;
        const auto record = model_manager.addModelRecord(QStringLiteral("TestTaskModel"), QStringLiteral("controller-test"),
                                                         QStringLiteral("ControllerModel"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        ModelTestTaskRepository test_task_repo(fixture.rootPath());
        test_task_repo.setProjectDatabasePath(fixture.projectDatabasePath());
        ModelTestTaskDefinition created_task;
        QVERIFY(test_task_repo.createTask(record.name, record.uuid, QStringLiteral("TaskAlpha"), {}, {}, created_task, &error));

        TaskManager task_manager;
        ModelTaskController controller(kControllerTestMethod, fixture.rootPath(), &model_manager, nullptr, &task_manager);

        const int task_id = controller.startModelTestTask(record.uuid, created_task.uuid);
        QVERIFY(task_id > 0);
        QTRY_VERIFY(task_manager.findTask(task_id) != nullptr
                    && task_manager.findTask(task_id)->status == TaskManager::Running);

        const TaskIdentity identity = task_manager.findTask(task_id)->identity;

        QTest::qWait(1100);
        QMetaObject::invokeMethod(&task_manager, "refreshRunningTasks", Qt::DirectConnection);

        QVERIFY(controller.stopModelTestTask(record.uuid, created_task.uuid));
        QCOMPARE(task_manager.findTask(task_id)->status, TaskManager::Stopped);

        // 验收条件 1 & 3: extra_data 不保留 test_tasks，执行状态唯一保存在 task.db
        const QVariantMap extra = model_manager.modelRecordForUuid(record.uuid).value(QStringLiteral("extra_data")).toMap();
        QVERIFY(!extra.contains(QStringLiteral("test_tasks")));

        const ModelStorageService storage(fixture.rootPath());
        const QString task_db_path = storage.testTaskDatabasePath(record.name, created_task.directory_name);
        dltool::database::ModelTaskDataBase task_db(task_db_path);
        QVariantMap execution_state;
        QVERIFY(task_db.readExecutionState(execution_state));
        QCOMPARE(execution_state.value(QStringLiteral("status")).toString(), QStringLiteral("stopped"));
        QVERIFY(execution_state.value(QStringLiteral("elapsed_seconds")).toLongLong() >= 1);
        const qint64 stopped_seconds = execution_state.value(QStringLiteral("elapsed_seconds")).toLongLong();

        // 重开并恢复测试任务
        controller.shutdown();
        task_manager.clearTasks();

        ModelManager reloaded_model_manager(kControllerTestMethod, &database, nullptr);
        TaskManager  reopened_task_manager;
        ModelTaskController reopened_controller(kControllerTestMethod, fixture.rootPath(),
                                                &reloaded_model_manager, nullptr, &reopened_task_manager);

        const auto *restored = reopened_task_manager.findModelTaskRecord(record.uuid, ModelTaskType::Test, created_task.uuid, true);
        QVERIFY(restored != nullptr);
        QCOMPARE(restored->status, TaskManager::Stopped);
        QCOMPARE(restored->elapsed_seconds, stopped_seconds);
        QCOMPARE(reopened_task_manager.taskRunningTimeSeconds(restored->identity.task_id), stopped_seconds);
    }
};

REGISTER_TEST(ModelTaskControllerTest)

#include "test_ModelTaskController.moc"
