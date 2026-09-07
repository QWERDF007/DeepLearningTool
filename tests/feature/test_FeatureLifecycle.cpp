#include "feature/FeatureManager.h"
#include "feature/ImageSearchController.h"
#include "feature/RoiClusterController.h"
#include "feature/SmartAnnotationController.h"
#include "core/CoreDef.h"
#include "data/DataManager.h"
#include "data/DataOperationWorkflow.h"
#include "database/DataBase.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <atomic>

namespace {

class FeatureLifecycleTest : public QObject
{
    Q_OBJECT

private slots:
    void roiClusterShutdownIsIdempotentAndRejectsNewWork()
    {
        dltool::feature::RoiClusterController controller(nullptr, nullptr);

        controller.shutdown();
        controller.shutdown();

        QVERIFY(!controller.isRunning());
        QVERIFY(!controller.cluster({1}));
        QCOMPARE(controller.lastError(), QStringLiteral("标注聚类控制器正在关闭"));
    }

    void featureManagerShutdownPropagatesToChildren()
    {
        dltool::feature::FeatureManager manager(nullptr, nullptr, nullptr, nullptr);

        manager.shutdown();
        manager.shutdown();

        QVERIFY(manager.imageSearch() != nullptr);
        QVERIFY(!manager.imageSearch()->search({1}, {}));
        QCOMPARE(manager.imageSearch()->lastError(), QStringLiteral("图像搜索控制器正在关闭"));

        QVERIFY(manager.roiCluster() != nullptr);
        QVERIFY(!manager.roiCluster()->cluster({1}));
        QCOMPARE(manager.roiCluster()->lastError(), QStringLiteral("标注聚类控制器正在关闭"));

        QVERIFY(manager.smartAnnotation() != nullptr);
        const QVariantMap result = manager.smartAnnotation()->infer({}, {}, {});
        QVERIFY(!result.value(QStringLiteral("success")).toBool());
        QCOMPARE(result.value(QStringLiteral("error")).toString(), QStringLiteral("智能标注控制器正在关闭"));
    }

    void roiClusterShutdownWaitsForCancelledDataOperations()
    {
        QTemporaryDir project_directory;
        QVERIFY(project_directory.isValid());

        const QString project_path = QDir(project_directory.path()).filePath(QStringLiteral("feature-lifecycle.dlpro"));
        dltool::database::ProjectDataBase database(project_path);
        QString                       database_error;
        const qint64                  now = QDateTime::currentSecsSinceEpoch();
        QVERIFY2(database.initProject(QStringLiteral("Feature lifecycle"),
                                      static_cast<int>(dltool::core::DeepLearningMethod::AnomalyDetection),
                                      project_path, QStringLiteral("shutdown test"), project_directory.path(), now, now,
                                      database_error),
                 qPrintable(database_error));

        dltool::data::DataManager data_manager(
            static_cast<int>(dltool::core::DeepLearningMethod::AnomalyDetection), &database,
            project_directory.path());
        data_manager.waitForOperations();

        std::atomic_bool work_started{false};
        dltool::data::DataOperationWorkflow::Options options;
        options.manage_progress = false;
        const auto operation = data_manager.runDatasetExportAsync(
            &data_manager, {}, options,
            [&work_started](const dltool::data::DatasetExportSource &,
                            dltool::data::DataOperationWorkflow::Result &result)
            {
                work_started.store(true, std::memory_order_release);
                while (!result.cancellationRequested())
                    QThread::msleep(1);
                result.cancelled = true;
                result.error     = QStringLiteral("测试取消");
            });
        QVERIFY(operation != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(work_started.load(std::memory_order_acquire), 2000);

        // Project::shutdown() requests cancellation before feature controllers
        // enter their own shutdown barriers.  The controller must then wait
        // for the shared DataManager operation and its completion callback.
        data_manager.cancelDataOperation();
        QVERIFY(operation->isCancellationRequested());

        dltool::feature::RoiClusterController controller(nullptr, &data_manager);
        QElapsedTimer shutdown_timer;
        shutdown_timer.start();
        controller.shutdown();

        QVERIFY(operation->isFinished());
        QVERIFY(operation->isCompletionFinished());
        QVERIFY2(shutdown_timer.elapsed() < 2000,
                 qPrintable(QStringLiteral("RoiClusterController 关闭等待数据操作超时: %1 ms")
                                .arg(shutdown_timer.elapsed())));
    }
};

} // namespace

QTEST_GUILESS_MAIN(FeatureLifecycleTest)

#include "test_FeatureLifecycle.moc"
