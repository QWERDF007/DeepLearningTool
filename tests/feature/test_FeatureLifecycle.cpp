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
#include "settings/GlobalSettings.h"
#include "ui/ProgressManager.h"

#include <inferrt/features/SAMImagePredictor.hpp>

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

        const QString image_path = QStringLiteral("F:/Projects/DeepLearningTool/3rdparty/EasyTrain/src/python/ultralytics/ultralytics/ultralytics/assets/bus.jpg");
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

        const QString image_path = QStringLiteral("F:/Projects/DeepLearningTool/3rdparty/EasyTrain/src/python/ultralytics/ultralytics/ultralytics/assets/bus.jpg");

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

        const QString image_path = QStringLiteral("F:/Projects/DeepLearningTool/3rdparty/EasyTrain/src/python/ultralytics/ultralytics/ultralytics/assets/bus.jpg");
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

        const QString image_path = QStringLiteral("F:/Projects/DeepLearningTool/3rdparty/EasyTrain/src/python/ultralytics/ultralytics/ultralytics/assets/bus.jpg");
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

        const QString real_image_path = QStringLiteral("F:/Projects/DeepLearningTool/3rdparty/EasyTrain/src/python/ultralytics/ultralytics/ultralytics/assets/bus.jpg");
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
};

} // namespace

QTEST_GUILESS_MAIN(FeatureLifecycleTest)

#include "test_FeatureLifecycle.moc"
