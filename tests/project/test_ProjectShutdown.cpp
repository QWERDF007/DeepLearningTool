#include "data/DataIO.h"
#include "data/DataManager.h"
#include "core/CoreDef.h"
#include "database/DataBase.h"
#include "project/Projects.h"

#include <QDir>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

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
};

QTEST_GUILESS_MAIN(ProjectShutdownIntegrationTest)

#include "test_ProjectShutdown.moc"
