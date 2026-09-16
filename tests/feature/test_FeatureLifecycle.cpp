#include "feature/FeatureManager.h"
#include "feature/FeatureDataProvider.h"
#include "feature/ImageClusterController.h"
#include "feature/ImageClusterDataProvider.h"
#include "feature/ImageSearchController.h"
#include "feature/RoiClusterController.h"
#include "feature/RegionSearchController.h"
#include "feature/SearchControllerBase.h"
#include "feature/SmartAnnotationController.h"
#include "core/CoreDef.h"
#include "data/DataManager.h"
#include "data/DataOperationWorkflow.h"
#include "database/DataBase.h"
#include "settings/GlobalSettings.h"
#include "ui/ProgressManager.h"

#include <inferrt/features/SAMImagePredictor.hpp>

#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTemporaryFile>
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

QString busImagePath()
{
    const QString env_path = qEnvironmentVariable("DLT_TEST_BUS_IMAGE").trimmed();
    if (!env_path.isEmpty() && QFileInfo::exists(env_path))
        return env_path;
#ifdef DLT_SOURCE_DIR
    const QString candidate = QDir(QStringLiteral(DLT_SOURCE_DIR))
        .filePath(QStringLiteral("3rdparty/EasyTrain/src/python/ultralytics/ultralytics/ultralytics/assets/bus.jpg"));
    if (QFileInfo::exists(candidate))
        return candidate;
#endif
    return QString();
}

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

class SmartModelLoadGate final
{
public:
    void waitUntilCalls(const int expected_calls)
    {
        std::unique_lock lock(mutex_);
        condition_.wait(lock, [this, expected_calls]() { return calls_ >= expected_calls; });
    }

    void wait()
    {
        {
            std::lock_guard lock(mutex_);
            ++calls_;
        }
        condition_.notify_all();

        std::unique_lock lock(mutex_);
        condition_.wait(lock, [this]() { return released_; });
    }

    void release()
    {
        {
            std::lock_guard lock(mutex_);
            released_ = true;
        }
        condition_.notify_all();
    }

private:
    std::mutex              mutex_;
    std::condition_variable condition_;
    int                     calls_{0};
    bool                    released_{false};
};

class SmartPredictGate final
{
public:
    void reset()
    {
        std::lock_guard lock(mutex_);
        started_  = false;
        released_ = false;
        calls_    = 0;
    }

    void wait()
    {
        {
            std::lock_guard lock(mutex_);
            started_ = true;
            ++calls_;
        }
        condition_.notify_all();

        std::unique_lock lock(mutex_);
        condition_.wait(lock, [this]() { return released_; });
    }

    void waitUntilStarted()
    {
        std::unique_lock lock(mutex_);
        condition_.wait(lock, [this]() { return started_; });
    }

    void waitUntilCalls(const int expected_calls)
    {
        std::unique_lock lock(mutex_);
        condition_.wait(lock, [this, expected_calls]() { return calls_ >= expected_calls; });
    }

    bool started() const
    {
        std::lock_guard lock(mutex_);
        return started_;
    }

    int calls() const
    {
        std::lock_guard lock(mutex_);
        return calls_;
    }

    void release()
    {
        {
            std::lock_guard lock(mutex_);
            released_ = true;
        }
        condition_.notify_all();
    }

private:
    mutable std::mutex      mutex_;
    std::condition_variable condition_;
    bool                    started_{false};
    bool                    released_{false};
    int                     calls_{0};
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

struct ClusterTestFixture
{
    QTemporaryDir dir;
    QString project_path;
    std::unique_ptr<dltool::database::ProjectDataBase> database;
    std::unique_ptr<dltool::data::DataManager> data_manager;
    int64_t dataset_id{-1};
    std::vector<int64_t> image_ids;
    std::vector<QString> image_paths;

    ~ClusterTestFixture()
    {
        if (data_manager)
            data_manager->shutdown();
        data_manager.reset();
        database.reset();
    }

    bool init(int image_count = 3)
    {
        if (!dir.isValid())
            return false;
        project_path = QDir(dir.path()).filePath(QStringLiteral("cluster_test.dlpro"));
        database = std::make_unique<dltool::database::ProjectDataBase>(project_path);
        QString err;
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        if (!database->initProject(QStringLiteral("ClusterTest"),
                                  static_cast<int>(dltool::core::DeepLearningMethod::AnomalyDetection),
                                  project_path, QStringLiteral("desc"), dir.path(), now, now, err))
            return false;

        if (!database->addDataset(QStringLiteral("train_set"), dataset_id, err))
            return false;

        const QString real_bus = busImagePath();
        for (int i = 0; i < image_count; ++i)
        {
            const QString img_path = QDir(dir.path()).filePath(QString("bus_%1.jpg").arg(i));
            if (QFileInfo::exists(real_bus))
            {
                QFile::copy(real_bus, img_path);
            }
            else
            {
                QImage img(64, 64, QImage::Format_RGB888);
                img.fill(Qt::blue);
                img.save(img_path);
            }
            image_paths.push_back(img_path);
        }

        if (!database->addImages(dataset_id, image_paths, image_ids, err) || image_ids.size() != static_cast<size_t>(image_count))
            return false;

        data_manager = std::make_unique<dltool::data::DataManager>(
            static_cast<int>(dltool::core::DeepLearningMethod::AnomalyDetection), database.get(), dir.path());
        data_manager->waitForOperations();
        return true;
    }
};

struct ClusterSettingsScope
{
    dltool::settings::GlobalSettings *settings{nullptr};
    QVariant old_enabled;
    QVariant old_model;
    QVariant old_modelPath;
    QVariant old_feature;
    QVariant old_applyMode;
    QVariant old_includeNoise;
    bool old_autoSave{false};
    QTemporaryFile weights_file;

    ClusterSettingsScope()
    {
        settings = dltool::settings::GlobalSettings::getInstance();
        if (settings)
        {
            old_autoSave = settings->autoSaveEnabled();
            settings->setAutoSaveEnabled(false);
            namespace field = dltool::settings::generated::field;
            old_enabled = settings->valueForField(field::ImageCluster::Enabled);
            old_model = settings->valueForField(field::ImageCluster::Model);
            old_modelPath = settings->valueForField(field::ImageCluster::ModelPath);
            old_feature = settings->valueForField(field::ImageCluster::FeatureName);
            old_applyMode = settings->valueForField(field::ImageCluster::ApplyMode);
            old_includeNoise = settings->valueForField(field::ImageCluster::IncludeNoise);

            weights_file.open();
            settings->setFieldValue(field::ImageCluster::Enabled, true);
            settings->setFieldValue(field::ImageCluster::Model, QStringLiteral("test_cluster_model"));
            settings->setFieldValue(field::ImageCluster::ModelPath, weights_file.fileName());
            settings->setFieldValue(field::ImageCluster::FeatureName, QStringLiteral("test_feature"));
            settings->setFieldValue(field::ImageCluster::ApplyMode, 0); // Move
            settings->setFieldValue(field::ImageCluster::IncludeNoise, false);
        }
    }

    ~ClusterSettingsScope()
    {
        if (settings)
        {
            namespace field = dltool::settings::generated::field;
            settings->setFieldValue(field::ImageCluster::Enabled, old_enabled);
            settings->setFieldValue(field::ImageCluster::Model, old_model);
            settings->setFieldValue(field::ImageCluster::ModelPath, old_modelPath);
            settings->setFieldValue(field::ImageCluster::FeatureName, old_feature);
            settings->setFieldValue(field::ImageCluster::ApplyMode, old_applyMode);
            settings->setFieldValue(field::ImageCluster::IncludeNoise, old_includeNoise);
            settings->setAutoSaveEnabled(old_autoSave);
        }
    }
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

    void searchShutdownRequestRejectsWorkBeforeWaiting()
    {
        dltool::feature::FeatureManager manager(nullptr, nullptr, nullptr, nullptr);
        auto *search = manager.imageSearch();
        search->requestShutdown();
        search->requestShutdown();
        QCOMPARE(search->validationError(), QStringLiteral("图像搜索控制器正在关闭"));
        QVERIFY(!search->search({1}, {}));
        QCOMPARE(search->lastError(), QStringLiteral("图像搜索控制器正在关闭"));
        search->shutdown();
        QVERIFY(!search->isRunning());
    }

    void featureManagerRequestShutdownGatesAllStarts()
    {
        ClusterTestFixture fixture;
        QVERIFY(fixture.init());
        ClusterSettingsScope settings_scope;

        dltool::feature::FeatureManager manager(fixture.data_manager.get(), nullptr, nullptr, nullptr);
        QVERIFY(manager.imageCluster()->validationError().isEmpty());

        manager.requestShutdown();

        // 1. 图像聚类入口校验被拒绝且不启动 worker
        QCOMPARE(manager.imageCluster()->validationError(), QStringLiteral("图像聚类控制器正在关闭"));
        QVERIFY(!manager.imageCluster()->cluster({fixture.dataset_id}));
        QCOMPARE(manager.imageCluster()->lastError(), QStringLiteral("图像聚类控制器正在关闭"));
        QVERIFY(!manager.imageCluster()->isRunning());

        // 2. 标注聚类入口校验被拒绝且不启动 worker
        QCOMPARE(manager.roiCluster()->validationError(), QStringLiteral("标注聚类控制器正在关闭"));
        QVERIFY(!manager.roiCluster()->cluster({QVariantMap{{QStringLiteral("dataset_id"), fixture.dataset_id}}}));
        QCOMPARE(manager.roiCluster()->lastError(), QStringLiteral("标注聚类控制器正在关闭"));
        QVERIFY(!manager.roiCluster()->isRunning());

        // 3. 图像搜索入口校验被拒绝且不启动 worker
        QCOMPARE(manager.imageSearch()->validationError(), QStringLiteral("图像搜索控制器正在关闭"));
        QVERIFY(!manager.imageSearch()->search({fixture.image_ids[0]}, {fixture.dataset_id}));
        QCOMPARE(manager.imageSearch()->lastError(), QStringLiteral("图像搜索控制器正在关闭"));
        QVERIFY(!manager.imageSearch()->isRunning());

        // 4. 小样本学习入口校验被拒绝且不启动 worker
        QCOMPARE(manager.fewShotLearning()->validationError(), QStringLiteral("小样本学习控制器正在关闭"));
        QVERIFY(!manager.fewShotLearning()->startFsSam2());
        QCOMPARE(manager.fewShotLearning()->lastError(), QStringLiteral("小样本学习控制器正在关闭"));
        QVERIFY(!manager.fewShotLearning()->running());

        // 5. 区域检索入口校验被拒绝且不启动 worker
        QCOMPARE(manager.regionSearch()->validationError(), QStringLiteral("区域检索控制器正在关闭"));
        QVERIFY(!manager.regionSearch()->start({}));
        QCOMPARE(manager.regionSearch()->errorText(), QStringLiteral("区域检索控制器正在关闭"));
        QVERIFY(!manager.regionSearch()->isBusy());

        // 6. 智能标注推理入口被拒绝
        const auto infer_res = manager.smartAnnotation()->infer(fixture.image_paths[0], {}, {});
        QVERIFY(!infer_res.value(QStringLiteral("success")).toBool());
        QCOMPARE(infer_res.value(QStringLiteral("error")).toString(), QStringLiteral("智能标注控制器正在关闭"));

        // 7. 验证数据库状态未发生任何写入或污染
        const int64_t count = fixture.database->getImagesCount(fixture.dataset_id);
        QCOMPARE(count, static_cast<int64_t>(fixture.image_ids.size()));
        std::vector<int64_t> datasets;
        std::vector<QString> dataset_names;
        QString db_err;
        QVERIFY(fixture.database->getAllDatasets(datasets, dataset_names, db_err));
        QCOMPARE(datasets.size(), static_cast<size_t>(1));

        // 8. 第二阶段清理平稳幂等完成
        manager.shutdown();
        QVERIFY(!manager.imageCluster()->isRunning());
        QVERIFY(!manager.roiCluster()->isRunning());
        QVERIFY(!manager.imageSearch()->isRunning());
        QVERIFY(!manager.regionSearch()->isBusy());
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

        QVERIFY(manager.regionSearch() != nullptr);
        QCOMPARE(manager.regionSearch()->validationError(), QStringLiteral("区域检索控制器正在关闭"));
        QVERIFY(!manager.regionSearch()->start({}));
        QCOMPARE(manager.regionSearch()->errorText(), QStringLiteral("区域检索控制器正在关闭"));

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

    void staleSmartAnnotationLoadsAreDiscardedAfterCacheClear()
    {
        auto *settings = dltool::settings::GlobalSettings::getInstance();
        QVERIFY(settings != nullptr);

        namespace field = dltool::settings::generated::field;
        const QVariant old_enabled   = settings->valueForField(field::SmartAnnotation::Enabled);
        const QVariant old_model     = settings->valueForField(field::SmartAnnotation::Model);
        const QVariant old_modelPath = settings->valueForField(field::SmartAnnotation::ModelPath);
        const bool     old_auto_save = settings->autoSaveEnabled();
        settings->setAutoSaveEnabled(false);

        QTemporaryFile model_file;
        QVERIFY(model_file.open());
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::Enabled, true));
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::Model, QStringLiteral("edge_sam")));
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::ModelPath, model_file.fileName()));

        SmartModelLoadGate gate;
        dltool::feature::SmartAnnotationController controller(
            [&gate](const QString &, const QString &, const irt::model::ModelRuntime &,
                    const irt::model::ModelPrecision)
                -> std::unique_ptr<irt::features::SAMImagePredictor>
            {
                gate.wait();
                return {};
            });
        QSignalSpy load_finished(&controller, &dltool::feature::SmartAnnotationController::modelLoadFinished);

        QVariantMap point;
        point.insert(QStringLiteral("x"), 1.0);
        point.insert(QStringLiteral("y"), 1.0);
        point.insert(QStringLiteral("label"), 1);
        const QVariantList prompt_points{point};

        const QVariantMap first_result = controller.infer(QStringLiteral("unused"), prompt_points, {});
        QVERIFY(first_result.value(QStringLiteral("loading")).toBool());
        gate.waitUntilCalls(1);

        controller.clearCache();
        const QVariantMap second_result = controller.infer(QStringLiteral("unused"), prompt_points, {});
        QVERIFY(second_result.value(QStringLiteral("loading")).toBool());
        gate.waitUntilCalls(2);

        gate.release();
        QTRY_VERIFY_WITH_TIMEOUT(load_finished.count() == 1, 2000);
        QTest::qWait(100);
        QCOMPARE(load_finished.count(), 1);

        controller.shutdown();
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::Enabled, old_enabled));
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::Model, old_model));
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::ModelPath, old_modelPath));
        settings->setAutoSaveEnabled(old_auto_save);
    }

    void smartAnnotationInferenceIsAsyncAndDoesNotBlockCaller()
    {
        auto *settings = dltool::settings::GlobalSettings::getInstance();
        QVERIFY(settings != nullptr);

        namespace field = dltool::settings::generated::field;
        const QVariant old_enabled   = settings->valueForField(field::SmartAnnotation::Enabled);
        const QVariant old_model     = settings->valueForField(field::SmartAnnotation::Model);
        const QVariant old_modelPath = settings->valueForField(field::SmartAnnotation::ModelPath);
        const bool     old_auto_save = settings->autoSaveEnabled();
        settings->setAutoSaveEnabled(false);

        QTemporaryFile model_file;
        QVERIFY(model_file.open());
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::Enabled, true));
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::Model, QStringLiteral("edge_sam")));
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::ModelPath, model_file.fileName()));

        SmartPredictGate predict_gate;
        dltool::feature::SmartAnnotationController controller(
            [](const QString &, const QString &, const irt::model::ModelRuntime &,
               const irt::model::ModelPrecision)
                -> std::unique_ptr<irt::features::SAMImagePredictor>
            {
                return std::make_unique<irt::features::SAMImagePredictor>();
            },
            [&predict_gate](irt::features::SAMImagePredictor *,
                            const std::filesystem::path &,
                            const irt::features::SAMImagePrompt &,
                            const irt::features::SAMImagePredictOptions &)
                -> irt::features::SAMImagePrediction
            {
                predict_gate.wait();
                irt::features::SAMImagePrediction pred;
                pred.width = 100;
                pred.height = 100;
                pred.mask_count = 1;
                pred.iou_predictions = {0.95F};
                pred.binary_masks.assign(100 * 100, 0);
                for (int y = 20; y < 60; ++y)
                    for (int x = 20; x < 60; ++x)
                        pred.binary_masks[y * 100 + x] = 1;
                return pred;
            });

        QSignalSpy load_spy(&controller, &dltool::feature::SmartAnnotationController::modelLoadFinished);
        QSignalSpy infer_spy(&controller, &dltool::feature::SmartAnnotationController::inferFinished);

        QVariantMap point;
        point.insert(QStringLiteral("x"), 30.0);
        point.insert(QStringLiteral("y"), 30.0);
        point.insert(QStringLiteral("label"), 1);
        const QVariantList prompt_points{point};

        const QString image_path = busImagePath();
        QVERIFY(QFileInfo::exists(image_path));

        // First call loads model asynchronously
        const QVariantMap load_result = controller.infer(image_path, prompt_points, {});
        QVERIFY(load_result.value(QStringLiteral("loading")).toBool());
        QTRY_VERIFY_WITH_TIMEOUT(load_spy.count() == 1, 2000);

        // Second call triggers async infer without blocking caller
        predict_gate.reset();
        const QVariantMap infer_result = controller.infer(image_path, prompt_points, {});
        QVERIFY(infer_result.value(QStringLiteral("pending")).toBool());
        QVERIFY(controller.isRunning());

        // Verify inference reached background executor
        QTRY_VERIFY_WITH_TIMEOUT(predict_gate.started(), 2000);
        QVERIFY(controller.isRunning());
        QCOMPARE(infer_spy.count(), 0);

        // Release predict executor and verify completion
        predict_gate.release();
        QTRY_VERIFY_WITH_TIMEOUT(infer_spy.count() == 1, 2000);
        QVERIFY(!controller.isRunning());

        const QVariantMap final_result = infer_spy.first().first().toMap();
        QVERIFY(final_result.value(QStringLiteral("success")).toBool());
        QCOMPARE(final_result.value(QStringLiteral("image_path")).toString(), image_path);
        QCOMPARE(controller.lastResult(), final_result);

        controller.shutdown();
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::Enabled, old_enabled));
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::Model, old_model));
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::ModelPath, old_modelPath));
        settings->setAutoSaveEnabled(old_auto_save);
    }

    void staleSmartAnnotationInferenceIsDiscardedOnNewRequest()
    {
        auto *settings = dltool::settings::GlobalSettings::getInstance();
        QVERIFY(settings != nullptr);

        namespace field = dltool::settings::generated::field;
        const QVariant old_enabled   = settings->valueForField(field::SmartAnnotation::Enabled);
        const QVariant old_model     = settings->valueForField(field::SmartAnnotation::Model);
        const QVariant old_modelPath = settings->valueForField(field::SmartAnnotation::ModelPath);
        const bool     old_auto_save = settings->autoSaveEnabled();
        settings->setAutoSaveEnabled(false);

        QTemporaryFile model_file;
        QVERIFY(model_file.open());
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::Enabled, true));
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::Model, QStringLiteral("edge_sam")));
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::ModelPath, model_file.fileName()));

        SmartPredictGate predict_gate;
        dltool::feature::SmartAnnotationController controller(
            [](const QString &, const QString &, const irt::model::ModelRuntime &,
               const irt::model::ModelPrecision)
                -> std::unique_ptr<irt::features::SAMImagePredictor>
            {
                return std::make_unique<irt::features::SAMImagePredictor>();
            },
            [&predict_gate](irt::features::SAMImagePredictor *,
                            const std::filesystem::path &,
                            const irt::features::SAMImagePrompt &,
                            const irt::features::SAMImagePredictOptions &)
                -> irt::features::SAMImagePrediction
            {
                predict_gate.wait();
                irt::features::SAMImagePrediction pred;
                pred.width = 100;
                pred.height = 100;
                pred.mask_count = 1;
                pred.iou_predictions = {0.90F};
                pred.binary_masks.assign(100 * 100, 0);
                for (int y = 10; y < 50; ++y)
                    for (int x = 10; x < 50; ++x)
                        pred.binary_masks[y * 100 + x] = 1;
                return pred;
            });

        QSignalSpy load_spy(&controller, &dltool::feature::SmartAnnotationController::modelLoadFinished);
        QSignalSpy infer_spy(&controller, &dltool::feature::SmartAnnotationController::inferFinished);

        const QString image_path = busImagePath();

        QVariantMap point1;
        point1.insert(QStringLiteral("x"), 20.0);
        point1.insert(QStringLiteral("y"), 20.0);
        point1.insert(QStringLiteral("label"), 1);
        const QVariantList prompt_points1{point1};

        // Load model
        controller.infer(image_path, prompt_points1, {});
        QTRY_VERIFY_WITH_TIMEOUT(load_spy.count() == 1, 2000);

        // Request 1 starts and waits at gate
        predict_gate.reset();
        controller.infer(image_path, prompt_points1, {});
        QTRY_VERIFY_WITH_TIMEOUT(predict_gate.started(), 2000);

        // Request 2 supersedes request 1 before request 1 completes
        QVariantMap point2;
        point2.insert(QStringLiteral("x"), 40.0);
        point2.insert(QStringLiteral("y"), 40.0);
        point2.insert(QStringLiteral("label"), 1);
        const QVariantList prompt_points2{point2};

        controller.infer(image_path, prompt_points2, {});

        // Release gate for request 1; request 2 will then run
        predict_gate.release();
        QTRY_VERIFY_WITH_TIMEOUT(infer_spy.count() == 1, 2000);
        QTest::qWait(100);

        // Request 1's late output was rejected; only 1 output was emitted
        QCOMPARE(infer_spy.count(), 1);

        controller.shutdown();
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::Enabled, old_enabled));
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::Model, old_model));
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::ModelPath, old_modelPath));
        settings->setAutoSaveEnabled(old_auto_save);
    }

    void staleSmartAnnotationInferenceIsDiscardedOnModelReplaceAndSettingsChange()
    {
        auto *settings = dltool::settings::GlobalSettings::getInstance();
        QVERIFY(settings != nullptr);

        namespace field = dltool::settings::generated::field;
        const QVariant old_enabled   = settings->valueForField(field::SmartAnnotation::Enabled);
        const QVariant old_model     = settings->valueForField(field::SmartAnnotation::Model);
        const QVariant old_modelPath = settings->valueForField(field::SmartAnnotation::ModelPath);
        const bool     old_auto_save = settings->autoSaveEnabled();
        settings->setAutoSaveEnabled(false);

        QTemporaryFile model_file;
        QVERIFY(model_file.open());
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::Enabled, true));
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::Model, QStringLiteral("edge_sam")));
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::ModelPath, model_file.fileName()));

        SmartPredictGate predict_gate;
        dltool::feature::SmartAnnotationController controller(
            [](const QString &, const QString &, const irt::model::ModelRuntime &,
               const irt::model::ModelPrecision)
                -> std::unique_ptr<irt::features::SAMImagePredictor>
            {
                return std::make_unique<irt::features::SAMImagePredictor>();
            },
            [&predict_gate](irt::features::SAMImagePredictor *,
                            const std::filesystem::path &,
                            const irt::features::SAMImagePrompt &,
                            const irt::features::SAMImagePredictOptions &)
                -> irt::features::SAMImagePrediction
            {
                predict_gate.wait();
                irt::features::SAMImagePrediction pred;
                pred.width = 100;
                pred.height = 100;
                pred.mask_count = 1;
                pred.iou_predictions = {0.90F};
                pred.binary_masks.assign(100 * 100, 1);
                return pred;
            });

        QSignalSpy load_spy(&controller, &dltool::feature::SmartAnnotationController::modelLoadFinished);
        QSignalSpy infer_spy(&controller, &dltool::feature::SmartAnnotationController::inferFinished);

        const QString image_path = busImagePath();
        QVariantMap point;
        point.insert(QStringLiteral("x"), 20.0);
        point.insert(QStringLiteral("y"), 20.0);
        point.insert(QStringLiteral("label"), 1);

        controller.infer(image_path, {point}, {});
        QTRY_VERIFY_WITH_TIMEOUT(load_spy.count() == 1, 2000);

        // Start infer and block in executor
        predict_gate.reset();
        controller.infer(image_path, {point}, {});
        QTRY_VERIFY_WITH_TIMEOUT(predict_gate.started(), 2000);

        // Settings change invalidates model while inference is in flight
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::Model, QStringLiteral("sam_vit_b")));

        // Release executor
        predict_gate.release();
        QTest::qWait(150);

        // Stale result from replaced model must be discarded
        QCOMPARE(infer_spy.count(), 0);

        controller.shutdown();
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::Enabled, old_enabled));
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::Model, old_model));
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::ModelPath, old_modelPath));
        settings->setAutoSaveEnabled(old_auto_save);
    }

    void smartAnnotationShutdownWaitsForInferenceAndRejectsLateOutput()
    {
        auto *settings = dltool::settings::GlobalSettings::getInstance();
        QVERIFY(settings != nullptr);

        namespace field = dltool::settings::generated::field;
        const QVariant old_enabled   = settings->valueForField(field::SmartAnnotation::Enabled);
        const QVariant old_model     = settings->valueForField(field::SmartAnnotation::Model);
        const QVariant old_modelPath = settings->valueForField(field::SmartAnnotation::ModelPath);
        const bool     old_auto_save = settings->autoSaveEnabled();
        settings->setAutoSaveEnabled(false);

        QTemporaryFile model_file;
        QVERIFY(model_file.open());
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::Enabled, true));
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::Model, QStringLiteral("edge_sam")));
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::ModelPath, model_file.fileName()));

        SmartPredictGate predict_gate;
        dltool::feature::SmartAnnotationController controller(
            [](const QString &, const QString &, const irt::model::ModelRuntime &,
               const irt::model::ModelPrecision)
                -> std::unique_ptr<irt::features::SAMImagePredictor>
            {
                return std::make_unique<irt::features::SAMImagePredictor>();
            },
            [&predict_gate](irt::features::SAMImagePredictor *,
                            const std::filesystem::path &,
                            const irt::features::SAMImagePrompt &,
                            const irt::features::SAMImagePredictOptions &)
                -> irt::features::SAMImagePrediction
            {
                predict_gate.wait();
                irt::features::SAMImagePrediction pred;
                pred.width = 100;
                pred.height = 100;
                pred.mask_count = 1;
                pred.iou_predictions = {0.90F};
                pred.binary_masks.assign(100 * 100, 1);
                return pred;
            });

        QSignalSpy load_spy(&controller, &dltool::feature::SmartAnnotationController::modelLoadFinished);
        QSignalSpy infer_spy(&controller, &dltool::feature::SmartAnnotationController::inferFinished);

        const QString image_path = busImagePath();
        QVariantMap point;
        point.insert(QStringLiteral("x"), 20.0);
        point.insert(QStringLiteral("y"), 20.0);
        point.insert(QStringLiteral("label"), 1);

        controller.infer(image_path, {point}, {});
        QTRY_VERIFY_WITH_TIMEOUT(load_spy.count() == 1, 2000);

        predict_gate.reset();
        controller.infer(image_path, {point}, {});
        QTRY_VERIFY_WITH_TIMEOUT(predict_gate.started(), 2000);

        std::thread releaser([&predict_gate]()
                             {
                                 QThread::msleep(50);
                                 predict_gate.release();
                             });

        controller.shutdown();
        releaser.join();

        QVERIFY(!controller.isRunning());
        QCOMPARE(infer_spy.count(), 0);

        // After shutdown, infer immediately rejects new work
        const QVariantMap closed_result = controller.infer(image_path, {point}, {});
        QVERIFY(!closed_result.value(QStringLiteral("success")).toBool());
        QCOMPARE(closed_result.value(QStringLiteral("error")).toString(), QStringLiteral("智能标注控制器正在关闭"));

        QVERIFY(settings->setFieldValue(field::SmartAnnotation::Enabled, old_enabled));
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::Model, old_model));
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::ModelPath, old_modelPath));
        settings->setAutoSaveEnabled(old_auto_save);
    }

    void smartAnnotationRealResourceRegionVerification()
    {
        auto *settings = dltool::settings::GlobalSettings::getInstance();
        QVERIFY(settings != nullptr);

        namespace field = dltool::settings::generated::field;
        const QVariant old_enabled   = settings->valueForField(field::SmartAnnotation::Enabled);
        const QVariant old_model     = settings->valueForField(field::SmartAnnotation::Model);
        const QVariant old_modelPath = settings->valueForField(field::SmartAnnotation::ModelPath);
        const bool     old_auto_save = settings->autoSaveEnabled();
        settings->setAutoSaveEnabled(false);

        QTemporaryFile model_file;
        QVERIFY(model_file.open());
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::Enabled, true));
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::Model, QStringLiteral("edge_sam")));
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::ModelPath, model_file.fileName()));

        dltool::feature::SmartAnnotationController controller(
            [](const QString &, const QString &, const irt::model::ModelRuntime &,
               const irt::model::ModelPrecision)
                -> std::unique_ptr<irt::features::SAMImagePredictor>
            {
                return std::make_unique<irt::features::SAMImagePredictor>();
            },
            [](irt::features::SAMImagePredictor *,
               const std::filesystem::path &,
               const irt::features::SAMImagePrompt &,
               const irt::features::SAMImagePredictOptions &)
                -> irt::features::SAMImagePrediction
            {
                // Predicts a 40x40 region in a 200x200 viewport
                irt::features::SAMImagePrediction pred;
                pred.width = 200;
                pred.height = 200;
                pred.mask_count = 1;
                pred.iou_predictions = {0.99F};
                pred.binary_masks.assign(200 * 200, 0);
                for (int y = 50; y < 90; ++y)
                    for (int x = 50; x < 90; ++x)
                        pred.binary_masks[y * 200 + x] = 1;
                return pred;
            });

        QSignalSpy load_spy(&controller, &dltool::feature::SmartAnnotationController::modelLoadFinished);
        QSignalSpy infer_spy(&controller, &dltool::feature::SmartAnnotationController::inferFinished);

        const QString real_image_path = busImagePath();
        QVERIFY2(QFileInfo::exists(real_image_path), qPrintable(real_image_path));
        const QImage real_image(real_image_path);
        QVERIFY(!real_image.isNull());
        QVERIFY(real_image.width() > 200 && real_image.height() > 200);

        QVariantMap point;
        point.insert(QStringLiteral("x"), 160.0);
        point.insert(QStringLiteral("y"), 160.0);
        point.insert(QStringLiteral("label"), 1);

        QVariantMap options;
        options.insert(QStringLiteral("use_viewport_input"), true);
        options.insert(QStringLiteral("viewport"), QVariantMap{
            {QStringLiteral("x"), 100.0},
            {QStringLiteral("y"), 100.0},
            {QStringLiteral("width"), 200.0},
            {QStringLiteral("height"), 200.0},
            {QStringLiteral("input_width"), 200},
            {QStringLiteral("input_height"), 200}
        });

        // Load model
        controller.infer(real_image_path, {point}, options);
        QTRY_VERIFY_WITH_TIMEOUT(load_spy.count() == 1, 2000);

        // Perform infer with viewport mapping
        controller.infer(real_image_path, {point}, options);
        QTRY_VERIFY_WITH_TIMEOUT(infer_spy.count() == 1, 2000);

        const QVariantMap result = infer_spy.first().first().toMap();
        QVERIFY(result.value(QStringLiteral("success")).toBool());
        QCOMPARE(result.value(QStringLiteral("image_width")).toInt(), real_image.width());
        QCOMPARE(result.value(QStringLiteral("image_height")).toInt(), real_image.height());

        // Viewport offset was x=100, y=100. Predictor mask was [50, 90].
        // Mapped source coords should be around [150, 190].
        const double bbox_x = result.value(QStringLiteral("x")).toDouble();
        const double bbox_y = result.value(QStringLiteral("y")).toDouble();
        const double bbox_w = result.value(QStringLiteral("width")).toDouble();
        const double bbox_h = result.value(QStringLiteral("height")).toDouble();

        QVERIFY(bbox_x >= 145.0 && bbox_x <= 155.0);
        QVERIFY(bbox_y >= 145.0 && bbox_y <= 155.0);
        QVERIFY(bbox_w >= 38.0 && bbox_w <= 42.0);
        QVERIFY(bbox_h >= 38.0 && bbox_h <= 42.0);

        const QVariantList points = result.value(QStringLiteral("points")).toList();
        QVERIFY(points.size() >= 3);
        const QVariantList mask_runs = result.value(QStringLiteral("mask_runs")).toList();
        QVERIFY(!mask_runs.isEmpty());

        controller.shutdown();
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::Enabled, old_enabled));
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::Model, old_model));
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::ModelPath, old_modelPath));
        settings->setAutoSaveEnabled(old_auto_save);
    }

    void smartAnnotationRealModelInferenceVerification()
    {
        const QString real_model_path = QStringLiteral("F:/models/edgesam/edge_sam/edge_sam.wts");
        QVERIFY2(QFileInfo::exists(real_model_path),
                 qPrintable(QString("真实 SAM 模型权重文件必须存在以执行推理验证: %1").arg(real_model_path)));

        auto *settings = dltool::settings::GlobalSettings::getInstance();
        QVERIFY(settings != nullptr);

        namespace field = dltool::settings::generated::field;
        const QVariant old_enabled   = settings->valueForField(field::SmartAnnotation::Enabled);
        const QVariant old_model     = settings->valueForField(field::SmartAnnotation::Model);
        const QVariant old_modelPath = settings->valueForField(field::SmartAnnotation::ModelPath);
        const QVariant old_runtime   = settings->valueForField(field::SmartAnnotation::ModelRuntime);
        const bool     old_auto_save = settings->autoSaveEnabled();
        settings->setAutoSaveEnabled(false);

        QVERIFY(settings->setFieldValue(field::SmartAnnotation::Enabled, true));
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::Model, QStringLiteral("edge_sam")));
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::ModelPath, real_model_path));
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::ModelRuntime, QStringLiteral("tensorrt:0")));

        // 使用默认无 mock 的控制器，直接拉起底层生产 Predictor 执行器
        dltool::feature::SmartAnnotationController controller;

        QSignalSpy load_spy(&controller, &dltool::feature::SmartAnnotationController::modelLoadFinished);
        QSignalSpy infer_spy(&controller, &dltool::feature::SmartAnnotationController::inferFinished);

        const QString real_image_path = busImagePath();
        QVERIFY2(QFileInfo::exists(real_image_path), qPrintable(real_image_path));
        const QImage real_image(real_image_path);
        QVERIFY(!real_image.isNull());

        // 点提示设定在图像中央 (bus 目标前景区域)
        QVariantMap point;
        point.insert(QStringLiteral("x"), static_cast<double>(real_image.width()) * 0.5);
        point.insert(QStringLiteral("y"), static_cast<double>(real_image.height()) * 0.5);
        point.insert(QStringLiteral("label"), 1);

        // 1. 首次触发 infer 会启动异步模型加载
        controller.infer(real_image_path, {point}, {});
        QTRY_VERIFY_WITH_TIMEOUT(load_spy.count() == 1, 15000);
        QVERIFY2(load_spy.first().first().toBool(), "EdgeSAM 模型加载失败");

        // 2. 模型已就绪，触发真实 SAM 推理并记录耗时
        QElapsedTimer timer;
        timer.start();
        controller.infer(real_image_path, {point}, {});

        // 等待 TensorRT 推理完成
        QTRY_VERIFY_WITH_TIMEOUT(infer_spy.count() == 1, 15000);
        const qint64 elapsed_ms = timer.elapsed();

        const QVariantMap result = infer_spy.first().first().toMap();
        const bool success = result.value(QStringLiteral("success")).toBool();
        const QString err = result.value(QStringLiteral("error")).toString();
        QVERIFY2(success, qPrintable(QString("真实 SAM 推理失败: %1").arg(err)));

        const QVariantList points = result.value(QStringLiteral("points")).toList();
        QVERIFY(!points.isEmpty());
        QVERIFY(points.size() >= 3);

        const QVariantList mask_runs = result.value(QStringLiteral("mask_runs")).toList();
        QVERIFY(!mask_runs.isEmpty());

        const double iou = result.value(QStringLiteral("iou")).toDouble();
        QVERIFY(iou > 0.0);

        const int pixel_count = result.value(QStringLiteral("mask_pixel_count")).toInt();
        QVERIFY(pixel_count > 0);

        qInfo() << "[Evidence Ticket 29] Real SAM model inference verified:"
                << "model=edge_sam"
                << "runtime=tensorrt:0"
                << "elapsed_ms=" << elapsed_ms
                << "polygon_points=" << points.size()
                << "mask_runs=" << mask_runs.size()
                << "mask_pixels=" << pixel_count
                << "iou=" << iou;

        controller.shutdown();
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::Enabled, old_enabled));
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::Model, old_model));
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::ModelPath, old_modelPath));
        QVERIFY(settings->setFieldValue(field::SmartAnnotation::ModelRuntime, old_runtime));
        settings->setAutoSaveEnabled(old_auto_save);
    }

    void clusterFreezesSourcesAndRejectsWritebackConflicts()
    {
        ClusterTestFixture fixture;
        QVERIFY(fixture.init(3));
        ClusterSettingsScope settings_scope;

        int64_t dataset_b_id = -1;
        QString db_err;
        QVERIFY(fixture.data_manager->ensureDataset(QStringLiteral("other_dataset"), dataset_b_id, db_err));

        std::atomic_bool cluster_started{false};
        std::atomic_bool cluster_proceed{false};

        dltool::feature::ImageClusterDataProvider provider(fixture.data_manager.get());
        dltool::feature::ImageClusterController controller(
            &provider, fixture.data_manager.get(),
            [&](const dltool::feature::ImageClusterController::ClusterRequest &,
                dltool::feature::ImageClusterController::ClusterResponse &resp,
                const irt::features::ImageClusterProgressCallback &)
            {
                cluster_started.store(true, std::memory_order_release);
                while (!cluster_proceed.load(std::memory_order_acquire))
                {
                    QThread::msleep(10);
                }
                resp.success = true;
                resp.cluster_count = 2;
                resp.noise_count = 0;
                resp.assignments = {
                    {fixture.image_ids[0], 0, 1.0f},
                    {fixture.image_ids[1], 1, 1.0f},
                    {fixture.image_ids[2], 0, 1.0f}
                };
            });

        QVariantMap scope_item;
        scope_item.insert(QStringLiteral("dataset_id"), fixture.dataset_id);
        QVERIFY(controller.cluster({scope_item}));

        QTRY_VERIFY_WITH_TIMEOUT(cluster_started.load(std::memory_order_acquire), 2000);

        bool move_finished = false;
        fixture.data_manager->moveToDatasetAsync(
            {fixture.image_ids[1]}, dataset_b_id, nullptr,
            [&move_finished](bool ok, const QString &) { move_finished = ok; }, false);
        QTRY_VERIFY_WITH_TIMEOUT(move_finished, 2000);

        cluster_proceed.store(true, std::memory_order_release);

        QTRY_VERIFY_WITH_TIMEOUT(!controller.isRunning(), 3000);

        QVERIFY(!controller.hasResults());
        QCOMPARE(controller.resultCount(), 0);
        QVERIFY(controller.lastError().contains(QStringLiteral("聚类写回冲突")));

        std::vector<int64_t> d_ids;
        std::vector<QString> d_names;
        QVERIFY(fixture.database->getAllDatasets(d_ids, d_names, db_err));
        for (const QString &name : d_names)
        {
            QVERIFY(!name.contains(QStringLiteral("cluster")));
        }
        controller.shutdown();
    }

    void clusterSingleDatabaseWritebackIsAtomicWithNoResidualDatasetsOnFailure()
    {
        ClusterTestFixture fixture;
        QVERIFY(fixture.init(3));

        dltool::database::ProjectDataBase::ClusterTarget t0;
        t0.target_dataset_name = QStringLiteral("train_set_cluster_0");
        t0.move_image_ids      = {fixture.image_ids[0]};

        dltool::database::ProjectDataBase::ClusterTarget t1;
        t1.target_dataset_name = QStringLiteral("train_set_cluster_1");
        t1.move_image_ids      = {fixture.image_ids[1], fixture.image_ids[2]};

        // Cancellation mid-transaction rolls back all datasets and image moves
        {
            dltool::database::ProjectDataBase::AtomicClusterOutput output;
            QString db_err;
            const bool ok = fixture.database->applyClusterAtomic(
                {t0, t1}, output, db_err, []() { return true; });
            QVERIFY(!ok);
            QCOMPARE(db_err, QStringLiteral("操作已取消"));

            std::vector<int64_t> d_ids;
            std::vector<QString> d_names;
            QVERIFY(fixture.database->getAllDatasets(d_ids, d_names, db_err));
            QCOMPARE(d_ids.size(), 1);
            QCOMPARE(d_names.front(), QStringLiteral("train_set"));

            std::vector<int64_t> img_ids;
            std::vector<QString> img_paths;
            QVERIFY(fixture.database->getImages(fixture.dataset_id, img_ids, img_paths, db_err));
            QCOMPARE(img_ids.size(), 3);
        }

        // Invalid target rolls back completely
        {
            dltool::database::ProjectDataBase::ClusterTarget bad_target;
            bad_target.target_dataset_name = QString();
            bad_target.move_image_ids      = {fixture.image_ids[0]};

            dltool::database::ProjectDataBase::AtomicClusterOutput output;
            QString db_err;
            const bool ok = fixture.database->applyClusterAtomic(
                {bad_target}, output, db_err);
            QVERIFY(!ok);
            QVERIFY(!db_err.isEmpty());

            std::vector<int64_t> d_ids;
            std::vector<QString> d_names;
            QVERIFY(fixture.database->getAllDatasets(d_ids, d_names, db_err));
            QCOMPARE(d_ids.size(), 1);
        }
    }

    void clusterCancellationLeavesNoResidualDatasets()
    {
        ClusterTestFixture fixture;
        QVERIFY(fixture.init(2));
        ClusterSettingsScope settings_scope;

        std::atomic_bool started{false};
        std::atomic_bool can_exit{false};

        dltool::feature::ImageClusterDataProvider provider(fixture.data_manager.get());
        dltool::feature::ImageClusterController controller(
            &provider, fixture.data_manager.get(),
            [&](const dltool::feature::ImageClusterController::ClusterRequest &req,
                dltool::feature::ImageClusterController::ClusterResponse &resp,
                const irt::features::ImageClusterProgressCallback &)
            {
                started.store(true, std::memory_order_release);
                while (!req.cancellationRequested() && !can_exit.load(std::memory_order_acquire))
                {
                    QThread::msleep(10);
                }
                if (req.cancellationRequested())
                {
                    resp.success = false;
                    resp.error = QStringLiteral("图像聚类已取消");
                    return;
                }
                resp.success = true;
                resp.assignments = {
                    {fixture.image_ids[0], 0, 1.0f},
                    {fixture.image_ids[1], 1, 1.0f}
                };
            });

        QVariantMap scope_item;
        scope_item.insert(QStringLiteral("dataset_id"), fixture.dataset_id);
        QVERIFY(controller.cluster({scope_item}));
        QTRY_VERIFY_WITH_TIMEOUT(started.load(std::memory_order_acquire), 2000);

        controller.shutdown();
        can_exit.store(true, std::memory_order_release);

        QVERIFY(!controller.isRunning());
        QVERIFY(!controller.hasResults());

        std::vector<int64_t> d_ids;
        std::vector<QString> d_names;
        QString db_err;
        QVERIFY(fixture.database->getAllDatasets(d_ids, d_names, db_err));
        QCOMPARE(d_ids.size(), 1);
        QCOMPARE(fixture.data_manager->datasets()->rowCount(), 1);
    }

    void clusterStaleCallbacksAreRejected()
    {
        ClusterTestFixture fixture;
        QVERIFY(fixture.init(2));
        ClusterSettingsScope settings_scope;

        dltool::feature::ImageClusterDataProvider provider(fixture.data_manager.get());
        dltool::feature::ImageClusterController controller(&provider, fixture.data_manager.get());

        controller.shutdown();
        QVERIFY(!controller.isRunning());
        QCOMPARE(controller.validationError(), QStringLiteral("图像聚类控制器正在关闭"));

        QVariantMap scope_item;
        scope_item.insert(QStringLiteral("dataset_id"), fixture.dataset_id);
        QVERIFY(!controller.cluster({scope_item}));
        QCOMPARE(controller.lastError(), QStringLiteral("图像聚类控制器正在关闭"));
    }

    void clusterRealResourceEndToEnd()
    {
        ClusterTestFixture fixture;
        QVERIFY(fixture.init(2));
        ClusterSettingsScope settings_scope;

        dltool::feature::ImageClusterDataProvider provider(fixture.data_manager.get());
        dltool::feature::ImageClusterController controller(
            &provider, fixture.data_manager.get(),
            [&](const dltool::feature::ImageClusterController::ClusterRequest &,
                dltool::feature::ImageClusterController::ClusterResponse &resp,
                const irt::features::ImageClusterProgressCallback &progress)
            {
                irt::features::ImageClusterProgress prog;
                prog.stage = irt::features::ImageClusterStage::Clustering;
                prog.processed_count = 2;
                prog.total_count = 2;
                progress(prog);

                resp.success       = true;
                resp.cluster_count = 2;
                resp.noise_count   = 0;
                resp.summary       = QStringLiteral("图像聚类完成: 2 张图像, 2 个簇, 噪声 0 张");
                resp.assignments   = {
                    {fixture.image_ids[0], 0, 0.99f},
                    {fixture.image_ids[1], 1, 0.95f}
                };
            });

        QSignalSpy results_spy(&controller, &dltool::feature::ImageClusterController::resultsChanged);
        QVariantMap scope_item;
        scope_item.insert(QStringLiteral("dataset_id"), fixture.dataset_id);

        QVERIFY(controller.cluster({scope_item}));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.isRunning(), 5000);

        QVERIFY(controller.hasResults());
        QCOMPARE(controller.resultCount(), 2);
        QVERIFY(controller.lastError().isEmpty());
        QVERIFY(!controller.lastSummary().isEmpty());
        QVERIFY(results_spy.count() >= 1);

        QCOMPARE(fixture.data_manager->datasets()->rowCount(), 3);

        const int64_t ds0_id = fixture.data_manager->imageDatasetId(fixture.image_ids[0]);
        const int64_t ds1_id = fixture.data_manager->imageDatasetId(fixture.image_ids[1]);
        QVERIFY(ds0_id != fixture.dataset_id);
        QVERIFY(ds1_id != fixture.dataset_id);
        QVERIFY(ds0_id != ds1_id);

        const QString ds0_name = fixture.data_manager->getDatasetName(static_cast<int>(ds0_id));
        const QString ds1_name = fixture.data_manager->getDatasetName(static_cast<int>(ds1_id));
        QCOMPARE(ds0_name, QStringLiteral("train_set-0"));
        QCOMPARE(ds1_name, QStringLiteral("train_set-1"));

        std::vector<int64_t> db_ds_ids;
        std::vector<QString> db_ds_names;
        QString db_err;
        QVERIFY(fixture.database->getAllDatasets(db_ds_ids, db_ds_names, db_err));
        QCOMPARE(db_ds_ids.size(), 3);

        controller.shutdown();
    }
};

} // namespace

QTEST_GUILESS_MAIN(FeatureLifecycleTest)

#include "test_FeatureLifecycle.moc"
