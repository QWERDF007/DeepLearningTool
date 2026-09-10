#include "data/DataIO.h"
#include "data/DataManager.h"
#include "core/CoreDef.h"
#include "database/DataBase.h"
#include "feature/FeatureManager.h"
#include "feature/ImageClusterController.h"
#include "feature/SmartAnnotationController.h"
#include "model/ModelManager.h"
#include "model/ModelTaskController.h"
#include "project/Projects.h"
#include "ui/ProgressManager.h"
#include <spdlog/spdlog.h>

#include <QDir>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <utility>

#include <sqlite3.h>

namespace {

class BlockingProjectDatabaseDataIO final : public dltool::data::DataIO
{
public:
    explicit BlockingProjectDatabaseDataIO(QString database_path, QObject *parent = nullptr)
        : DataIO(parent)
        , database_path_(std::move(database_path))
    {
    }

    void startDatabaseLock()
    {
        runInThread([this]()
                    {
                        sqlite3 *database = nullptr;
                        if (sqlite3_open_v2(database_path_.toUtf8().constData(), &database,
                                            SQLITE_OPEN_READWRITE, nullptr)
                            != SQLITE_OK)
                        {
                            if (database != nullptr)
                                sqlite3_close(database);
                            return;
                        }

                        sqlite3_busy_timeout(database, 0);
                        bool locked = false;
                        while (!isCancelRequested())
                        {
                            char *error_message = nullptr;
                            const int result = sqlite3_exec(database, "BEGIN EXCLUSIVE;", nullptr, nullptr,
                                                            &error_message);
                            if (error_message != nullptr)
                                sqlite3_free(error_message);
                            if (result == SQLITE_OK)
                            {
                                locked = true;
                                lock_acquired_.store(true, std::memory_order_release);
                                break;
                            }
                            QThread::msleep(5);
                        }

                        while (locked && !isCancelRequested())
                            QThread::msleep(5);

                        if (locked)
                            sqlite3_exec(database, "ROLLBACK;", nullptr, nullptr, nullptr);
                        sqlite3_close(database);
                    });
    }

    void startCancellationNotification()
    {
        runInThread([this]()
                    {
                        while (!isCancelRequested())
                            QThread::msleep(5);
                        emit importFinished(true, {}, {});
                    });
    }

    bool lockAcquired() const
    {
        return lock_acquired_.load(std::memory_order_acquire);
    }

private:
    QString          database_path_;
    std::atomic_bool lock_acquired_{false};
};

} // namespace

class ProjectShutdownIntegrationTest final : public QObject
{
    Q_OBJECT

private slots:
    void closesProjectAfterCancellingDatabaseOperation()
    {
        QTemporaryDir project_directory;
        QVERIFY(project_directory.isValid());

        auto *manager = dltool::project::ProjectManager::getInstance();
        QVERIFY(manager != nullptr);
        if (manager->currentProject() != nullptr)
            manager->closeProject();

        const QString project_path = QDir(project_directory.path()).filePath(QStringLiteral("close-order.dlpro"));
        auto *project = manager->createProject(
            QStringLiteral("关闭顺序测试"),
            static_cast<int>(dltool::core::DeepLearningMethod::AnomalyDetection),
            project_path,
            QStringLiteral("项目关闭生命周期测试"),
            project_directory.path());
        QVERIFY(project != nullptr);
        QVERIFY(project->dataManager() != nullptr);

        QVariantMap project_info;
        QString     database_error;
        QVERIFY2(dltool::database::ProjectDataBase::getProjectInfo(project_path, project_info, database_error),
                 qPrintable(database_error));
        QVERIFY2(dltool::database::ProjectDataBase::updateProjectBaseInfo(
                      project_path, project_info.value(QStringLiteral("name")).toString(),
                      project_info.value(QStringLiteral("description")).toString(), 1, database_error),
                 qPrintable(database_error));

        auto *blocking_operation = new BlockingProjectDatabaseDataIO(project_path, project->dataManager());
        blocking_operation->startDatabaseLock();
        QTRY_VERIFY_WITH_TIMEOUT(blocking_operation->lockAcquired(), 10000);

        QElapsedTimer close_timer;
        close_timer.start();
        manager->closeProject();

        QVERIFY2(manager->currentProject() == nullptr, "项目关闭后仍保留当前项目");
        QVERIFY2(close_timer.elapsed() < 3000,
                 qPrintable(QStringLiteral("项目关闭等待了 %1 ms，未先取消后台数据库操作")
                                .arg(close_timer.elapsed())));

        QVariantMap closed_project_info;
        QVERIFY2(dltool::database::ProjectDataBase::getProjectInfo(project_path, closed_project_info, database_error),
                 qPrintable(database_error));
        QVERIFY2(closed_project_info.value(QStringLiteral("mtime")).toString()
                     != QStringLiteral("1970/01/01 00:00"),
                 "项目关闭前的更新时间写入失败");
    }

    void rejectsNewDataWritesAfterProjectShutdownBegins()
    {
        QTemporaryDir project_directory;
        QVERIFY(project_directory.isValid());

        auto *manager = dltool::project::ProjectManager::getInstance();
        QVERIFY(manager != nullptr);
        if (manager->currentProject() != nullptr)
            manager->closeProject();

        const QString project_path = QDir(project_directory.path()).filePath(QStringLiteral("shutdown-gate.dlpro"));
        auto *project = manager->createProject(
            QStringLiteral("关闭闸门测试"),
            static_cast<int>(dltool::core::DeepLearningMethod::AnomalyDetection),
            project_path,
            QStringLiteral("项目关闭闸门测试"),
            project_directory.path());
        QVERIFY(project != nullptr);
        auto *data_manager = project->dataManager();
        QVERIFY(data_manager != nullptr);

        qRegisterMetaType<std::vector<int64_t>>();
        qRegisterMetaType<std::vector<QString>>();
        std::atomic_bool completion_attempted{false};
        std::atomic_bool write_accepted{false};
        auto *operation = new BlockingProjectDatabaseDataIO(project_path, data_manager);
        connect(operation, &dltool::data::DataIO::importFinished, data_manager,
                [data_manager, &completion_attempted, &write_accepted](bool, std::vector<int64_t>,
                                                                         std::vector<int64_t>)
                {
                    completion_attempted.store(true, std::memory_order_release);
                    int64_t dataset_id = -1;
                    QString error;
                    write_accepted.store(data_manager->ensureDataset(QStringLiteral("关闭期间不应创建"), dataset_id,
                                                                     error),
                                         std::memory_order_release);
                },
                Qt::QueuedConnection);
        operation->startCancellationNotification();

        manager->closeProject();

        QVERIFY(completion_attempted.load(std::memory_order_acquire));
        QVERIFY(!write_accepted.load(std::memory_order_acquire));

        QString database_error;
        QVariantMap project_info;
        QVERIFY2(dltool::database::ProjectDataBase::getProjectInfo(project_path, project_info, database_error),
                 qPrintable(database_error));
        dltool::database::ProjectDataBase database(project_path);
        std::vector<int64_t> dataset_ids;
        std::vector<QString> dataset_names;
        QVERIFY(database.getAllDatasets(dataset_ids, dataset_names, database_error));
        QVERIFY(!std::any_of(dataset_names.cbegin(), dataset_names.cend(),
                             [](const QString &name) { return name == QStringLiteral("关闭期间不应创建"); }));
    }

    void rejectsQueuedFeatureAndModelOperationsDuringProjectShutdown()
    {
        QTemporaryDir project_directory;
        QVERIFY(project_directory.isValid());

        auto *manager = dltool::project::ProjectManager::getInstance();
        QVERIFY(manager != nullptr);
        if (manager->currentProject() != nullptr)
            manager->closeProject();

        const QString project_path = QDir(project_directory.path()).filePath(QStringLiteral("shutdown-feature-model.dlpro"));
        auto *project = manager->createProject(
            QStringLiteral("关闭组件测试"),
            static_cast<int>(dltool::core::DeepLearningMethod::AnomalyDetection),
            project_path,
            QStringLiteral("项目关闭期间组件拒绝测试"),
            project_directory.path());
        QVERIFY(project != nullptr);
        auto *data_manager = project->dataManager();
        auto *model_manager = project->modelManager();
        auto *task_controller = project->modelTaskController();
        auto *feature_manager = project->featureManager();
        QVERIFY(data_manager != nullptr && model_manager != nullptr && task_controller != nullptr && feature_manager != nullptr);

        // 创建一个模型记录
        QString add_model_err;
        const auto model_record = model_manager->addModelRecord(
            QStringLiteral("TestModel"), QStringLiteral("anomalib"), QStringLiteral("patchcore"), &add_model_err);
        QVERIFY2(model_record.isValid(), qPrintable(add_model_err));

        std::atomic_bool queued_slot_ran{false};
        std::atomic_bool model_task_accepted{false};
        std::atomic_bool cluster_accepted{false};
        std::atomic_bool infer_accepted{false};
        std::atomic_bool add_model_accepted{false};

        auto *operation = new BlockingProjectDatabaseDataIO(project_path, data_manager);
        connect(operation, &dltool::data::DataIO::importFinished, this,
                [model_manager, task_controller, feature_manager, model_uuid = model_record.uuid,
                 &queued_slot_ran, &model_task_accepted, &cluster_accepted, &infer_accepted, &add_model_accepted]()
                {
                    queued_slot_ran.store(true, std::memory_order_release);

                    // 尝试在关闭阶段启动模型任务 -> 必须被拒绝
                    const int task_id = task_controller->startModelTask(model_uuid, dltool::model::ModelTaskType::Train);
                    if (task_id >= 0)
                        model_task_accepted.store(true, std::memory_order_release);

                    // 尝试在关闭阶段启动聚类 -> 必须被拒绝
                    if (feature_manager->imageCluster()->cluster({1}))
                        cluster_accepted.store(true, std::memory_order_release);

                    // 尝试在关闭阶段启动推理 -> 必须被拒绝
                    const auto infer_res = feature_manager->smartAnnotation()->infer(QStringLiteral("fake.jpg"), {}, {});
                    if (infer_res.value(QStringLiteral("success")).toBool())
                        infer_accepted.store(true, std::memory_order_release);

                    // 尝试在关闭阶段添加模型 -> 必须被拒绝
                    if (model_manager->addModel(QStringLiteral("LateModel"), QStringLiteral("anomalib"), QStringLiteral("patchcore")))
                        add_model_accepted.store(true, std::memory_order_release);
                },
                Qt::QueuedConnection);
        operation->startCancellationNotification();

        manager->closeProject();

        QVERIFY(queued_slot_ran.load(std::memory_order_acquire));
        QVERIFY(!model_task_accepted.load(std::memory_order_acquire));
        QVERIFY(!cluster_accepted.load(std::memory_order_acquire));
        QVERIFY(!infer_accepted.load(std::memory_order_acquire));
        QVERIFY(!add_model_accepted.load(std::memory_order_acquire));
        QVERIFY(manager->currentProject() == nullptr);
    }

    void repeatedCloseAndSwitchProjectLeavesCleanState()
    {
        QTemporaryDir dir_a;
        QVERIFY(dir_a.isValid());
        QTemporaryDir dir_b;
        QVERIFY(dir_b.isValid());

        auto *manager = dltool::project::ProjectManager::getInstance();
        QVERIFY(manager != nullptr);
        if (manager->currentProject() != nullptr)
            manager->closeProject();

        const QString path_a = QDir(dir_a.path()).filePath(QStringLiteral("project-a.dlpro"));
        auto *project_a = manager->createProject(
            QStringLiteral("项目A"),
            static_cast<int>(dltool::core::DeepLearningMethod::AnomalyDetection),
            path_a,
            QStringLiteral("描述A"),
            dir_a.path());
        QVERIFY(project_a != nullptr);

        // 重复关闭安全幂等
        manager->closeProject();
        QVERIFY(manager->currentProject() == nullptr);
        manager->closeProject();
        QVERIFY(manager->currentProject() == nullptr);

        // 切换创建项目B
        const QString path_b = QDir(dir_b.path()).filePath(QStringLiteral("project-b.dlpro"));
        auto *project_b = manager->createProject(
            QStringLiteral("项目B"),
            static_cast<int>(dltool::core::DeepLearningMethod::AnomalyDetection),
            path_b,
            QStringLiteral("描述B"),
            dir_b.path());
        QVERIFY(project_b != nullptr);
        auto *data_b = project_b->dataManager();
        QVERIFY(data_b != nullptr);

        int64_t dataset_b_id = -1;
        QString error;
        QVERIFY(data_b->ensureDataset(QStringLiteral("数据集B"), dataset_b_id, error));
        QVERIFY(dataset_b_id >= 0);

        manager->closeProject();
        QVERIFY(manager->currentProject() == nullptr);

        // 重新打开项目B，验证状态干净且数据未受污染
        auto *reopened_b = manager->openProject(path_b);
        QVERIFY(reopened_b != nullptr);
        auto *reopened_data_b = reopened_b->dataManager();
        QVERIFY(reopened_data_b != nullptr);
        QCOMPARE(reopened_data_b->getDatasetId(QStringLiteral("数据集B")), dataset_b_id);

        manager->closeProject();
    }

    void twoPhaseShutdownSequenceAndLateCallbackIsolation()
    {
        QTemporaryDir project_directory;
        QVERIFY(project_directory.isValid());

        auto *manager = dltool::project::ProjectManager::getInstance();
        QVERIFY(manager != nullptr);
        if (manager->currentProject() != nullptr)
            manager->closeProject();

        const QString project_path = QDir(project_directory.path()).filePath(QStringLiteral("twophase.dlpro"));
        auto *project = manager->createProject(
            QStringLiteral("两阶段关闭测试"),
            static_cast<int>(dltool::core::DeepLearningMethod::AnomalyDetection),
            project_path,
            QStringLiteral("两阶段关闭生命周期测试"),
            project_directory.path());
        QVERIFY(project != nullptr);
        auto *data_manager = project->dataManager();
        auto *model_manager = project->modelManager();
        auto *feature_manager = project->featureManager();
        auto *test_task_manager = project->modelTestTaskManager();
        QVERIFY(data_manager != nullptr);
        QVERIFY(model_manager != nullptr);
        QVERIFY(feature_manager != nullptr);
        QVERIFY(test_task_manager != nullptr);

        auto *progress = dltool::ui::ProgressManager::getInstance();
        QVERIFY(progress != nullptr);
        const QString old_task_id = progress->startTask(QStringLiteral("关闭前任务"), QStringLiteral("task_old_phase"));
        QCOMPARE(progress->activeTaskId(), QStringLiteral("task_old_phase"));
        QVERIFY(progress->getIsRunning());

        // 启动后台锁定操作
        auto *blocking_op = new BlockingProjectDatabaseDataIO(project_path, data_manager);
        blocking_op->startDatabaseLock();
        QTRY_VERIFY_WITH_TIMEOUT(blocking_op->lockAcquired(), 10000);

        // 验证两阶段关闭：先取消、再等待，总耗时应在 3 秒内完成收敛
        QElapsedTimer close_timer;
        close_timer.start();
        manager->closeProject();

        QVERIFY2(close_timer.elapsed() < 3000,
                 qPrintable(QStringLiteral("两阶段关闭等待超时: %1 ms").arg(close_timer.elapsed())));
        QVERIFY(manager->currentProject() == nullptr);

        // 进度管理器必须已被重置，旧任务标识清除
        QVERIFY(!progress->getIsRunning());
        QCOMPARE(progress->getProgress(), 0);
        QVERIFY(progress->activeTaskId().isEmpty());

        // 模拟迟到回调：旧任务的回调通知必须被拒绝，绝不唤醒或污染进度管理器
        progress->updateProgress(75, old_task_id);
        progress->addMessage(spdlog::level::info, QStringLiteral("迟到消息"), old_task_id);
        QVERIFY(!progress->getIsRunning());
        QCOMPARE(progress->getProgress(), 0);
        QVERIFY(progress->activeTaskId().isEmpty());

        // 打开新项目，验证环境干净
        QTemporaryDir new_directory;
        QVERIFY(new_directory.isValid());
        const QString new_project_path = QDir(new_directory.path()).filePath(QStringLiteral("clean_next.dlpro"));
        auto *next_project = manager->createProject(
            QStringLiteral("新干净项目"),
            static_cast<int>(dltool::core::DeepLearningMethod::AnomalyDetection),
            new_project_path,
            QStringLiteral("新项目无污染验证"),
            new_directory.path());
        QVERIFY(next_project != nullptr);
        QVERIFY(!progress->getIsRunning());
        QCOMPARE(progress->getProgress(), 0);
        QVERIFY(progress->activeTaskId().isEmpty());

        manager->closeProject();
    }
};

QTEST_GUILESS_MAIN(ProjectShutdownIntegrationTest)

#include "test_ProjectShutdown.moc"
