#include "feature/FeatureManager.h"
#include "feature/FeatureDataProvider.h"
#include "feature/ImageSearchController.h"
#include "feature/RoiClusterController.h"
#include "feature/SearchControllerBase.h"
#include "feature/SmartAnnotationController.h"
#include "core/CoreDef.h"
#include "data/DataManager.h"
#include "data/DataOperationWorkflow.h"
#include "database/DataBase.h"
#include "ui/ProgressManager.h"

#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTest>
#include <QSignalSpy>
#include <QThread>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace {

class SearchExecutorGate final
{
public:
    void reset()
    {
        std::lock_guard lock(mutex_);
        started_ = false;
        release_ = false;
    }

    void waitUntilReleased()
    {
        {
            std::lock_guard lock(mutex_);
            started_ = true;
        }
        condition_.notify_all();

        std::unique_lock lock(mutex_);
        condition_.wait(lock, [this]() { return release_; });
    }

    bool started() const
    {
        std::lock_guard lock(mutex_);
        return started_;
    }

    void release()
    {
        {
            std::lock_guard lock(mutex_);
            release_ = true;
        }
        condition_.notify_all();
    }

private:
    mutable std::mutex      mutex_;
    std::condition_variable condition_;
    bool                    started_{false};
    bool                    release_{false};
};

SearchExecutorGate search_executor_gate;
std::atomic_bool   search_executor_saw_cancellation{false};
std::atomic_bool   search_executor_delay_progress{false};
std::mutex         delayed_search_progress_mutex;
std::vector<std::function<void(const irt::features::ImageSearchBuildProgress &)>> delayed_search_progress;

void clearDelayedSearchProgress()
{
    std::lock_guard lock(delayed_search_progress_mutex);
    delayed_search_progress.clear();
}

std::function<void(const irt::features::ImageSearchBuildProgress &)> delayedSearchProgressAt(const size_t index)
{
    std::lock_guard lock(delayed_search_progress_mutex);
    return index < delayed_search_progress.size() ? delayed_search_progress[index]
                                                   : std::function<void(const irt::features::ImageSearchBuildProgress &)>{};
}

class SearchLifecycleProvider final : public dltool::feature::FeatureDataProvider
{
public:
    SearchLifecycleProvider()
        : FeatureDataProvider(nullptr)
    {
    }
};

class SearchLifecycleController final : public dltool::feature::SearchControllerBase
{
public:
    explicit SearchLifecycleController(dltool::feature::FeatureDataProvider *provider)
        : SearchControllerBase(dltool::settings::generated::AccessorKey::ImageSearch)
        , provider_(provider)
    {
    }

protected:
    dltool::feature::FeatureDataProvider *dataProvider() const override
    {
        return provider_;
    }

    SearchExecutor searchExecutor() const override
    {
        return &SearchLifecycleController::execute;
    }

    void buildSearchRequest(SearchRequest &request) const override
    {
        request.image_config.model_name   = "test";
        request.image_config.feature_name = "test";
    }

    QString validationErrorForRequest(const SearchRequest &) const override
    {
        return {};
    }

    void collectGallery(SearchRequest &request, const SearchScope &) override
    {
        request.gallery_images.push_back({1, std::filesystem::path("gallery")});
    }

    void collectQuery(SearchRequest &request, const std::vector<int64_t> &) override
    {
        request.query_images.push_back(std::filesystem::path("query"));
    }

    void applyResults(const SearchResponse &) override {}
    void clearProviderResults() override {}

private:
    static void execute(const SearchRequest &request, SearchResponse &response, const BuildProgressCallback &progress)
    {
        search_executor_gate.waitUntilReleased();

        if (request.cancellationRequested())
        {
            search_executor_saw_cancellation.store(true, std::memory_order_release);
            response.success = false;
            response.error     = QStringLiteral("测试搜索已取消");
            return;
        }

        irt::features::ImageSearchBuildProgress build_progress;
        build_progress.stage          = irt::features::ImageSearchBuildStage::LoadingModel;
        build_progress.processed_count = 1;
        build_progress.total_count     = 1;
        if (search_executor_delay_progress.load(std::memory_order_acquire))
        {
            std::lock_guard lock(delayed_search_progress_mutex);
            delayed_search_progress.push_back(progress);
        }
        else
        {
            progress(build_progress);
        }

        response.success = true;
        response.summary = QStringLiteral("测试搜索完成");
    }

    dltool::feature::FeatureDataProvider *provider_{nullptr};
};

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

    void searchProgressCallbacksAreDiscardedAfterShutdown()
    {
        search_executor_gate.reset();
        search_executor_saw_cancellation.store(false, std::memory_order_release);
        search_executor_delay_progress.store(false, std::memory_order_release);

        SearchLifecycleProvider   provider;
        SearchLifecycleController controller(&provider);
        auto                     *progress = dltool::ui::ProgressManager::getInstance();
        progress->reset();

        QVERIFY(controller.search({QVariant(1)}, {}));
        QTRY_VERIFY_WITH_TIMEOUT(search_executor_gate.started(), 2000);

        std::thread releaser([]()
                             {
                                 QThread::msleep(200);
                                 search_executor_gate.release();
                             });
        controller.shutdown();
        releaser.join();

        QVERIFY(search_executor_saw_cancellation.load(std::memory_order_acquire));
        progress->reset();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        QCOMPARE(progress->getMessage(), QString());
        QVERIFY(!controller.isRunning());
    }

    void staleSearchProgressCallbacksAreDiscardedAfterNextRunStarts()
    {
        clearDelayedSearchProgress();
        search_executor_delay_progress.store(true, std::memory_order_release);

        SearchLifecycleProvider   provider;
        SearchLifecycleController controller(&provider);

        search_executor_gate.reset();
        QVERIFY(controller.search({QVariant(1)}, {}));
        QTRY_VERIFY_WITH_TIMEOUT(search_executor_gate.started(), 2000);
        search_executor_gate.release();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.isRunning(), 2000);

        const auto first_run_progress = delayedSearchProgressAt(0);
        QVERIFY(first_run_progress != nullptr);

        search_executor_gate.reset();
        QVERIFY(controller.search({QVariant(1)}, {}));
        QTRY_VERIFY_WITH_TIMEOUT(search_executor_gate.started(), 2000);
        search_executor_gate.release();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.isRunning(), 2000);

        const auto second_run_progress = delayedSearchProgressAt(1);
        QVERIFY(second_run_progress != nullptr);

        QSignalSpy progress_spy(&controller, &SearchLifecycleController::buildProgressChanged);
        irt::features::ImageSearchBuildProgress late_progress;
        late_progress.stage           = irt::features::ImageSearchBuildStage::LoadingModel;
        late_progress.processed_count = 1;
        late_progress.total_count     = 1;
        first_run_progress(late_progress);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

        QCOMPARE(progress_spy.count(), 0);
        search_executor_delay_progress.store(false, std::memory_order_release);
    }
};

} // namespace

QTEST_GUILESS_MAIN(FeatureLifecycleTest)

#include "test_FeatureLifecycle.moc"
