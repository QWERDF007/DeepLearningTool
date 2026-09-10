#include "../test_runner.h"

#include "TestFixture.h"

#include "database/DataBase.h"
#include "database/ModelTaskDataBase.h"
#include "model/AggregateEvaluation.h"
#include "model/AnomalyPreprocessingTransform.h"
#include "model/EvaluationDataset.h"
#include "model/DetectionEvaluationEngine.h"
#include "model/EvaluationEngineRegistry.h"
#include "model/EvaluationViewModelRegistry.h"
#include "model/IParams.h"
#include "model/ModelParamDefs.h"
#include "model/ModelManager.h"
#include "model/ModelStorageService.h"
#include "model/ModelTestTaskManager.h"
#include "model/TaskManager.h"
#include "model/detail/EvaluationImageRequestCache.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

#include <opencv2/opencv.hpp>

#include <atomic>
#include <cmath>
#include <future>
#include <vector>

using namespace dltool::model;
using namespace dltool::model::testsupport;

namespace {

class ControlledShutdownEvaluationEngine final : public DetectionEvaluationEngine
{
public:
    inline static std::atomic_int entered_count{0};
    inline static std::atomic_int active_count{0};
    inline static std::atomic_int cancel_observed_count{0};

    static void reset()
    {
        entered_count.store(0, std::memory_order_relaxed);
        active_count.store(0, std::memory_order_relaxed);
        cancel_observed_count.store(0, std::memory_order_relaxed);
    }

protected:
    bool computeInstanceCounts(const QMap<qint64, EvaluationImageData> &images, const QMap<int, QString> &classes,
                               QMap<int, EvaluationCounts> &per_class, EvaluationCounts &overall,
                               QString *err_msg) override
    {
        active_count.fetch_add(1, std::memory_order_relaxed);
        entered_count.fetch_add(1, std::memory_order_release);
        while (!cancelled(scratch_.cancel_token))
        {
            QThread::msleep(2);
        }
        cancel_observed_count.fetch_add(1, std::memory_order_release);
        active_count.fetch_sub(1, std::memory_order_relaxed);
        return DetectionEvaluationEngine::computeInstanceCounts(images, classes, per_class, overall, err_msg);
    }
};

struct RestoreDetectionEvaluationEngine
{
    ~RestoreDetectionEvaluationEngine()
    {
        EvaluationEngineRegistry::instance().registerEngine(
            evaluation::Method::Detection, []() { return std::make_unique<DetectionEvaluationEngine>(); });
    }
};

ParamGroupModel *findGroup(IParams *params, const QString &name)
{
    if (params == nullptr)
        return nullptr;
    for (QObject *object : params->groupObjects())
    {
        auto *group = qobject_cast<ParamGroupModel *>(object);
        if (group != nullptr && group->nameEn() == name)
            return group;
    }
    return nullptr;
}

bool prepareEvaluationInputs(EvaluationFixture &fixture, const ModelTestTaskManager &manager,
                             const ModelManager::ModelRecordView &record, const qint64 image_id,
                             const QVariant &prediction, const bool write_prediction, QString *error)
{
    const ModelStorageService storage(fixture.rootPath());
    const QString             file_list_path
        = storage.testTaskFileListPath(record.name, manager.currentTaskDirectory());
    const QString task_database_path = storage.testTaskDatabasePath(record.name, manager.currentTaskDirectory());
    const QString prediction_dir
        = storage.testTaskPredictionPath(record.name, manager.currentTaskDirectory());
    if (file_list_path.isEmpty() || task_database_path.isEmpty())
    {
        if (error != nullptr)
            *error = QStringLiteral("测试任务存储路径为空");
        return false;
    }
    if (QFileInfo::exists(file_list_path) && !QFile::remove(file_list_path))
    {
        if (error != nullptr)
            *error = QString("删除旧测试图像列表失败: %1").arg(file_list_path);
        return false;
    }
    if (!QFile::copy(fixture.fileListPath(), file_list_path))
    {
        if (error != nullptr)
            *error = QString("复制测试图像列表失败: %1").arg(file_list_path);
        return false;
    }

    dltool::database::ModelTaskDataBase task_database(task_database_path);
    if (!task_database.replaceDatasets({
            {QStringLiteral("test"), fixture.datasetId(), fixture.classIds()}
    }, error))
        return false;
    if (write_prediction && !task_database.upsertPrediction({image_id, prediction}, error))
        return false;

    // Anomaly evaluation consumes the task prediction TIFF, not the task.db
    // image_score convenience field. Keep this helper's anomaly fixture on
    // the same artifact path used by the production evaluator.
    const QVariantMap value = prediction.toMap();
    bool              score_ok = false;
    const double      score    = value.value(QStringLiteral("image_score")).toDouble(&score_ok);
    if (write_prediction && value.contains(QStringLiteral("image_score")) && score_ok && std::isfinite(score))
    {
        if (!QDir().mkpath(prediction_dir))
        {
            if (error != nullptr)
                *error = QString("创建预测目录失败: %1").arg(prediction_dir);
            return false;
        }
        if (!fixture.writePrediction(image_id, prediction))
        {
            if (error != nullptr)
                *error = fixture.error();
            return false;
        }
        const QString source_path = QDir(fixture.predictionDirectory()).filePath(QStringLiteral("%1.tiff").arg(image_id));
        const QString target_path = QDir(prediction_dir).filePath(QStringLiteral("%1.tiff").arg(image_id));
        if (!QFile::copy(source_path, target_path))
        {
            if (error != nullptr)
                *error = QString("复制异常分数图失败: %1").arg(target_path);
            return false;
        }
    }

    QList<dltool::database::DatasetSelectionRecord> selections;
    if (!task_database.readDatasets(selections, error) || selections.size() != 1
        || selections.front().type != QStringLiteral("test") || selections.front().class_ids.isEmpty())
    {
        if (error != nullptr && error->isEmpty())
            *error = QStringLiteral("测试夹具写入数据集选择后无法读回");
        return false;
    }
    return true;
}

} // namespace

class ModelEvaluationParameterBehaviorTest : public QObject
{
    Q_OBJECT

private slots:
    void parameterGroupsControlEvaluationTrigger()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 cat   = fixture.addClass(QStringLiteral("Cat"), QStringLiteral("normal"));
        const qint64 image = fixture.addImage(QStringLiteral("cat"));
        QVERIFY(cat >= 0);
        QVERIFY(image >= 0);
        QVERIFY(fixture.addDetectionLabel(image, cat, 0, 0, 10, 10) >= 0);
        QVERIFY(fixture.writeImageList());

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager model_manager(static_cast<int>(evaluation::Method::Detection), &database, nullptr);
        QString      error;
        const auto record = model_manager.addModelRecord(QStringLiteral("Managed"), QStringLiteral("ultralytics"),
                                                         QStringLiteral("YOLOv8"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        TaskManager task_manager_instance;
        TaskManager *task_manager = &task_manager_instance;
        ModelTestTaskManager manager(fixture.rootPath(), &model_manager, nullptr, task_manager);
        manager.setModelUuid(record.uuid);
        QVERIFY(manager.currentEvaluation() != nullptr);
        QVERIFY(manager.currentTestParams() != nullptr);
        QVERIFY2(prepareEvaluationInputs(fixture, manager, record, image,
                                         detectionPrediction(static_cast<int>(cat), QStringLiteral("Cat"), 0.9, 0, 0,
                                                             10, 10),
                                         true, &error),
                 qPrintable(error));

        auto *evaluation = manager.currentEvaluation();
        QSignalSpy evaluation_completed(evaluation, &ModelEvaluationViewModel::evaluationCompleted);
        evaluation->evaluate(false);
        QTRY_COMPARE_WITH_TIMEOUT(evaluation->stateKind(), ModelEvaluationViewModel::Ready, 5000);
        QVERIFY(evaluation->available());
        QCOMPARE(evaluation_completed.count(), 1);

        // The first successful evaluation applies the searched optimum in the
        // same result publication, so the effective threshold is the single
        // prediction score in this fixture.
        QCOMPARE(evaluation->confidenceThreshold(), 0.9);
        // 验收条件 1 & 3: 首评应用标记有唯一持久化来源 task.db，不保留 extra_data.test_tasks 平行权威入口
        const QVariantMap extra_data = model_manager.modelRecordForUuid(record.uuid).value(QStringLiteral("extra_data")).toMap();
        QVERIFY(!extra_data.contains(QStringLiteral("test_tasks")));

        const ModelStorageService storage(fixture.rootPath());
        const QString task_db_path = storage.testTaskDatabasePath(record.name, manager.currentTaskDirectory());
        dltool::database::ModelTaskDataBase task_database(task_db_path);
        bool applied = false;
        QVERIFY(task_database.readAdaptiveThresholdApplied(applied));
        QVERIFY(applied);

        auto *inference = findGroup(manager.currentTestParams(), QStringLiteral("inference"));
        auto *evaluation_params = findGroup(manager.currentTestParams(), QStringLiteral("evaluation"));
        QVERIFY(inference != nullptr);
        QVERIFY(evaluation_params != nullptr);
        QCOMPARE(evaluation_params->valueForName(QStringLiteral("conf")).toDouble(), 0.9);
        QVERIFY(inference->fieldMapForName(QStringLiteral("conf")).size() > 0);
        QVERIFY(inference->fieldMapForName(QStringLiteral("iou")).size() > 0);
        QVERIFY(inference->fieldMapForName(QStringLiteral("max_det")).size() > 0);
        QVERIFY(evaluation_params->fieldMapForName(QStringLiteral("conf")).size() > 0);
        QVERIFY(evaluation_params->fieldMapForName(QStringLiteral("iou")).size() > 0);
        QVERIFY(evaluation_params->fieldMapForName(QStringLiteral("matching_strategy")).size() > 0);

        QSignalSpy evaluation_changed(evaluation, &ModelEvaluationViewModel::evaluationChanged);
        QSignalSpy loading_changed(evaluation, &ModelEvaluationViewModel::loadingChanged);
        const int original_batch = inference->valueForName(QStringLiteral("batch_size")).toInt();
        QVERIFY(inference->setValueForName(QStringLiteral("batch_size"), original_batch + 1));
        const double original_inference_conf = inference->valueForName(QStringLiteral("conf")).toDouble();
        QVERIFY(inference->setValueForName(QStringLiteral("conf"), original_inference_conf > 0.5 ? 0.25 : 0.75));
        const double original_inference_iou = inference->valueForName(QStringLiteral("iou")).toDouble();
        QVERIFY(inference->setValueForName(QStringLiteral("iou"), original_inference_iou > 0.5 ? 0.25 : 0.75));
        const int original_max_det = inference->valueForName(QStringLiteral("max_det")).toInt();
        QVERIFY(inference->setValueForName(QStringLiteral("max_det"), original_max_det + 1));
        QCOMPARE(evaluation->stateKind(), ModelEvaluationViewModel::Ready);
        QVERIFY(evaluation->available());
        QCOMPARE(evaluation->confidenceThreshold(), 0.9);
        QCOMPARE(evaluation->iouThreshold(), 0.5);
        QCOMPARE(evaluation_changed.count(), 0);
        QCOMPARE(loading_changed.count(), 0);

        QVERIFY(evaluation_params->setValueForName(QStringLiteral("conf"), 0.6));
        QTRY_VERIFY_WITH_TIMEOUT(loading_changed.count() >= 2, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(evaluation->stateKind(), ModelEvaluationViewModel::Ready, 5000);
        QVERIFY(evaluation->available());
        QCOMPARE(evaluation->confidenceThreshold(), 0.6);
        QCOMPARE(evaluation->bestThreshold(), 0.9);

        QVERIFY(evaluation_params->setValueForName(QStringLiteral("iou"), 0.7));
        QTRY_VERIFY_WITH_TIMEOUT(loading_changed.count() >= 4, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(evaluation->stateKind(), ModelEvaluationViewModel::Ready, 5000);
        QCOMPARE(evaluation->iouThreshold(), 0.7);

        QVERIFY(evaluation_params->setValueForName(QStringLiteral("matching_strategy"),
                                                   QStringLiteral("hungarian_iou")));
        QTRY_VERIFY_WITH_TIMEOUT(loading_changed.count() >= 6, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(evaluation->stateKind(), ModelEvaluationViewModel::Ready, 5000);
        QCOMPARE(evaluation->matchingStrategy(), QStringLiteral("hungarian_iou"));
        task_manager->clearTasks();
    }

    void changedPredictionSnapshotInvalidatesRetainedEvaluation()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 cat   = fixture.addClass(QStringLiteral("Cat"), QStringLiteral("normal"));
        const qint64 image = fixture.addImage(QStringLiteral("cat"));
        QVERIFY(fixture.addDetectionLabel(image, cat, 0, 0, 10, 10) >= 0);
        QVERIFY(fixture.writeImageList());

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager model_manager(static_cast<int>(evaluation::Method::Detection), &database, nullptr);
        QString      error;
        const auto record = model_manager.addModelRecord(QStringLiteral("Managed"), QStringLiteral("ultralytics"),
                                                         QStringLiteral("YOLOv8"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        TaskManager task_manager_instance;
        TaskManager *task_manager = &task_manager_instance;
        ModelTestTaskManager manager(fixture.rootPath(), &model_manager, nullptr, task_manager);
        manager.setModelUuid(record.uuid);
        QVERIFY2(prepareEvaluationInputs(fixture, manager, record, image,
                                         detectionPrediction(static_cast<int>(cat), QStringLiteral("Cat"), 0.9, 0, 0,
                                                             10, 10),
                                         true, &error),
                 qPrintable(error));

        auto *evaluation = manager.currentEvaluation();
        QVERIFY(evaluation != nullptr);
        evaluation->evaluate(false);
        QTRY_COMPARE_WITH_TIMEOUT(evaluation->stateKind(), ModelEvaluationViewModel::Ready, 5000);

        const ModelStorageService storage(fixture.rootPath());
        const QString prediction_dir
            = storage.testTaskPredictionPath(record.name, manager.currentTaskDirectory());
        QFile marker(QDir(prediction_dir).filePath(QStringLiteral("snapshot-marker")));
        QVERIFY2(marker.open(QIODevice::WriteOnly), qPrintable(marker.errorString()));
        QVERIFY(marker.write("changed") == 7);
        marker.close();

        QVERIFY(manager.switchTask(manager.currentTaskUuid()));
        QCOMPARE(manager.currentEvaluation(), evaluation);
        QCOMPARE(evaluation->stateKind(), ModelEvaluationViewModel::NotRun);
        QVERIFY(!evaluation->available());
        evaluation->evaluate(false);
        QTRY_COMPARE_WITH_TIMEOUT(evaluation->stateKind(), ModelEvaluationViewModel::Ready, 5000);
        task_manager->clearTasks();
    }

    void anomalyClassificationThresholdIsEvaluationOnly()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::AnomalyDetection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 good    = fixture.addClass(QStringLiteral("Good"), QStringLiteral("good"));
        const qint64 anomaly = fixture.addClass(QStringLiteral("Scratch"), QStringLiteral("anomaly"));
        const qint64 normal  = fixture.addImage(QStringLiteral("normal"),
                                                 {{QStringLiteral("image_label_class_id"), good}});
        const qint64 bad     = fixture.addImage(QStringLiteral("bad"),
                                                {{QStringLiteral("image_label_class_id"), anomaly}});
        QVERIFY(good >= 0);
        QVERIFY(anomaly >= 0);
        QVERIFY(normal >= 0);
        QVERIFY(bad >= 0);
        QVERIFY(fixture.writeImageList());

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager model_manager(static_cast<int>(evaluation::Method::AnomalyDetection), &database, nullptr);
        QString      error;
        const auto record = model_manager.addModelRecord(QStringLiteral("Managed"), QStringLiteral("anomalib"),
                                                         QStringLiteral("patchcore"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        TaskManager task_manager_instance;
        TaskManager *task_manager = &task_manager_instance;
        ModelTestTaskManager manager(fixture.rootPath(), &model_manager, nullptr, task_manager);
        manager.setModelUuid(record.uuid);
        const bool prepared_normal
            = prepareEvaluationInputs(fixture, manager, record, normal, anomalyPrediction(0.2), true, &error);
        QVERIFY2(prepared_normal, qPrintable(error));
        QVERIFY2(prepareEvaluationInputs(fixture, manager, record, bad, anomalyPrediction(0.9), true, &error),
                 qPrintable(error));

        auto *evaluation = manager.currentEvaluation();
        auto *inference  = findGroup(manager.currentTestParams(), QStringLiteral("inference"));
        auto *evaluation_params = findGroup(manager.currentTestParams(), QStringLiteral("evaluation"));
        QVERIFY(evaluation != nullptr);
        QVERIFY(inference != nullptr);
        QVERIFY(evaluation_params != nullptr);
        QVERIFY(evaluation_params->fieldMapForName(QStringLiteral("classification_threshold")).size() > 0);
        QVERIFY(evaluation_params->fieldMapForName(QStringLiteral("heatmap_threshold")).size() > 0);

        evaluation->evaluate(false);
        QTRY_COMPARE_WITH_TIMEOUT(evaluation->stateKind(), ModelEvaluationViewModel::Ready, 5000);
        QVERIFY(evaluation->available());
        QVERIFY(std::abs(evaluation->confidenceThreshold() - 0.9) < 1e-6);

        QSignalSpy loading_changed(evaluation, &ModelEvaluationViewModel::loadingChanged);
        const int original_batch = inference->valueForName(QStringLiteral("batch_size")).toInt();
        QVERIFY(inference->setValueForName(QStringLiteral("batch_size"), original_batch + 1));
        QCOMPARE(evaluation->stateKind(), ModelEvaluationViewModel::Ready);
        QCOMPARE(loading_changed.count(), 0);

        const double original_heatmap_threshold
            = evaluation_params->valueForName(QStringLiteral("heatmap_threshold")).toDouble();
        const double next_heatmap_threshold
            = qFuzzyCompare(original_heatmap_threshold, 1.0) ? 0.75 : 1.0;
        QVERIFY(evaluation_params->setValueForName(QStringLiteral("heatmap_threshold"), next_heatmap_threshold));
        QCOMPARE(evaluation->stateKind(), ModelEvaluationViewModel::Ready);
        QVERIFY(evaluation->available());
        QCOMPARE(loading_changed.count(), 0);

        QVERIFY(evaluation_params->setValueForName(QStringLiteral("classification_threshold"), 0.8));
        QTRY_VERIFY_WITH_TIMEOUT(loading_changed.count() >= 2, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(evaluation->stateKind(), ModelEvaluationViewModel::Ready, 5000);
        QCOMPARE(evaluation->confidenceThreshold(), 0.8);
        task_manager->clearTasks();
    }

    void anomalyViewModelRecognizesTiffPredictionsWithoutTaskRows()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::AnomalyDetection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 good = fixture.addClass(QStringLiteral("Good"), QStringLiteral("good"));
        const qint64 anomaly = fixture.addClass(QStringLiteral("Scratch"), QStringLiteral("anomaly"));
        const qint64 image = fixture.addImage(QStringLiteral("bad"),
                                              {{QStringLiteral("image_label_class_id"), anomaly}});
        QVERIFY(good >= 0);
        QVERIFY(anomaly >= 0);
        QVERIFY(image >= 0);
        QVERIFY(fixture.writeImageList());
        QVERIFY(fixture.setTestSelection({good, anomaly}));
        QVERIFY(fixture.writePrediction(image, anomalyPrediction(0.9)));
        QVERIFY(fixture.removePrediction(image));

        ModelEvaluationOptions options;
        options.method                 = evaluation::Method::AnomalyDetection;
        options.dataset_file_list_path = fixture.fileListPath();
        options.prediction_dir         = fixture.predictionDirectory();
        options.task_database_path
            = QDir(fixture.rootPath()).filePath(QStringLiteral("missing-task.db"));
        ModelEvaluationViewModel *evaluation
            = EvaluationViewModelRegistry::instance().createViewModel(evaluation::Method::AnomalyDetection);
        QVERIFY(evaluation != nullptr);
        evaluation->setEvaluationOptions(options);
        QVERIFY(evaluation->hasPredictionResults());
        delete evaluation;
    }

    void evaluationParameterWithoutPredictionsDoesNotStartEvaluation()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 cat   = fixture.addClass(QStringLiteral("Cat"), QStringLiteral("normal"));
        const qint64 image = fixture.addImage(QStringLiteral("cat"));
        QVERIFY(cat >= 0);
        QVERIFY(image >= 0);
        QVERIFY(fixture.addDetectionLabel(image, cat, 0, 0, 10, 10) >= 0);
        QVERIFY(fixture.writeImageList());

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager model_manager(static_cast<int>(evaluation::Method::Detection), &database, nullptr);
        QString      error;
        const auto record = model_manager.addModelRecord(QStringLiteral("Managed"), QStringLiteral("ultralytics"),
                                                         QStringLiteral("YOLOv8"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        TaskManager task_manager_instance;
        TaskManager *task_manager = &task_manager_instance;
        ModelTestTaskManager manager(fixture.rootPath(), &model_manager, nullptr, task_manager);
        manager.setModelUuid(record.uuid);
        QVERIFY2(prepareEvaluationInputs(fixture, manager, record, image, {}, false, &error), qPrintable(error));

        auto *evaluation = manager.currentEvaluation();
        auto *evaluation_params = findGroup(manager.currentTestParams(), QStringLiteral("evaluation"));
        QVERIFY(evaluation != nullptr);
        QVERIFY(evaluation_params != nullptr);
        QSignalSpy loading_changed(evaluation, &ModelEvaluationViewModel::loadingChanged);

        QVERIFY(evaluation_params->setValueForName(QStringLiteral("conf"), 0.6));
        QTRY_COMPARE_WITH_TIMEOUT(evaluation->stateKind(), ModelEvaluationViewModel::NotRun, 1000);
        QVERIFY(!evaluation->loading());
        QVERIFY(!evaluation->available());
        QCOMPARE(loading_changed.count(), 0);
        task_manager->clearTasks();
    }

    void finishedTestAutomaticallyEvaluatesAndBusyStateRestores()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 cat   = fixture.addClass(QStringLiteral("Cat"), QStringLiteral("normal"));
        const qint64 image = fixture.addImage(QStringLiteral("cat"));
        QVERIFY(cat >= 0);
        QVERIFY(image >= 0);
        QVERIFY(fixture.addDetectionLabel(image, cat, 0, 0, 10, 10) >= 0);
        QVERIFY(fixture.writeImageList());

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager model_manager(static_cast<int>(evaluation::Method::Detection), &database, nullptr);
        QString      error;
        const auto record = model_manager.addModelRecord(QStringLiteral("Managed"), QStringLiteral("ultralytics"),
                                                         QStringLiteral("YOLOv8"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        TaskManager task_manager_instance;
        TaskManager *task_manager = &task_manager_instance;
        ModelTestTaskManager manager(fixture.rootPath(), &model_manager, nullptr, task_manager);
        manager.setModelUuid(record.uuid);
        QVERIFY2(prepareEvaluationInputs(fixture, manager, record, image,
                                         detectionPrediction(static_cast<int>(cat), QStringLiteral("Cat"), 0.9, 0, 0,
                                                             10, 10),
                                         true, &error),
                 qPrintable(error));

        auto *evaluation = manager.currentEvaluation();
        QVERIFY(evaluation != nullptr);
        QSignalSpy loading_changed(evaluation, &ModelEvaluationViewModel::loadingChanged);
        const int task_id = task_manager->addTask(record.uuid, record.name, ModelTaskType::Test,
                                                  manager.currentTaskUuid(), manager.currentTaskName());
        QVERIFY(task_id > 0);
        QVERIFY(!manager.currentModelBusy());
        QVERIFY(task_manager->markTaskRunning(task_id));
        QVERIFY(manager.currentModelBusy());
        QVERIFY(task_manager->finishTask(task_id));
        QTRY_VERIFY_WITH_TIMEOUT(loading_changed.count() >= 2, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(evaluation->stateKind(), ModelEvaluationViewModel::Ready, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.currentModelBusy(), 5000);
        const int completed_loading_changes = loading_changed.count();

        const int failed_id = task_manager->addTask(record.uuid, record.name, ModelTaskType::Train);
        QVERIFY(failed_id > 0);
        QVERIFY(task_manager->markTaskRunning(failed_id));
        QVERIFY(manager.currentModelBusy());
        QVERIFY(task_manager->failTask(failed_id));
        QVERIFY(!manager.currentModelBusy());
        QCOMPARE(loading_changed.count(), completed_loading_changes);

        const int stopped_id = task_manager->addTask(record.uuid, record.name, ModelTaskType::Train);
        QVERIFY(stopped_id > 0);
        QVERIFY(task_manager->startTask(stopped_id));
        QVERIFY(manager.currentModelBusy());
        QVERIFY(task_manager->stopTask(stopped_id));
        QVERIFY(manager.currentModelBusy());
        QVERIFY(task_manager->markTaskStopped(stopped_id));
        QVERIFY(!manager.currentModelBusy());
        QCOMPARE(loading_changed.count(), completed_loading_changes);
        task_manager->clearTasks();
    }

    void modifyingTrainParamsDoesNotAlterPreprocessingContextOfPublishedPredictions()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 cat   = fixture.addClass(QStringLiteral("Cat"), QStringLiteral("normal"));
        const qint64 image = fixture.addImage(QStringLiteral("cat"));
        QVERIFY(cat >= 0 && image >= 0);
        QVERIFY(fixture.addDetectionLabel(image, cat, 0, 0, 10, 10) >= 0);
        QVERIFY(fixture.writeImageList());

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager model_manager(static_cast<int>(evaluation::Method::Detection), &database, nullptr);
        QString      error;
        const auto record = model_manager.addModelRecord(QStringLiteral("PreprocessingModel"), QStringLiteral("ultralytics"),
                                                         QStringLiteral("YOLOv8"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        IModel *model = model_manager.modelForUuid(record.uuid);
        QVERIFY(model != nullptr && model->config() != nullptr && model->config()->trainParams() != nullptr);

        auto *network_group = findGroup(model->config()->trainParams(), QStringLiteral("network"));
        QVERIFY(network_group != nullptr);
        network_group->setValueForName(QStringLiteral("imgsz"), 320);

        ModelTestTaskManager manager(fixture.rootPath(), &model_manager, nullptr, nullptr);
        manager.setModelUuid(record.uuid);

        QVERIFY2(prepareEvaluationInputs(fixture, manager, record, image,
                                         detectionPrediction(static_cast<int>(cat), QStringLiteral("Cat"), 0.9, 0, 0, 10, 10),
                                         true, &error),
                 qPrintable(error));

        const ModelStorageService storage(fixture.rootPath());
        const QString task_db_path = storage.testTaskDatabasePath(record.name, manager.currentTaskDirectory());
        dltool::database::ModelTaskDataBase task_db(task_db_path);

        QVariantMap saved_prep;
        saved_prep.insert(QStringLiteral("imgsz"), 320);
        QVERIFY(task_db.writePreprocessingConfig(saved_prep));

        ModelEvaluationOptions options;
        QVERIFY(manager.buildEvaluationOptions(options, &error));
        QCOMPARE(options.preprocessing_config.value(QStringLiteral("imgsz")).toInt(), 320);

        // 修改当前模型的 trainParams，不应改变已发布预测的 preprocessing_config
        network_group->setValueForName(QStringLiteral("imgsz"), 640);
        QCOMPARE(model->config()->trainParams()->valuesMap().value(QStringLiteral("network")).toMap().value(QStringLiteral("imgsz")).toInt(), 640);

        ModelEvaluationOptions options_after_edit;
        QVERIFY(manager.buildEvaluationOptions(options_after_edit, &error));
        QCOMPARE(options_after_edit.preprocessing_config.value(QStringLiteral("imgsz")).toInt(), 320);

        // 重新推理：新预处理上下文写入 task.db
        saved_prep.insert(QStringLiteral("imgsz"), 640);
        QVERIFY(task_db.writePreprocessingConfig(saved_prep));

        ModelEvaluationOptions options_reinferenced;
        QVERIFY(manager.buildEvaluationOptions(options_reinferenced, &error));
        QCOMPARE(options_reinferenced.preprocessing_config.value(QStringLiteral("imgsz")).toInt(), 640);
        QVERIFY(options_after_edit.preprocessing_config != options_reinferenced.preprocessing_config);
    }

    void rawPredictionsAreNotMutatedDuringEvaluationOrThresholdAdjustment()
    {
        // 1. Anomaly detection: TIFF 原始文件完全不被修改
        {
            EvaluationFixture fixture(static_cast<int>(evaluation::Method::AnomalyDetection));
            QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
            const qint64 normal_class = fixture.addClass(QStringLiteral("Normal"), QStringLiteral("normal"));
            const qint64 anomaly_class = fixture.addClass(QStringLiteral("Defect"), QStringLiteral("anomaly"));
            const qint64 image = fixture.addImage(QStringLiteral("defect_sample"));
            QVERIFY(fixture.addAnomalyLabel(image, anomaly_class, {{1, 1}, {8, 1}, {8, 8}, {1, 8}}) >= 0);
            QVERIFY(fixture.writeImageList());

            dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
            ModelManager model_manager(static_cast<int>(evaluation::Method::AnomalyDetection), &database, nullptr);
            QString      error;
            const auto record = model_manager.addModelRecord(QStringLiteral("AnomalyRawModel"), QStringLiteral("anomalib"),
                                                             QStringLiteral("patchcore"), &error);
            QVERIFY2(record.isValid(), qPrintable(error));

            ModelTestTaskManager manager(fixture.rootPath(), &model_manager, nullptr, nullptr);
            manager.setModelUuid(record.uuid);

            QVERIFY2(prepareEvaluationInputs(fixture, manager, record, image,
                                             anomalyPrediction(0.85),
                                             true, &error),
                     qPrintable(error));

            const ModelStorageService storage(fixture.rootPath());
            const QString prediction_dir = storage.testTaskPredictionPath(record.name, manager.currentTaskDirectory());
            const QString tiff_path = QDir(prediction_dir).filePath(QStringLiteral("%1.tiff").arg(image));
            QVERIFY(QFile::exists(tiff_path));

            QFile tiff_file(tiff_path);
            QVERIFY(tiff_file.open(QIODevice::ReadOnly));
            const QByteArray original_bytes = tiff_file.readAll();
            tiff_file.close();
            QVERIFY(!original_bytes.isEmpty());

            auto *evaluation = manager.currentEvaluation();
            QVERIFY(evaluation != nullptr);
            evaluation->evaluate();
            QTRY_COMPARE_WITH_TIMEOUT(evaluation->stateKind(), ModelEvaluationViewModel::Ready, 5000);

            evaluation->adoptEvaluationThreshold(0.9);
            const QString thumb = evaluation->heatmapThumbnailUrl(image, QStringLiteral("defect.png"), tiff_path, 0.9);
            QVERIFY(!thumb.isEmpty());

            QFile tiff_file_after(tiff_path);
            QVERIFY(tiff_file_after.open(QIODevice::ReadOnly));
            const QByteArray after_bytes = tiff_file_after.readAll();
            tiff_file_after.close();
            QCOMPARE(after_bytes, original_bytes);
        }

        // 2. Detection: task.db prediction 表记录完全不被修改
        {
            EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
            QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
            const qint64 cat = fixture.addClass(QStringLiteral("Cat"), QStringLiteral("normal"));
            const qint64 image = fixture.addImage(QStringLiteral("cat_sample"));
            QVERIFY(fixture.addDetectionLabel(image, cat, 0, 0, 10, 10) >= 0);
            QVERIFY(fixture.writeImageList());

            dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
            ModelManager model_manager(static_cast<int>(evaluation::Method::Detection), &database, nullptr);
            QString      error;
            const auto record = model_manager.addModelRecord(QStringLiteral("DetectionRawModel"), QStringLiteral("ultralytics"),
                                                             QStringLiteral("YOLOv8"), &error);
            QVERIFY2(record.isValid(), qPrintable(error));

            ModelTestTaskManager manager(fixture.rootPath(), &model_manager, nullptr, nullptr);
            manager.setModelUuid(record.uuid);

            const QVariant raw_pred = detectionPrediction(static_cast<int>(cat), QStringLiteral("Cat"), 0.9, 0, 0, 10, 10);
            QVERIFY2(prepareEvaluationInputs(fixture, manager, record, image, raw_pred, true, &error),
                     qPrintable(error));

            const ModelStorageService storage(fixture.rootPath());
            const QString task_db_path = storage.testTaskDatabasePath(record.name, manager.currentTaskDirectory());
            dltool::database::ModelTaskDataBase task_db(task_db_path);

            QHash<qint64, QVariant> original_preds;
            QVERIFY(task_db.readPredictions(original_preds));
            QVERIFY(original_preds.contains(image));

            auto *evaluation = manager.currentEvaluation();
            QVERIFY(evaluation != nullptr);
            evaluation->evaluate();
            QTRY_COMPARE_WITH_TIMEOUT(evaluation->stateKind(), ModelEvaluationViewModel::Ready, 5000);

            auto *evaluation_params = findGroup(manager.currentTestParams(), QStringLiteral("evaluation"));
            QVERIFY(evaluation_params != nullptr);
            QVERIFY(evaluation_params->setValueForName(QStringLiteral("conf"), 0.7));

            QHash<qint64, QVariant> after_preds;
            QVERIFY(task_db.readPredictions(after_preds));
            QCOMPARE(after_preds.size(), original_preds.size());
            QCOMPARE(after_preds.value(image), original_preds.value(image));
        }
    }

    void unrelatedModelWritesDoNotInvalidateEvaluationSnapshot()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 cat   = fixture.addClass(QStringLiteral("Cat"), QStringLiteral("normal"));
        const qint64 image = fixture.addImage(QStringLiteral("cat_test"));
        QVERIFY(fixture.addDetectionLabel(image, cat, 0, 0, 10, 10) >= 0);
        QVERIFY(fixture.writeImageList());

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager model_manager(static_cast<int>(evaluation::Method::Detection), &database, nullptr);
        QString      error;
        const auto record = model_manager.addModelRecord(QStringLiteral("SnapshotModel"), QStringLiteral("ultralytics"),
                                                         QStringLiteral("YOLOv8"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        TaskManager task_manager_instance;
        ModelTestTaskManager manager(fixture.rootPath(), &model_manager, nullptr, &task_manager_instance);
        manager.setModelUuid(record.uuid);
        QVERIFY2(prepareEvaluationInputs(fixture, manager, record, image,
                                         detectionPrediction(static_cast<int>(cat), QStringLiteral("Cat"), 0.9, 0, 0,
                                                             10, 10),
                                         true, &error),
                 qPrintable(error));

        auto *evaluation = manager.currentEvaluation();
        QVERIFY(evaluation != nullptr);
        evaluation->evaluate(false);
        QTRY_COMPARE_WITH_TIMEOUT(evaluation->stateKind(), ModelEvaluationViewModel::Ready, 5000);

        ModelEvaluationOptions base_options;
        QVERIFY(manager.buildEvaluationOptions(base_options));
        const QString initial_snapshot = base_options.prediction_snapshot;
        QVERIFY(!initial_snapshot.isEmpty());

        // 验收条件 2: 无关模型写入（如更新训练损失/耗时、新增其他模型）不改变真值指纹与评估快照
        QVariantMap train_update;
        train_update.insert(QStringLiteral("status"), QStringLiteral("running"));
        train_update.insert(QStringLiteral("loss"), QStringLiteral("0.35"));
        train_update.insert(QStringLiteral("elapsed_seconds"), 120);
        QVERIFY(model_manager.updateModelExtraData(record.uuid, {{QStringLiteral("train"), train_update}}, &error));

        const auto other_model = model_manager.addModelRecord(QStringLiteral("OtherModel"), QStringLiteral("ultralytics"),
                                                              QStringLiteral("YOLOv8"), &error);
        QVERIFY(other_model.isValid());

        ModelEvaluationOptions options_after_unrelated;
        QVERIFY(manager.buildEvaluationOptions(options_after_unrelated));
        QCOMPARE(options_after_unrelated.prediction_snapshot, initial_snapshot);

        // 切换任务/重新选中任务，当前就绪评估保持 Ready，不被重新读取或清空
        QVERIFY(manager.switchTask(manager.currentTaskUuid()));
        QCOMPARE(manager.currentEvaluation(), evaluation);
        QCOMPARE(evaluation->stateKind(), ModelEvaluationViewModel::Ready);

        // 验收条件 2: 相关输入变化（如真值标签增加/变更）正确改变指纹并使当前结果失效
        const qint64 dog = fixture.addClass(QStringLiteral("Dog"), QStringLiteral("normal"));
        QVERIFY(fixture.addDetectionLabel(image, dog, 5, 5, 15, 15) >= 0);

        ModelEvaluationOptions options_after_gt_change;
        QVERIFY(manager.buildEvaluationOptions(options_after_gt_change));
        QVERIFY(options_after_gt_change.prediction_snapshot != initial_snapshot);

        // 切换或重新绑定任务，评估状态应正确失效重置
        QVERIFY(manager.switchTask(manager.currentTaskUuid()));
        QCOMPARE(manager.currentEvaluation(), evaluation);
        QCOMPARE(evaluation->stateKind(), ModelEvaluationViewModel::NotRun);
    }

    void coldAndHotEvaluationOpenRecordsMetricsAndGuiResponseWithConsistentValues()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 cat = fixture.addClass(QStringLiteral("Cat"), QStringLiteral("normal"));

        // 构造多图测试样本集 (5 张图像，具备标注与预测)
        QList<qint64> sample_ids;
        for (int i = 0; i < 5; ++i)
        {
            const qint64 img_id = fixture.addImage(QStringLiteral("cat_sample_%1").arg(i));
            QVERIFY(fixture.addDetectionLabel(img_id, cat, 5.0 * i, 5.0 * i, 15.0 + i, 15.0 + i) >= 0);
            sample_ids.append(img_id);
        }
        QVERIFY(fixture.writeImageList());

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager model_manager(static_cast<int>(evaluation::Method::Detection), &database, nullptr);
        QString      error;
        const auto record = model_manager.addModelRecord(QStringLiteral("PerfModel"), QStringLiteral("ultralytics"),
                                                         QStringLiteral("YOLOv8"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        ModelTestTaskManager manager(fixture.rootPath(), &model_manager, nullptr, nullptr);
        manager.setModelUuid(record.uuid);

        // 初始化任务 1 并写入全部 5 张图的预测记录
        const QString task1_uuid = manager.currentTaskUuid();
        const QString task1_dir  = manager.currentTaskDirectory();
        QVERIFY2(prepareEvaluationInputs(fixture, manager, record, sample_ids.first(),
                                         detectionPrediction(static_cast<int>(cat), QStringLiteral("Cat"), 0.9, 0, 0, 15, 15),
                                         true, &error),
                 qPrintable(error));

        const ModelStorageService storage(fixture.rootPath());
        dltool::database::ModelTaskDataBase task1_db(storage.testTaskDatabasePath(record.name, task1_dir));
        for (int i = 1; i < sample_ids.size(); ++i)
        {
            QVERIFY(task1_db.upsertPrediction({sample_ids[i],
                                              detectionPrediction(static_cast<int>(cat), QStringLiteral("Cat"), 0.85 + 0.02 * i,
                                                                  5.0 * i, 5.0 * i, 15.0 + i, 15.0 + i)}));
        }

        ModelEvaluationOptions options;
        QVERIFY(manager.buildEvaluationOptions(options));

        auto *evaluation = manager.currentEvaluation();
        QVERIFY(evaluation != nullptr);

        // 1. 冷打开：执行后台全量评估，实测记录读取成本（物理磁盘/DB 读取次数 > 0）与执行耗时
        evaluation::EvaluationDiskIoTracker::reset();
        const qint64 total_reads_before_cold = evaluation::EvaluationDiskIoTracker::totalDiskReadCount();
        QCOMPARE(evaluation->evaluationCount(), 0);
        evaluation->evaluate(false);
        QTRY_COMPARE_WITH_TIMEOUT(evaluation->stateKind(), ModelEvaluationViewModel::Ready, 5000);
        QCOMPARE(evaluation->evaluationCount(), 1);
        const qint64 cold_elapsed        = evaluation->lastEvaluationElapsedMs();
        const int    cold_reads          = evaluation->lastDiskReadCount();
        const qint64 physical_cold_reads = evaluation::EvaluationDiskIoTracker::totalDiskReadCount() - total_reads_before_cold;
        QVERIFY(cold_elapsed >= 0);
        QVERIFY2(physical_cold_reads > 0, qPrintable(QString("冷打开应产生真实的物理磁盘读取，实际: %1").arg(physical_cold_reads)));
        QCOMPARE(cold_reads, static_cast<int>(physical_cold_reads));

        const double cold_conf = evaluation->confidenceThreshold();
        const double cold_best = evaluation->hasBestThreshold() ? evaluation->bestThreshold() : 0.0;
        const auto *matrix = evaluation->confusionMatrix();
        QVERIFY(matrix != nullptr);
        const int cold_cols = matrix->columnCount();
        const auto cold_metrics = evaluation->instanceMetrics()->records();
        QVERIFY(!cold_metrics.empty());
        const auto cold_cells = matrix->records();
        QVERIFY(!cold_cells.empty());

        // 2. 切换到新任务 Task 2，建立独立 VM
        const QString task2_uuid = manager.createTask(QStringLiteral("Task 2"));
        QVERIFY(!task2_uuid.isEmpty());
        QVERIFY(manager.switchTask(task2_uuid));
        prepareEvaluationInputs(fixture, manager, record, sample_ids.first(),
                                detectionPrediction(static_cast<int>(cat), QStringLiteral("Cat"), 0.7, 0, 0, 10, 10),
                                true, &error);
        auto *task2_eval = manager.currentEvaluation();
        QVERIFY(task2_eval != nullptr);
        QVERIFY(task2_eval != evaluation);

        // 3. 热打开：跨任务切换切回任务 1，GUI 立即响应（无需重新触发后台执行），数值完全一致
        const qint64 total_reads_before_hot = evaluation::EvaluationDiskIoTracker::totalDiskReadCount();
        QElapsedTimer hot_timer;
        hot_timer.start();
        QVERIFY(manager.switchTask(task1_uuid));
        const qint64 hot_gui_elapsed = hot_timer.elapsed();
        const qint64 physical_hot_reads = evaluation::EvaluationDiskIoTracker::totalDiskReadCount() - total_reads_before_hot;
        QVERIFY2(hot_gui_elapsed < 50, qPrintable(QString("热打开 GUI 响应耗时过长: %1 ms").arg(hot_gui_elapsed)));
        // 真实实测断言：热打开期间全系统评估入口累计物理磁盘读取数必须严格为 0！
        QCOMPARE(physical_hot_reads, 0);

        auto *hot_evaluation = manager.currentEvaluation();
        QCOMPARE(hot_evaluation, evaluation);
        QCOMPARE(hot_evaluation->stateKind(), ModelEvaluationViewModel::Ready);
        // 读取成本与执行验证：无额外后台计算（计数不变）、无新评估排队、内存 O(1) 复用
        QCOMPARE(hot_evaluation->evaluationCount(), 1);
        QCOMPARE(hot_evaluation->lastDiskReadCount(), cold_reads);

        QCOMPARE(hot_evaluation->confidenceThreshold(), cold_conf);
        QCOMPARE(hot_evaluation->hasBestThreshold() ? hot_evaluation->bestThreshold() : 0.0, cold_best);
        QCOMPARE(matrix->columnCount(), cold_cols);

        // 指标数值逐字段严格一致验证
        const auto hot_metrics = hot_evaluation->instanceMetrics()->records();
        QCOMPARE(hot_metrics.size(), cold_metrics.size());
        for (size_t i = 0; i < cold_metrics.size(); ++i)
        {
            QCOMPARE(hot_metrics[i].key, cold_metrics[i].key);
            QCOMPARE(hot_metrics[i].precision, cold_metrics[i].precision);
            QCOMPARE(hot_metrics[i].recall, cold_metrics[i].recall);
            QCOMPARE(hot_metrics[i].f1, cold_metrics[i].f1);
            QCOMPARE(hot_metrics[i].ap, cold_metrics[i].ap);
            QCOMPARE(hot_metrics[i].tp, cold_metrics[i].tp);
            QCOMPARE(hot_metrics[i].fp, cold_metrics[i].fp);
            QCOMPARE(hot_metrics[i].fn, cold_metrics[i].fn);
        }

        const auto hot_cells = matrix->records();
        QCOMPARE(hot_cells.size(), cold_cells.size());
        for (size_t j = 0; j < cold_cells.size(); ++j)
        {
            QCOMPARE(hot_cells[j].row_key, cold_cells[j].row_key);
            QCOMPARE(hot_cells[j].column_key, cold_cells[j].column_key);
            QCOMPARE(hot_cells[j].count, cold_cells[j].count);
            QCOMPARE(hot_cells[j].cell_kind, cold_cells[j].cell_kind);
        }

        qInfo() << "[Evidence Ticket 21] Cold evaluation time:" << cold_elapsed << "ms, disk reads:" << cold_reads << ", execution count: 1";
        qInfo() << "[Evidence Ticket 21] Hot evaluation GUI response:" << hot_gui_elapsed << "ms, re-evaluations: 0, disk re-reads:" << physical_hot_reads;
        qInfo() << "[Evidence Ticket 21] Metrics bit-for-bit identical:"
                << "metrics_count=" << cold_metrics.size()
                << "precision=" << cold_metrics.front().precision
                << "recall=" << cold_metrics.front().recall
                << "f1=" << cold_metrics.front().f1
                << "ap=" << cold_metrics.front().ap
                << "tp=" << cold_metrics.front().tp
                << "fp=" << cold_metrics.front().fp
                << "fn=" << cold_metrics.front().fn;

        // 4. 显式重新评估：计数递增，结果数值依然一致
        hot_evaluation->refreshEvaluation();
        QTRY_COMPARE_WITH_TIMEOUT(hot_evaluation->stateKind(), ModelEvaluationViewModel::Ready, 5000);
        QCOMPARE(hot_evaluation->evaluationCount(), 2);
        QCOMPARE(hot_evaluation->confidenceThreshold(), cold_conf);
        QCOMPARE(hot_evaluation->hasBestThreshold() ? hot_evaluation->bestThreshold() : 0.0, cold_best);
        QCOMPARE(matrix->columnCount(), cold_cols);
    }

    void concurrentEvaluationsHaveIsolatedIoCounters()
    {
        // 验证并发评估场景下 thread_local EvaluationIoScope 的读盘入口计数互相隔离、互不污染
        evaluation::EvaluationDiskIoTracker::reset();

        std::atomic<bool> t1_ready{false};
        std::atomic<bool> t2_ready{false};
        std::atomic<bool> start_signal{false};

        auto fut1 = std::async(std::launch::async, [&t1_ready, &start_signal]() {
            evaluation::EvaluationIoScope scope1;
            t1_ready.store(true);
            while (!start_signal.load())
                QThread::yieldCurrentThread();

            // Thread 1 模拟执行 5 次读取入口调用
            for (int i = 0; i < 5; ++i)
                evaluation::EvaluationDiskIoTracker::recordDiskRead(QStringLiteral("t1_file_%1").arg(i));

            return scope1.readCount();
        });

        auto fut2 = std::async(std::launch::async, [&t2_ready, &start_signal]() {
            evaluation::EvaluationIoScope scope2;
            t2_ready.store(true);
            while (!start_signal.load())
                QThread::yieldCurrentThread();

            // Thread 2 模拟并发执行 3 次读取入口调用
            for (int i = 0; i < 3; ++i)
                evaluation::EvaluationDiskIoTracker::recordDiskRead(QStringLiteral("t2_file_%1").arg(i));

            return scope2.readCount();
        });

        while (!t1_ready.load() || !t2_ready.load())
            QThread::msleep(1);
        start_signal.store(true);

        const qint64 c1 = fut1.get();
        const qint64 c2 = fut2.get();

        // 验证两个并发线程的作用域计数严格隔离，各自统计本线程真实调用，互不计入对方调用
        QCOMPARE(c1, 5);
        QCOMPARE(c2, 3);
        // 全局计数器准确统计两线程总和
        QCOMPARE(evaluation::EvaluationDiskIoTracker::totalDiskReadCount(), 8);
    }

    void evaluationSnapshotAvoidsGuiDirectoryScanningAndFullTableSerialization()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 cat = fixture.addClass(QStringLiteral("Cat"), QStringLiteral("normal"));
        const qint64 image = fixture.addImage(QStringLiteral("cat_bench"));
        QVERIFY(fixture.addDetectionLabel(image, cat, 0, 0, 10, 10) >= 0);
        QVERIFY(fixture.writeImageList());

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager model_manager(static_cast<int>(evaluation::Method::Detection), &database, nullptr);
        QString      error;
        const auto record = model_manager.addModelRecord(QStringLiteral("BenchModel"), QStringLiteral("ultralytics"),
                                                         QStringLiteral("YOLOv8"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        ModelTestTaskManager manager(fixture.rootPath(), &model_manager, nullptr, nullptr);
        manager.setModelUuid(record.uuid);
        QVERIFY2(prepareEvaluationInputs(fixture, manager, record, image,
                                         detectionPrediction(static_cast<int>(cat), QStringLiteral("Cat"), 0.9, 0, 0, 10, 10),
                                         true, &error),
                 qPrintable(error));

        const ModelStorageService storage(fixture.rootPath());
        const QString file_list = storage.testTaskFileListPath(record.name, manager.currentTaskDirectory());
        const QString task_db = storage.testTaskDatabasePath(record.name, manager.currentTaskDirectory());
        const QString pred_dir = storage.testTaskPredictionPath(record.name, manager.currentTaskDirectory());

        // 验收条件 1: 在 GUI 线程获取快照身份时不进行前台全表序列化或全目录扫描，耗时极低（≤ 50ms）
        QElapsedTimer timer;
        timer.start();
        const QString snapshot = ModelTestTaskManager::evaluationInputSnapshot(fixture.projectDatabasePath(), file_list, task_db, pred_dir);
        const qint64 elapsed = timer.elapsed();

        QVERIFY(!snapshot.isEmpty());
        QVERIFY2(elapsed < 50, qPrintable(QString("快照计算耗时过长: %1 ms").arg(elapsed)));
    }

    void nonSquareCenterCropPaddingAndResizeFixedCoordinatesAssertion()
    {
        // 1. 纯几何变换层面的固定数值断言：非方形原图、Resize、Padding、CenterCrop
        // source_size: 200 x 100 (非方形 2:1)
        // model_size: 60 x 30
        // resize: 160 x 80
        // padding: left=20, top=10, right=20, bottom=10 -> padded_size = 200 x 100
        // center_crop_size: 120 x 60 -> crop_rect = (40, 20, 120, 60)
        const QVariantMap preprocessing = {
            {QStringLiteral("network"),
             QVariantMap{
                 {QStringLiteral("image_size"), QVariantList{80, 160}},
                 {QStringLiteral("padding"), QVariantList{20, 10, 20, 10}},
                 {QStringLiteral("center_crop_size"), QVariantList{60, 120}}
             }}
        };

        const AnomalyPreprocessingTransform transform
            = AnomalyPreprocessingTransform::fromConfig(QSize(200, 100), QSize(60, 30), preprocessing);
        QVERIFY(transform.isValid());
        QCOMPARE(transform.sourceSize(), QSize(200, 100));
        QCOMPARE(transform.resizedSize(), QSize(160, 80));
        QCOMPARE(transform.padding(), QMargins(20, 10, 20, 10));
        QCOMPARE(transform.paddedSize(), QSize(200, 100));
        QCOMPARE(transform.cropRect(), QRect(40, 20, 120, 60));

        // 模型中心点 (29.5, 14.5) 映射到 原图中心点 (99.5, 49.5)
        const QPointF center_model(29.5, 14.5);
        const QPointF center_image = transform.modelToImage(center_model);
        QVERIFY(std::abs(center_image.x() - 99.5) < 1e-9);
        QVERIFY(std::abs(center_image.y() - 49.5) < 1e-9);
        const QPointF center_roundtrip = transform.imageToModel(center_image);
        QVERIFY(std::abs(center_roundtrip.x() - center_model.x()) < 1e-9);
        QVERIFY(std::abs(center_roundtrip.y() - center_model.y()) < 1e-9);

        // 模型左上角边界 (-0.5, -0.5) 映射到 (24.5, 12.0)
        const QPointF topleft_model(-0.5, -0.5);
        const QPointF topleft_image = transform.modelToImage(topleft_model);
        QVERIFY(std::abs(topleft_image.x() - 24.5) < 1e-9);
        QVERIFY(std::abs(topleft_image.y() - 12.0) < 1e-9);
        const QPointF topleft_roundtrip = transform.imageToModel(topleft_image);
        QVERIFY(std::abs(topleft_roundtrip.x() - topleft_model.x()) < 1e-9);
        QVERIFY(std::abs(topleft_roundtrip.y() - topleft_model.y()) < 1e-9);

        // 模型右下角边界 (59.5, 29.5) 映射到 (174.5, 87.0)
        const QPointF bottomright_model(59.5, 29.5);
        const QPointF bottomright_image = transform.modelToImage(bottomright_model);
        QVERIFY(std::abs(bottomright_image.x() - 174.5) < 1e-9);
        QVERIFY(std::abs(bottomright_image.y() - 87.0) < 1e-9);
        const QPointF bottomright_roundtrip = transform.imageToModel(bottomright_image);
        QVERIFY(std::abs(bottomright_roundtrip.x() - bottomright_model.x()) < 1e-9);
        QVERIFY(std::abs(bottomright_roundtrip.y() - bottomright_model.y()) < 1e-9);

        // 内部定点 (9.5, 4.5) 映射到 (49.5, 24.5)
        const QPointF interior_model(9.5, 4.5);
        const QPointF interior_image = transform.modelToImage(interior_model);
        QVERIFY(std::abs(interior_image.x() - 49.5) < 1e-9);
        QVERIFY(std::abs(interior_image.y() - 24.5) < 1e-9);

        // 2. 真实评估引擎端到端固定坐标断言（非方形图片 + TIFF 分数图）
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::AnomalyDetection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 normal_class = fixture.addClass(QStringLiteral("Normal"), QStringLiteral("normal"));
        const qint64 anomaly_class = fixture.addClass(QStringLiteral("Defect"), QStringLiteral("anomaly"));
        const qint64 image = fixture.addImage(QStringLiteral("nonsquare_sample"));
        QVERIFY(fixture.addAnomalyLabel(image, anomaly_class, {{1, 1}, {8, 1}, {8, 8}, {1, 8}}) >= 0);
        QVERIFY(fixture.writeImageList());

        // 覆盖为真实 200 x 100 图像文件
        QImage nonsquare_file(200, 100, QImage::Format_RGBA8888);
        nonsquare_file.fill(QColor(100, 100, 100));
        QVERIFY(nonsquare_file.save(fixture.imagePaths().front(), "PNG"));

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager model_manager(static_cast<int>(evaluation::Method::AnomalyDetection), &database, nullptr);
        QString      error;
        const auto record = model_manager.addModelRecord(QStringLiteral("GeomModel"), QStringLiteral("anomalib"),
                                                         QStringLiteral("patchcore"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        ModelTestTaskManager manager(fixture.rootPath(), &model_manager, nullptr, nullptr);
        manager.setModelUuid(record.uuid);
        QVERIFY2(prepareEvaluationInputs(fixture, manager, record, image,
                                         anomalyPrediction(0.9), true, &error),
                 qPrintable(error));

        // 写入与几何配置匹配的 60 x 30 TIFF 分数图，在 [10..30, 5..15] 放置高异常区域
        const ModelStorageService storage(fixture.rootPath());
        const QString pred_dir = storage.testTaskPredictionPath(record.name, manager.currentTaskDirectory());
        QVERIFY(QDir().mkpath(pred_dir));
        const QString tiff_path = QDir(pred_dir).filePath(QStringLiteral("%1.tiff").arg(image));
        cv::Mat custom_map = cv::Mat::zeros(30, 60, CV_32FC1);
        custom_map.setTo(0.05F);
        custom_map(cv::Range(5, 15), cv::Range(10, 30)).setTo(0.95F);
        const QString source_tiff = QDir(fixture.predictionDirectory()).filePath(QStringLiteral("%1.tiff").arg(image));
        QVERIFY(cv::imwrite(source_tiff.toStdString(), custom_map));
        QFile::remove(tiff_path);
        QVERIFY(QFile::copy(source_tiff, tiff_path));

        // 写入固定预处理配置到 task.db
        const QString task_db_path = storage.testTaskDatabasePath(record.name, manager.currentTaskDirectory());
        dltool::database::ModelTaskDataBase task_db(task_db_path);
        QVERIFY(task_db.writePreprocessingConfig(preprocessing));

        ModelEvaluationOptions options;
        QVERIFY(manager.buildEvaluationOptions(options, &error));
        auto *evaluation = manager.currentEvaluation();
        QVERIFY(evaluation != nullptr);
        evaluation->setEvaluationOptions(options);
        evaluation->evaluate(false);
        QTRY_COMPARE_WITH_TIMEOUT(evaluation->stateKind(), ModelEvaluationViewModel::Ready, 5000);

        const auto *instances = evaluation->instances();
        QVERIFY(instances != nullptr);
        QCOMPARE(instances->rowCount(), 1);

        const QVariantList model_polygons
            = instances->data(instances->index(0, 0), EvaluationInstanceModel::AnomalyModelPolygonsRole).toList();
        const QVariantList image_polygons
            = instances->data(instances->index(0, 0), EvaluationInstanceModel::AnomalyImagePolygonsRole).toList();
        QVERIFY(!model_polygons.isEmpty());
        QVERIFY(!image_polygons.isEmpty());
        QCOMPARE(model_polygons.size(), image_polygons.size());

        const QVariantList model_contour = model_polygons.front().toList();
        const QVariantList image_contour = image_polygons.front().toList();
        QVERIFY(model_contour.size() >= 4);
        QCOMPARE(model_contour.size(), image_contour.size());

        for (int i = 0; i < model_contour.size(); ++i)
        {
            const QVariantMap m_pt = model_contour.at(i).toMap();
            const QVariantMap i_pt = image_contour.at(i).toMap();
            const double mx = m_pt.value(QStringLiteral("x")).toDouble();
            const double my = m_pt.value(QStringLiteral("y")).toDouble();
            const double ix = i_pt.value(QStringLiteral("x")).toDouble();
            const double iy = i_pt.value(QStringLiteral("y")).toDouble();

            // 模型坐标位于 60x30 的 [10..30, 5..15] 异常矩形边缘
            QVERIFY(mx >= 9.0 && mx <= 31.0);
            QVERIFY(my >= 4.0 && my <= 16.0);

            // 原图多边形坐标必须严格等于 transform.modelToImage(m_pt)
            const QPointF expected_mapped = transform.modelToImage(QPointF(mx, my));
            QVERIFY(std::abs(ix - expected_mapped.x()) < 1e-4);
            QVERIFY(std::abs(iy - expected_mapped.y()) < 1e-4);
        }
    }

    void modifyingCurrentTrainParamsDoesNotAlterOldPredictionInterpretation()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::AnomalyDetection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 normal_class = fixture.addClass(QStringLiteral("Normal"), QStringLiteral("normal"));
        const qint64 anomaly_class = fixture.addClass(QStringLiteral("Defect"), QStringLiteral("anomaly"));
        const qint64 image = fixture.addImage(QStringLiteral("frozen_sample"));
        QVERIFY(fixture.addAnomalyLabel(image, anomaly_class, {{2, 2}, {6, 2}, {6, 6}, {2, 6}}) >= 0);
        QVERIFY(fixture.writeImageList());

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager model_manager(static_cast<int>(evaluation::Method::AnomalyDetection), &database, nullptr);
        QString      error;
        const auto record = model_manager.addModelRecord(QStringLiteral("FrozenModel"), QStringLiteral("anomalib"),
                                                         QStringLiteral("patchcore"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        IModel *model = model_manager.modelForUuid(record.uuid);
        QVERIFY(model != nullptr && model->config() != nullptr && model->config()->trainParams() != nullptr);

        auto *network_group = findGroup(model->config()->trainParams(), QStringLiteral("network"));
        QVERIFY(network_group != nullptr);
        network_group->setValueForName(QStringLiteral("image_size"), 256);
        network_group->setValueForName(QStringLiteral("center_crop_size"), 200);

        ModelTestTaskManager manager(fixture.rootPath(), &model_manager, nullptr, nullptr);
        manager.setModelUuid(record.uuid);
        QVERIFY2(prepareEvaluationInputs(fixture, manager, record, image,
                                         anomalyPrediction(0.92), true, &error),
                 qPrintable(error));

        // 将初始预处理配置持久化至 task.db
        const ModelStorageService storage(fixture.rootPath());
        const QString task_db_path = storage.testTaskDatabasePath(record.name, manager.currentTaskDirectory());
        dltool::database::ModelTaskDataBase task_db(task_db_path);
        QVariantMap original_prep;
        original_prep.insert(QStringLiteral("image_size"), 256);
        original_prep.insert(QStringLiteral("center_crop_size"), 200);
        QVERIFY(task_db.writePreprocessingConfig(original_prep));

        ModelEvaluationOptions initial_options;
        QVERIFY(manager.buildEvaluationOptions(initial_options, &error));
        auto *evaluation = manager.currentEvaluation();
        QVERIFY(evaluation != nullptr);
        evaluation->setEvaluationOptions(initial_options);
        evaluation->evaluate(false);
        QTRY_COMPARE_WITH_TIMEOUT(evaluation->stateKind(), ModelEvaluationViewModel::Ready, 5000);

        const auto *instances = evaluation->instances();
        QVERIFY(instances != nullptr && instances->rowCount() == 1);
        const QVariantList initial_image_polygons
            = instances->data(instances->index(0, 0), EvaluationInstanceModel::AnomalyImagePolygonsRole).toList();
        const QVariantList initial_model_polygons
            = instances->data(instances->index(0, 0), EvaluationInstanceModel::AnomalyModelPolygonsRole).toList();
        QVERIFY(!initial_image_polygons.isEmpty());

        const QString pred_dir = storage.testTaskPredictionPath(record.name, manager.currentTaskDirectory());
        const QString tiff_path = QDir(pred_dir).filePath(QStringLiteral("%1.tiff").arg(image));
        const QString initial_heatmap_url = evaluation->heatmapThumbnailUrl(image, fixture.imagePaths().front(), tiff_path, 0.5);
        QVERIFY(initial_heatmap_url.contains(QStringLiteral("image_size")) && initial_heatmap_url.contains(QStringLiteral("256")));

        // 验收条件 2: 修改当前模型的训练参数，绝对不能改变已有预测的几何解释
        network_group->setValueForName(QStringLiteral("image_size"), 512);
        network_group->setValueForName(QStringLiteral("center_crop_size"), 400);

        ModelEvaluationOptions options_after_param_edit;
        QVERIFY(manager.buildEvaluationOptions(options_after_param_edit, &error));
        QCOMPARE(options_after_param_edit.preprocessing_config.value(QStringLiteral("image_size")).toInt(), 256);
        QCOMPARE(options_after_param_edit.preprocessing_config.value(QStringLiteral("center_crop_size")).toInt(), 200);

        // 刷新评估重算
        evaluation->refreshEvaluation();
        QTRY_COMPARE_WITH_TIMEOUT(evaluation->stateKind(), ModelEvaluationViewModel::Ready, 5000);

        const QVariantList after_image_polygons
            = instances->data(instances->index(0, 0), EvaluationInstanceModel::AnomalyImagePolygonsRole).toList();
        const QVariantList after_model_polygons
            = instances->data(instances->index(0, 0), EvaluationInstanceModel::AnomalyModelPolygonsRole).toList();
        QCOMPARE(after_image_polygons, initial_image_polygons);
        QCOMPARE(after_model_polygons, initial_model_polygons);

        const QString after_heatmap_url = evaluation->heatmapThumbnailUrl(image, fixture.imagePaths().front(), tiff_path, 0.5);
        QCOMPARE(after_heatmap_url, initial_heatmap_url);
    }

    void confusionMatrixSwitchingKeepsIdenticalRedPolygonAndReusesInspectionOverlay()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::AnomalyDetection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 normal_class = fixture.addClass(QStringLiteral("Normal"), QStringLiteral("normal"));
        const qint64 anomaly_class = fixture.addClass(QStringLiteral("Defect"), QStringLiteral("anomaly"));
        const qint64 good_image = fixture.addImage(QStringLiteral("good_sample"), {{QStringLiteral("image_label_class_id"), normal_class}});
        const qint64 bad_image = fixture.addImage(QStringLiteral("defect_sample"), {{QStringLiteral("image_label_class_id"), anomaly_class}});
        QVERIFY(fixture.addAnomalyLabel(bad_image, anomaly_class, {{2, 2}, {10, 2}, {10, 10}, {2, 10}}) >= 0);
        QVERIFY(fixture.writeImageList());

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager model_manager(static_cast<int>(evaluation::Method::AnomalyDetection), &database, nullptr);
        QString      error;
        const auto record = model_manager.addModelRecord(QStringLiteral("OverlayModel"), QStringLiteral("anomalib"),
                                                         QStringLiteral("patchcore"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        ModelTestTaskManager manager(fixture.rootPath(), &model_manager, nullptr, nullptr);
        manager.setModelUuid(record.uuid);
        QVERIFY2(prepareEvaluationInputs(fixture, manager, record, good_image,
                                         anomalyPrediction(0.1), true, &error),
                 qPrintable(error));
        QVERIFY2(prepareEvaluationInputs(fixture, manager, record, bad_image,
                                         anomalyPrediction(0.95), true, &error),
                 qPrintable(error));

        auto *evaluation = manager.currentEvaluation();
        QVERIFY(evaluation != nullptr);
        evaluation->evaluate(false);
        QTRY_COMPARE_WITH_TIMEOUT(evaluation->stateKind(), ModelEvaluationViewModel::Ready, 5000);

        // 1. 首次打开（未过滤混淆矩阵）：显示全部实例，找到异常实例的多边形
        auto *filtered_instances = evaluation->filteredInstances();
        QVERIFY(filtered_instances != nullptr);
        QCOMPARE(filtered_instances->rowCount(), 2);

        QVariantList initial_tp_model_polygons;
        QVariantList initial_tp_image_polygons;
        for (int row = 0; row < filtered_instances->rowCount(); ++row)
        {
            const QModelIndex idx = filtered_instances->index(row, 0);
            const qint64 img_id = idx.data(EvaluationInstanceModel::ImageIdRole).toLongLong();
            if (img_id == bad_image)
            {
                initial_tp_model_polygons = idx.data(EvaluationInstanceModel::AnomalyModelPolygonsRole).toList();
                initial_tp_image_polygons = idx.data(EvaluationInstanceModel::AnomalyImagePolygonsRole).toList();
                break;
            }
        }
        QVERIFY(!initial_tp_model_polygons.isEmpty());
        QVERIFY(!initial_tp_image_polygons.isEmpty());

        // 2. 查找混淆矩阵并选中 TP 单元格
        auto *matrix = evaluation->confusionMatrix();
        QVERIFY(matrix != nullptr);
        int tp_row = -1;
        int tp_col = -1;
        int tn_row = -1;
        int tn_col = -1;
        for (int r = 0; r < matrix->rowCount(); ++r)
        {
            for (int c = 0; c < matrix->columnCount(); ++c)
            {
                const QModelIndex midx = matrix->index(r, c);
                const int r_cid = midx.data(EvaluationConfusionModel::RowClassIdRole).toInt();
                const int c_cid = midx.data(EvaluationConfusionModel::ColumnClassIdRole).toInt();
                const int count = midx.data(EvaluationConfusionModel::CountRole).toInt();
                if (r_cid == 1 && c_cid == anomaly_class && count == 1)
                {
                    tp_row = r;
                    tp_col = c;
                }
                else if (r_cid == 0 && c_cid == normal_class && count == 1)
                {
                    tn_row = r;
                    tn_col = c;
                }
            }
        }
        QVERIFY(tp_row >= 0 && tp_col >= 0);
        QVERIFY(tn_row >= 0 && tn_col >= 0);

        // 切换到 TP 单元格
        QVERIFY(evaluation->selectConfusionCell(tp_row, tp_col));
        QCOMPARE(filtered_instances->rowCount(), 1);
        const QModelIndex tp_idx = filtered_instances->index(0, 0);
        QCOMPARE(tp_idx.data(EvaluationInstanceModel::ImageIdRole).toLongLong(), bad_image);
        // 验收条件 3: 混淆矩阵切换后仍显示同一红色 polygon
        QCOMPARE(tp_idx.data(EvaluationInstanceModel::AnomalyModelPolygonsRole).toList(), initial_tp_model_polygons);
        QCOMPARE(tp_idx.data(EvaluationInstanceModel::AnomalyImagePolygonsRole).toList(), initial_tp_image_polygons);

        // 切换到 TN 单元格，验证正常样本无异常多边形
        QVERIFY(evaluation->selectConfusionCell(tn_row, tn_col));
        QCOMPARE(filtered_instances->rowCount(), 1);
        const QModelIndex tn_idx = filtered_instances->index(0, 0);
        QCOMPARE(tn_idx.data(EvaluationInstanceModel::ImageIdRole).toLongLong(), good_image);
        QVERIFY(tn_idx.data(EvaluationInstanceModel::AnomalyModelPolygonsRole).toList().isEmpty());

        // 再次切换回 TP 单元格，多边形数值依然完全一致
        QVERIFY(evaluation->selectConfusionCell(tp_row, tp_col));
        QCOMPARE(filtered_instances->rowCount(), 1);
        QCOMPARE(filtered_instances->index(0, 0).data(EvaluationInstanceModel::AnomalyModelPolygonsRole).toList(),
                 initial_tp_model_polygons);
        QCOMPARE(filtered_instances->index(0, 0).data(EvaluationInstanceModel::AnomalyImagePolygonsRole).toList(),
                 initial_tp_image_polygons);

        // 清除混淆矩阵筛选，回到全部视图，多边形仍然完全一致
        evaluation->clearConfusionCellFilter();
        QCOMPARE(filtered_instances->rowCount(), 2);
        for (int row = 0; row < filtered_instances->rowCount(); ++row)
        {
            const QModelIndex idx = filtered_instances->index(row, 0);
            if (idx.data(EvaluationInstanceModel::ImageIdRole).toLongLong() == bad_image)
            {
                QCOMPARE(idx.data(EvaluationInstanceModel::AnomalyModelPolygonsRole).toList(), initial_tp_model_polygons);
                QCOMPARE(idx.data(EvaluationInstanceModel::AnomalyImagePolygonsRole).toList(), initial_tp_image_polygons);
            }
        }
    }

    void crossTaskEvaluationCacheEvictionUnderBudget()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::AnomalyDetection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        fixture.addClass(QStringLiteral("Normal"), QStringLiteral("normal"));
        const qint64 anomaly_class = fixture.addClass(QStringLiteral("Defect"), QStringLiteral("anomaly"));
        const qint64 image = fixture.addImage(QStringLiteral("sample_img"));
        QVERIFY(fixture.addAnomalyLabel(image, anomaly_class, {{2, 2}, {6, 2}, {6, 6}, {2, 6}}) >= 0);
        QVERIFY(fixture.writeImageList());

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager model_manager(static_cast<int>(evaluation::Method::AnomalyDetection), &database, nullptr);
        QString      error;
        const auto record = model_manager.addModelRecord(QStringLiteral("LRUTestModel"), QStringLiteral("anomalib"),
                                                         QStringLiteral("patchcore"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        ModelTestTaskManager manager(fixture.rootPath(), &model_manager, nullptr, nullptr);
        manager.setModelUuid(record.uuid);
        QCOMPARE(manager.count(), 1);
        const QString task1_uuid = manager.currentTaskUuid();
        QVERIFY(!task1_uuid.isEmpty());
        prepareEvaluationInputs(fixture, manager, record, image, anomalyPrediction(0.8), true, &error);

        // 设定跨任务 VM 缓存预算上限为 2
        manager.setMaxCachedEvaluations(2);
        QCOMPARE(manager.maxCachedEvaluations(), 2);
        QCOMPARE(manager.cachedEvaluationCount(), 1); // 仅有初始 task1
        QCOMPARE(manager.evictedEvaluationCount(), 0);

        // 创建 task2: 缓存包含 task1 与 task2（达到上限 2，未淘汰）
        const QString task2_uuid = manager.createTask(QStringLiteral("测试 2"));
        QVERIFY(!task2_uuid.isEmpty());
        prepareEvaluationInputs(fixture, manager, record, image, anomalyPrediction(0.8), true, &error);
        QCOMPARE(manager.cachedEvaluationCount(), 2);
        QCOMPARE(manager.evictedEvaluationCount(), 0);

        // 创建 task3: 缓存已有 task1 与 task2，新增 task3 超过上限 2，按 LRU 淘汰最旧且空闲的 task1
        const QString task3_uuid = manager.createTask(QStringLiteral("测试 3"));
        QVERIFY(!task3_uuid.isEmpty());
        prepareEvaluationInputs(fixture, manager, record, image, anomalyPrediction(0.8), true, &error);
        QCOMPARE(manager.count(), 3);
        QCOMPARE(manager.cachedEvaluationCount(), 2);
        QCOMPARE(manager.evictedEvaluationCount(), 1);

        // 切换到 task2: task2 仍在缓存中（淘汰的是 task1），未发生新淘汰
        QVERIFY(manager.switchTask(task2_uuid));
        QCOMPARE(manager.cachedEvaluationCount(), 2);
        QCOMPARE(manager.evictedEvaluationCount(), 1);

        // 切换到 task1: task1 此前已被淘汰，重新创建并淘汰当前空闲且最旧的 task3
        QVERIFY(manager.switchTask(task1_uuid));
        QCOMPARE(manager.cachedEvaluationCount(), 2);
        QCOMPARE(manager.evictedEvaluationCount(), 2);
    }

    void multiTaskEvaluationStressAndVisualCacheBudgetUnderPressure()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::AnomalyDetection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        fixture.addClass(QStringLiteral("Normal"), QStringLiteral("normal"));
        const qint64 anomaly_class = fixture.addClass(QStringLiteral("Defect"), QStringLiteral("anomaly"));
        const qint64 image = fixture.addImage(QStringLiteral("stress_sample"));
        QVERIFY(fixture.addAnomalyLabel(image, anomaly_class, {{2, 2}, {6, 2}, {6, 6}, {2, 6}}) >= 0);
        QVERIFY(fixture.writeImageList());

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager model_manager(static_cast<int>(evaluation::Method::AnomalyDetection), &database, nullptr);
        QString      error;
        const auto record = model_manager.addModelRecord(QStringLiteral("StressModel"), QStringLiteral("anomalib"),
                                                         QStringLiteral("patchcore"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        ModelTestTaskManager manager(fixture.rootPath(), &model_manager, nullptr, nullptr);
        manager.setModelUuid(record.uuid);

        // 1. 创建 5 个测试任务，设置缓存上限为 2
        manager.setMaxCachedEvaluations(2);
        QCOMPARE(manager.maxCachedEvaluations(), 2);

        QStringList task_uuids;
        task_uuids.append(manager.currentTaskUuid());
        for (int i = 2; i <= 5; ++i)
        {
            const QString task_uuid = manager.createTask(QStringLiteral("压力测试 %1").arg(i));
            QVERIFY(!task_uuid.isEmpty());
            task_uuids.append(task_uuid);
        }
        QCOMPARE(task_uuids.size(), 5);
        QCOMPARE(manager.count(), 5);

        // 为每个任务准备测试输入
        for (const QString &uuid : task_uuids)
        {
            QVERIFY(manager.switchTask(uuid));
            prepareEvaluationInputs(fixture, manager, record, image, anomalyPrediction(0.85), true, &error);
        }

        // 密集循环切换 5 个任务，断言缓存总量恒定 <= 2，不会发生内存与 VM 泄漏
        const QList<int> switch_sequence = {0, 1, 2, 3, 4, 0, 2, 4, 1, 3, 4, 2, 0, 3, 1};
        for (const int task_idx : switch_sequence)
        {
            QVERIFY(manager.switchTask(task_uuids[task_idx]));
            QVERIFY2(manager.cachedEvaluationCount() <= 2,
                     qPrintable(QString("缓存评估数超出预算上限: %1 > 2").arg(manager.cachedEvaluationCount())));
        }
        QVERIFY(manager.evictedEvaluationCount() >= 5);

        // 2. 视觉请求与图像缓存并发预算约束测试：
        // 验证多线程生成、同 key 合并及已完成图像的缓存容量。
        detail::EvaluationImageRequestCache image_cache(256 * 1024, 8); // 256 KB 预算，最大 8 并发
        QCOMPARE(image_cache.maxCost(), 256 * 1024);
        QCOMPARE(image_cache.maxPending(), 8);

        std::atomic<int>  shared_loader_runs{0};
        std::atomic<int>  total_loader_runs{0};
        std::atomic<int>  active_in_loader{0};
        std::atomic<bool> barrier_release{false};
        std::atomic<bool> over_budget_detected{false};

        const int num_workers = 8;
        std::vector<std::future<void>> worker_futures;
        for (int t = 0; t < num_workers; ++t)
        {
            worker_futures.push_back(std::async(std::launch::async, [&image_cache, &shared_loader_runs, &total_loader_runs, &active_in_loader, &barrier_release, &over_budget_detected, t]() {
                for (int round = 0; round < 6; ++round)
                {
                    // 偶数轮使用跨 worker 共享 key 测试去重合并；奇数轮使用独占 key 制造并发 miss 和内存压力
                    const QString key = (round % 2 == 0)
                        ? QStringLiteral("shared_key_%1").arg(round)
                        : QStringLiteral("worker_%1_key_%2").arg(t).arg(round);

                    // 每张 ARGB32 图像占用 48,400 字节。
                    QImage img = image_cache.getOrCreate(key, [&shared_loader_runs, &total_loader_runs, &active_in_loader, &barrier_release, round]() {
                        total_loader_runs.fetch_add(1, std::memory_order_relaxed);
                        if (round % 2 == 0)
                            shared_loader_runs.fetch_add(1, std::memory_order_relaxed);

                        if (round == 1)
                        {
                            // 在 round 1 独占 key 阶段，多个 worker 同时进入 loader，通过屏障确保并发重叠与 peak_pending > 1
                            active_in_loader.fetch_add(1, std::memory_order_relaxed);
                            for (int wait_i = 0; wait_i < 3000 && !barrier_release.load(std::memory_order_relaxed); ++wait_i)
                                QThread::msleep(1);
                        }
                        else
                        {
                            QThread::msleep(5);
                        }

                        QImage dummy(110, 110, QImage::Format_ARGB32);
                        dummy.fill(Qt::blue);
                        return dummy;
                    });

                    if (image_cache.totalCost() > 256 * 1024)
                        over_budget_detected.store(true, std::memory_order_relaxed);
                    Q_UNUSED(img);
                }
            }));
        }

        // 控制线程：等待至少 3 个 worker 线程同时处于 loader 执行与 pending 状态中
        for (int wait_i = 0; wait_i < 5000 && active_in_loader.load(std::memory_order_relaxed) < 3; ++wait_i)
            QThread::msleep(1);

        // 在多个请求真正并发处于 pending / in-flight 状态时，执行动态缩容至 64 KiB
        image_cache.setMaxCost(64 * 1024);
        if (image_cache.totalCost() > image_cache.maxCost())
            over_budget_detected.store(true, std::memory_order_relaxed);

        // 释放屏障，允许所有进行中的 worker 继续执行完成
        barrier_release.store(true, std::memory_order_relaxed);

        for (auto &f : worker_futures)
            f.get();

        QVERIFY2(!over_budget_detected.load(), "已完成图像缓存超过容量限制");
        QVERIFY2(image_cache.totalCost() <= image_cache.maxCost(),
                 qPrintable(QString("图像缓存总预算突破: %1 > %2").arg(image_cache.totalCost()).arg(image_cache.maxCost())));
        QCOMPARE(image_cache.maxCost(), 64 * 1024);

        // 必须实测观察到并发峰值 peak_pending > 1
        QVERIFY2(image_cache.peakPendingCount() > 1,
                 qPrintable(QString("并发压力下 peakPendingCount 应 > 1，实际: %1").arg(image_cache.peakPendingCount())));
        QVERIFY(image_cache.peakPendingCount() <= image_cache.maxPending());
        // 淘汰后再次访问允许重新生成；同一在途请求的去重由独立同步测试验证。
        QVERIFY(shared_loader_runs.load() >= 3);
        QVERIFY(shared_loader_runs.load() <= num_workers * 3);
        QVERIFY(image_cache.hitCount() > 0);

        // 大于缓存容量的图像正常返回，但不保留在缓存中。
        image_cache.setMaxCost(256 * 1024);
        std::atomic<bool> huge_loader_executed{false};
        const QImage huge_img = image_cache.getOrCreate(QStringLiteral("huge_oversized_sample"), [&huge_loader_executed]() {
            huge_loader_executed.store(true, std::memory_order_relaxed);
            QImage dummy(300, 300, QImage::Format_ARGB32);
            dummy.fill(Qt::red);
            return dummy;
        });
        QVERIFY(!huge_img.isNull());
        QVERIFY(huge_loader_executed.load());
        QCOMPARE(huge_img.size(), QSize(300, 300));
        QVERIFY2(image_cache.totalCost() <= image_cache.maxCost(),
                 "超大单图生成后不得突破缓存总预算");

        qInfo() << "[Evidence Ticket 23] Multi-task 5-task switching under budget: max_cached="
                << manager.maxCachedEvaluations()
                << "current_cached=" << manager.cachedEvaluationCount()
                << "total_evictions=" << manager.evictedEvaluationCount();
        qInfo() << "[Evidence Ticket 23] Visual cache budget strictly enforced under concurrency: total_cost="
                << image_cache.totalCost() << "<= max_cost=" << image_cache.maxCost()
                << "hits=" << image_cache.hitCount()
                << "misses=" << image_cache.missCount()
                << "peak_pending=" << image_cache.peakPendingCount();

    }

    void heatmapThresholdChangePreservesMetricsAndPolygonsWhileUpdatingVisualUrl()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::AnomalyDetection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        fixture.addClass(QStringLiteral("Normal"), QStringLiteral("normal"));
        const qint64 anomaly_class = fixture.addClass(QStringLiteral("Defect"), QStringLiteral("anomaly"));
        const qint64 image = fixture.addImage(QStringLiteral("heatmap_test_sample"));
        QVERIFY(fixture.addAnomalyLabel(image, anomaly_class, {{2, 2}, {6, 2}, {6, 6}, {2, 6}}) >= 0);
        QVERIFY(fixture.writeImageList());

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager model_manager(static_cast<int>(evaluation::Method::AnomalyDetection), &database, nullptr);
        QString      error;
        const auto record = model_manager.addModelRecord(QStringLiteral("HeatmapTestModel"), QStringLiteral("anomalib"),
                                                         QStringLiteral("patchcore"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        ModelTestTaskManager manager(fixture.rootPath(), &model_manager, nullptr, nullptr);
        manager.setModelUuid(record.uuid);
        QVERIFY2(prepareEvaluationInputs(fixture, manager, record, image, anomalyPrediction(0.88), true, &error),
                 qPrintable(error));

        auto *evaluation = manager.currentEvaluation();
        QVERIFY(evaluation != nullptr);
        evaluation->evaluate(false);
        QTRY_COMPARE_WITH_TIMEOUT(evaluation->stateKind(), ModelEvaluationViewModel::Ready, 5000);

        const int initial_eval_count = evaluation->evaluationCount();
        QCOMPARE(initial_eval_count, 1);
        const auto *instances = evaluation->instances();
        QVERIFY(instances != nullptr && instances->rowCount() == 1);
        const QVariantList initial_image_polygons
            = instances->data(instances->index(0, 0), EvaluationInstanceModel::AnomalyImagePolygonsRole).toList();
        const QVariantList initial_model_polygons
            = instances->data(instances->index(0, 0), EvaluationInstanceModel::AnomalyModelPolygonsRole).toList();
        QVERIFY(!initial_image_polygons.isEmpty());
        const auto &metric_records = evaluation->imageMetrics()->records();
        QVERIFY(!metric_records.empty());
        const double initial_f1 = metric_records.front().f1;
        const qint64 initial_tp = metric_records.front().tp;

        // 模拟 QML 调整热力图阈值滑块
        ITestParams *params = manager.currentTestParams();
        QVERIFY(params != nullptr);
        auto *eval_group = findGroup(params, QStringLiteral("evaluation"));
        if (eval_group != nullptr)
            eval_group->setValueForName(QStringLiteral("heatmap_threshold"), 0.35);

        // 验收条件 2: 仅热力图阈值变化绝对不无故重算指标（evaluationCount 保持 1，F1/Polygon 复用）
        QCOMPARE(evaluation->evaluationCount(), initial_eval_count);
        const auto &updated_metric_records = evaluation->imageMetrics()->records();
        QVERIFY(!updated_metric_records.empty());
        QCOMPARE(updated_metric_records.front().f1, initial_f1);
        QCOMPARE(updated_metric_records.front().tp, initial_tp);
        QCOMPARE(instances->data(instances->index(0, 0), EvaluationInstanceModel::AnomalyImagePolygonsRole).toList(),
                 initial_image_polygons);
        QCOMPARE(instances->data(instances->index(0, 0), EvaluationInstanceModel::AnomalyModelPolygonsRole).toList(),
                 initial_model_polygons);

        // 视觉热力图 URL 更新，并且包含新的阈值 0.35
        const ModelStorageService storage(fixture.rootPath());
        const QString pred_dir = storage.testTaskPredictionPath(record.name, manager.currentTaskDirectory());
        const QString tiff_path = QDir(pred_dir).filePath(QStringLiteral("%1.tiff").arg(image));
        const QString updated_heatmap_url = evaluation->heatmapThumbnailUrl(image, fixture.imagePaths().front(), tiff_path, 0.35);
        QVERIFY(updated_heatmap_url.contains(QStringLiteral("heatmapThreshold=0.35")));
    }

    void cancellationAndReInferenceRejectsStaleEvaluationResults()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::AnomalyDetection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        fixture.addClass(QStringLiteral("Normal"), QStringLiteral("normal"));
        const qint64 anomaly_class = fixture.addClass(QStringLiteral("Defect"), QStringLiteral("anomaly"));
        const qint64 image = fixture.addImage(QStringLiteral("cancel_sample"));
        QVERIFY(fixture.addAnomalyLabel(image, anomaly_class, {{2, 2}, {6, 2}, {6, 6}, {2, 6}}) >= 0);
        QVERIFY(fixture.writeImageList());

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager model_manager(static_cast<int>(evaluation::Method::AnomalyDetection), &database, nullptr);
        QString      error;
        const auto record = model_manager.addModelRecord(QStringLiteral("CancelModel"), QStringLiteral("anomalib"),
                                                         QStringLiteral("patchcore"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        ModelTestTaskManager manager(fixture.rootPath(), &model_manager, nullptr, nullptr);
        manager.setModelUuid(record.uuid);
        QVERIFY2(prepareEvaluationInputs(fixture, manager, record, image, anomalyPrediction(0.85), true, &error),
                 qPrintable(error));

        auto *evaluation = manager.currentEvaluation();
        QVERIFY(evaluation != nullptr);

        // 1. 发起评估后立即取消/失效
        evaluation->evaluate(false);
        evaluation->invalidate(evaluation::ViewState::NotRun);

        // 等待后台线程池完成运行
        QTest::qWait(200);

        // 验收条件 3: 取消后旧结果必须被拒绝/丢弃，状态保持 NotRun，且不发布任何旧结果
        QCOMPARE(evaluation->stateKind(), ModelEvaluationViewModel::NotRun);
        QCOMPARE(evaluation->instances()->rowCount(), 0);

        // 2. 发起重推理，验证新评估能够正常就绪
        evaluation->evaluate(false);
        QTRY_COMPARE_WITH_TIMEOUT(evaluation->stateKind(), ModelEvaluationViewModel::Ready, 5000);
        QCOMPARE(evaluation->instances()->rowCount(), 1);
    }

    void firstEvaluationAppliesBestThresholdWithoutTriggeringSecondEvaluation()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::AnomalyDetection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        fixture.addClass(QStringLiteral("Normal"), QStringLiteral("normal"));
        const qint64 anomaly_class = fixture.addClass(QStringLiteral("Defect"), QStringLiteral("anomaly"));
        const qint64 image = fixture.addImage(QStringLiteral("first_eval_sample"));
        QVERIFY(fixture.addAnomalyLabel(image, anomaly_class, {{2, 2}, {6, 2}, {6, 6}, {2, 6}}) >= 0);
        QVERIFY(fixture.writeImageList());

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager model_manager(static_cast<int>(evaluation::Method::AnomalyDetection), &database, nullptr);
        QString      error;
        const auto record = model_manager.addModelRecord(QStringLiteral("FirstEvalModel"), QStringLiteral("anomalib"),
                                                         QStringLiteral("patchcore"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        ModelTestTaskManager manager(fixture.rootPath(), &model_manager, nullptr, nullptr);
        manager.setModelUuid(record.uuid);
        const QString task_uuid = manager.currentTaskUuid();
        QVERIFY(!task_uuid.isEmpty());
        QVERIFY2(prepareEvaluationInputs(fixture, manager, record, image, anomalyPrediction(0.77), true, &error),
                 qPrintable(error));

        const ModelStorageService storage(fixture.rootPath());
        const QString task_db_path = storage.testTaskDatabasePath(record.name, manager.currentTaskDirectory());
        dltool::database::ModelTaskDataBase task_db(task_db_path);

        // 验证首评前尚未记录自动应用事实
        bool applied_before = false;
        QVERIFY(task_db.readAdaptiveThresholdApplied(applied_before));
        QVERIFY(!applied_before);

        auto *evaluation = manager.currentEvaluation();
        QVERIFY(evaluation != nullptr);

        // 触发首次评估
        evaluation->evaluate(false);
        QTRY_COMPARE_WITH_TIMEOUT(evaluation->stateKind(), ModelEvaluationViewModel::Ready, 5000);

        // 验收条件 2: 首评应用不触发第二次评估（evaluationCount 严格等于 1）
        QCOMPARE(evaluation->evaluationCount(), 1);
        QVERIFY(evaluation->hasBestThreshold());
        const double best = evaluation->bestThreshold();
        QVERIFY(std::isfinite(best));

        // 验证已自动写库标记应用事实
        bool applied_after = false;
        QVERIFY(task_db.readAdaptiveThresholdApplied(applied_after));
        QVERIFY(applied_after);

        // 验证当前测试参数已更新为最佳阈值
        ITestParams *params = manager.currentTestParams();
        QVERIFY(params != nullptr);
        auto *eval_group = findGroup(params, QStringLiteral("evaluation"));
        QVERIFY(eval_group != nullptr);
        const double current_param_threshold = eval_group->valueForName(QStringLiteral("classification_threshold")).toDouble();
        QCOMPARE(current_param_threshold, best);

        // 等待可能残留的异步信号，验证 evaluationCount 始终保持 1
        QTest::qWait(100);
        QCOMPARE(evaluation->evaluationCount(), 1);
    }

    void parameterModelEnforcesLegalityByClampingRangeAndPreservingInvalidOption()
    {
        std::vector<ParamDefinition> defs;
        defs.push_back(makeIntegerParam(QStringLiteral("epochs"), QStringLiteral("Epochs"), 10, 1, 100, 1));
        defs.push_back(makeSliderParam(QStringLiteral("conf"), QStringLiteral("Confidence"), 0.5, 0.0, 1.0, 0.05));
        defs.push_back(makeComboParam(QStringLiteral("model_type"), QStringLiteral("Type"), QStringLiteral("fast"),
                                      {QStringLiteral("fast"), QStringLiteral("accurate")}));

        ParamGroupModel group(QStringLiteral("train"), QStringLiteral("Train"), QStringLiteral("Training params"),
                              true, 0, std::move(defs));

        // 1. Integer clamping to range [1, 100]
        QVERIFY(group.setValueForName(QStringLiteral("epochs"), 150));
        QCOMPARE(group.valueForName(QStringLiteral("epochs")).toInt(), 100);

        QVERIFY(group.setValueForName(QStringLiteral("epochs"), -10));
        QCOMPARE(group.valueForName(QStringLiteral("epochs")).toInt(), 1);

        // 2. Double / slider clamping to range [0.0, 1.0]
        QVERIFY(group.setValueForName(QStringLiteral("conf"), 1.8));
        QCOMPARE(group.valueForName(QStringLiteral("conf")).toDouble(), 1.0);

        QVERIFY(group.setValueForName(QStringLiteral("conf"), -0.5));
        QCOMPARE(group.valueForName(QStringLiteral("conf")).toDouble(), 0.0);

        // 3. Invalid option value is preserved by model without auto-mutating
        QVERIFY(group.setValueForName(QStringLiteral("model_type"), QStringLiteral("custom_unlisted")));
        QCOMPARE(group.valueForName(QStringLiteral("model_type")).toString(), QStringLiteral("custom_unlisted"));
    }

    void nonEvaluationParametersOnlySaveWithoutEvaluationOrInference()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 cat   = fixture.addClass(QStringLiteral("Cat"), QStringLiteral("normal"));
        const qint64 image = fixture.addImage(QStringLiteral("cat"));
        QVERIFY(cat >= 0 && image >= 0);
        QVERIFY(fixture.addDetectionLabel(image, cat, 0, 0, 10, 10) >= 0);
        QVERIFY(fixture.writeImageList());

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager model_manager(static_cast<int>(evaluation::Method::Detection), &database, nullptr);
        QString      error;
        const auto record = model_manager.addModelRecord(QStringLiteral("NonEvalModel"), QStringLiteral("ultralytics"),
                                                         QStringLiteral("YOLOv8"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        TaskManager task_manager_instance;
        TaskManager *task_manager = &task_manager_instance;
        ModelTestTaskManager manager(fixture.rootPath(), &model_manager, nullptr, task_manager);
        manager.setModelUuid(record.uuid);
        QVERIFY2(prepareEvaluationInputs(fixture, manager, record, image,
                                         detectionPrediction(static_cast<int>(cat), QStringLiteral("Cat"), 0.9, 0, 0,
                                                             10, 10),
                                         true, &error),
                 qPrintable(error));

        auto *evaluation = manager.currentEvaluation();
        QVERIFY(evaluation != nullptr);
        evaluation->evaluate(false);
        QTRY_COMPARE_WITH_TIMEOUT(evaluation->stateKind(), ModelEvaluationViewModel::Ready, 5000);
        QCOMPARE(evaluation->evaluationCount(), 1);

        QSignalSpy loading_changed(evaluation, &ModelEvaluationViewModel::loadingChanged);

        auto *inference = findGroup(manager.currentTestParams(), QStringLiteral("inference"));
        QVERIFY(inference != nullptr);

        // 修改非 evaluation 参数 (如 batch_size)
        const int old_batch = inference->valueForName(QStringLiteral("batch_size")).toInt();
        QVERIFY(inference->setValueForName(QStringLiteral("batch_size"), old_batch + 2));

        // 验证：不触发评估（evaluationCount 保持 1，loadingChanged 为 0）
        QTest::qWait(100);
        QCOMPARE(loading_changed.count(), 0);
        QCOMPARE(evaluation->evaluationCount(), 1);

        // 验证：不自动启动任何推理任务
        QVERIFY(!task_manager->hasActiveModelTasks(record.uuid));
        QVERIFY(!manager.currentModelBusy());

        // 验证：flush() 后值正确保存到 task.db
        QVERIFY(manager.flush());
        const ModelStorageService storage(fixture.rootPath());
        const QString task_db_path = storage.testTaskDatabasePath(record.name, manager.currentTaskDirectory());
        dltool::database::ModelTaskDataBase task_db(task_db_path);
        QVariantMap saved_params;
        QVERIFY(task_db.readTestParams(saved_params, &error));
        QCOMPARE(saved_params.value(QStringLiteral("inference")).toMap().value(QStringLiteral("batch_size")).toInt(),
                 old_batch + 2);
        task_manager->clearTasks();
    }

    void evaluationParameterWithNoActualChangeDoesNotTriggerEvaluation()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 cat   = fixture.addClass(QStringLiteral("Cat"), QStringLiteral("normal"));
        const qint64 image = fixture.addImage(QStringLiteral("cat"));
        QVERIFY(cat >= 0 && image >= 0);
        QVERIFY(fixture.addDetectionLabel(image, cat, 0, 0, 10, 10) >= 0);
        QVERIFY(fixture.writeImageList());

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager model_manager(static_cast<int>(evaluation::Method::Detection), &database, nullptr);
        QString      error;
        const auto record = model_manager.addModelRecord(QStringLiteral("NoChangeEvalModel"), QStringLiteral("ultralytics"),
                                                         QStringLiteral("YOLOv8"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        TaskManager task_manager_instance;
        TaskManager *task_manager = &task_manager_instance;
        ModelTestTaskManager manager(fixture.rootPath(), &model_manager, nullptr, task_manager);
        manager.setModelUuid(record.uuid);
        QVERIFY2(prepareEvaluationInputs(fixture, manager, record, image,
                                         detectionPrediction(static_cast<int>(cat), QStringLiteral("Cat"), 0.9, 0, 0,
                                                             10, 10),
                                         true, &error),
                 qPrintable(error));

        auto *evaluation = manager.currentEvaluation();
        QVERIFY(evaluation != nullptr);
        evaluation->evaluate(false);
        QTRY_COMPARE_WITH_TIMEOUT(evaluation->stateKind(), ModelEvaluationViewModel::Ready, 5000);
        QCOMPARE(evaluation->evaluationCount(), 1);

        auto *eval_group = findGroup(manager.currentTestParams(), QStringLiteral("evaluation"));
        QVERIFY(eval_group != nullptr);
        const double current_conf = eval_group->valueForName(QStringLiteral("conf")).toDouble();

        QSignalSpy loading_changed(evaluation, &ModelEvaluationViewModel::loadingChanged);

        // 设置相同的 conf 值 (无变化)
        QVERIFY(eval_group->setValueForName(QStringLiteral("conf"), current_conf));

        QTest::qWait(100);
        // 验证：无变化不触发评估
        QCOMPARE(loading_changed.count(), 0);
        QCOMPARE(evaluation->evaluationCount(), 1);

        // 设置不同的 conf 值 (实际变化)
        const double new_conf = current_conf > 0.5 ? 0.3 : 0.8;
        QVERIFY(eval_group->setValueForName(QStringLiteral("conf"), new_conf));

        // 验证：实际变化触发评估重算
        QTRY_VERIFY_WITH_TIMEOUT(loading_changed.count() >= 2, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(evaluation->stateKind(), ModelEvaluationViewModel::Ready, 5000);
        QCOMPARE(evaluation->evaluationCount(), 2);
        QCOMPARE(evaluation->confidenceThreshold(), new_conf);
        task_manager->clearTasks();
    }

    void activeModelTaskLocksEditingAndRestoresOnTerminalStateWithoutLockingOtherModels()
    {
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 cat   = fixture.addClass(QStringLiteral("Cat"), QStringLiteral("normal"));
        const qint64 image = fixture.addImage(QStringLiteral("cat"));
        QVERIFY(cat >= 0 && image >= 0);
        QVERIFY(fixture.addDetectionLabel(image, cat, 0, 0, 10, 10) >= 0);
        QVERIFY(fixture.writeImageList());

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager model_manager(static_cast<int>(evaluation::Method::Detection), &database, nullptr);
        QString      error;
        const auto model1 = model_manager.addModelRecord(QStringLiteral("Model1"), QStringLiteral("ultralytics"),
                                                         QStringLiteral("YOLOv8"), &error);
        QVERIFY2(model1.isValid(), qPrintable(error));
        const auto model2 = model_manager.addModelRecord(QStringLiteral("Model2"), QStringLiteral("ultralytics"),
                                                         QStringLiteral("YOLOv8"), &error);
        QVERIFY2(model2.isValid(), qPrintable(error));

        TaskManager task_manager_instance;
        TaskManager *task_manager = &task_manager_instance;
        ModelTestTaskManager manager1(fixture.rootPath(), &model_manager, nullptr, task_manager);
        manager1.setModelUuid(model1.uuid);

        ModelTestTaskManager manager2(fixture.rootPath(), &model_manager, nullptr, task_manager);
        manager2.setModelUuid(model2.uuid);

        auto *params1 = manager1.currentTestParams();
        auto *params2 = manager2.currentTestParams();
        QVERIFY(params1 != nullptr && params2 != nullptr);
        QVERIFY(params1->isEnabled());
        QVERIFY(params2->isEnabled());

        // 启动 Model1 的任务
        const int task1 = task_manager->addTask(model1.uuid, model1.name, ModelTaskType::Train);
        QVERIFY(task1 > 0);
        QVERIFY(task_manager->startTask(task1));

        // 验证 Model1 处于忙碌状态，禁用编辑
        QVERIFY(manager1.currentModelBusy());
        QVERIFY(!params1->isEnabled());
        auto *inference1 = findGroup(params1, QStringLiteral("inference"));
        QVERIFY(inference1 != nullptr);
        const int old_batch1 = inference1->valueForName(QStringLiteral("batch_size")).toInt();
        // 尝试在任务运行期间修改 Model1 参数，必须被拒绝 (返回 false)
        QVERIFY(!inference1->setValueForName(QStringLiteral("batch_size"), old_batch1 + 10));
        QCOMPARE(inference1->valueForName(QStringLiteral("batch_size")).toInt(), old_batch1);

        // 验证 Model2 没有被无关锁定！
        QVERIFY(!manager2.currentModelBusy());
        QVERIFY(params2->isEnabled());
        auto *inference2 = findGroup(params2, QStringLiteral("inference"));
        QVERIFY(inference2 != nullptr);
        const int old_batch2 = inference2->valueForName(QStringLiteral("batch_size")).toInt();
        QVERIFY(inference2->setValueForName(QStringLiteral("batch_size"), old_batch2 + 5));
        QCOMPARE(inference2->valueForName(QStringLiteral("batch_size")).toInt(), old_batch2 + 5);

        // Model1 任务到达终态 (Stopped)
        QVERIFY(task_manager->stopTask(task1));
        QVERIFY(task_manager->markTaskStopped(task1));

        // 验证 Model1 终态后恢复编辑
        QVERIFY(!manager1.currentModelBusy());
        QVERIFY(params1->isEnabled());
        QVERIFY(inference1->setValueForName(QStringLiteral("batch_size"), old_batch1 + 10));
        QCOMPARE(inference1->valueForName(QStringLiteral("batch_size")).toInt(), old_batch1 + 10);
        task_manager->clearTasks();
    }

    void concurrentEvaluationsAndAggregationsConvergeSafelyOnShutdown()
    {
        // 参考 test_ModelTestTaskManager.cpp:244 (shutdownWithMultipleControlledEvaluationsCancelsAllExecutorsBeforeWaiting)
        // 本测试进一步验证：在两个真实评估与一个非空聚合执行者全部进入并发执行后，生产级两阶段关闭
        // 保证执行者全部收到协作取消并优雅收敛，且无任何迟到回调污染已关闭的 ViewModel。
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 cat = fixture.addClass(QStringLiteral("Cat"), QStringLiteral("normal"));
        const qint64 dog = fixture.addClass(QStringLiteral("Dog"), QStringLiteral("normal"));
        for (int i = 0; i < 6; ++i)
        {
            const qint64 img = fixture.addImage(QStringLiteral("sample_%1").arg(i));
            fixture.addDetectionLabel(img, (i % 2 == 0) ? cat : dog, 10, 10, 30, 30);
            const QVariant pred = QVariantList{QVariantMap{
                {QStringLiteral("class_id"), (i % 2 == 0) ? cat : dog},
                {QStringLiteral("score"), 0.85},
                {QStringLiteral("x"), 10.0},
                {QStringLiteral("y"), 10.0},
                {QStringLiteral("width"), 30.0},
                {QStringLiteral("height"), 30.0}
            }};
            fixture.writePrediction(img, pred);
        }
        QVERIFY(fixture.writeImageList());
        QVERIFY(fixture.setTestSelection({cat, dog}));

        QThreadPool shared_pool;
        shared_pool.setMaxThreadCount(4);

        std::unique_ptr<ModelEvaluationViewModel> vm1(EvaluationViewModelRegistry::instance().createViewModel(
            evaluation::Method::Detection, nullptr, &shared_pool));
        std::unique_ptr<ModelEvaluationViewModel> vm2(EvaluationViewModelRegistry::instance().createViewModel(
            evaluation::Method::Detection, nullptr, &shared_pool));
        QVERIFY(vm1 != nullptr && vm2 != nullptr);

        ModelEvaluationOptions options1;
        options1.method                 = evaluation::Method::Detection;
        options1.model_uuid             = QStringLiteral("model-eval-1");
        options1.test_task_uuid         = QStringLiteral("task-eval-1");
        options1.project_database_path  = fixture.projectDatabasePath();
        options1.dataset_file_list_path = fixture.fileListPath();
        options1.task_database_path     = fixture.taskDatabasePath();
        options1.prediction_dir         = fixture.predictionDirectory();
        options1.confidence_threshold   = 0.5;
        options1.iou_threshold          = 0.5;

        ModelEvaluationOptions options2 = options1;
        options2.model_uuid             = QStringLiteral("model-eval-2");
        options2.test_task_uuid         = QStringLiteral("task-eval-2");

        vm1->setEvaluationOptions(options1);
        vm2->setEvaluationOptions(options2);

        // 注册受控执行屏障引擎，验证工作线程在取消前确实已进入计算
        ControlledShutdownEvaluationEngine::reset();
        EvaluationEngineRegistry::instance().registerEngine(
            evaluation::Method::Detection, []() { return std::make_unique<ControlledShutdownEvaluationEngine>(); });
        RestoreDetectionEvaluationEngine restore_engine;

        // 构造包含多条真实样本数据的非空聚合输入，绝不使用空输入欺骗测试
        EvaluationAggregateInput agg_input;
        agg_input.anomaly_detection = false;
        agg_input.has_instance_metrics = true;
        agg_input.has_image_metrics = true;
        agg_input.has_confusion_matrix = true;
        agg_input.class_catalog[cat] = QStringLiteral("Cat");
        agg_input.class_catalog[dog] = QStringLiteral("Dog");
        for (int i = 0; i < 30; ++i)
        {
            EvaluationAggregateInput::InstanceEvent event;
            event.status        = (i % 2 == 0) ? evaluation::Status::TruePositive : evaluation::Status::FalsePositive;
            event.gt_class_id   = (i % 2 == 0) ? static_cast<int>(cat) : -1;
            event.gt_class      = (i % 2 == 0) ? QStringLiteral("Cat") : QString();
            event.pred_class_id = (i % 2 == 0) ? static_cast<int>(cat) : static_cast<int>(dog);
            event.pred_class    = (i % 2 == 0) ? QStringLiteral("Cat") : QStringLiteral("Dog");
            agg_input.instances.push_back(event);
        }

        auto agg_cancel_token = std::make_shared<std::atomic_bool>(false);
        std::atomic_bool agg_entered{false};
        std::atomic_bool agg_completed{false};
        std::atomic_bool late_callback_delivered{false};
        EvaluationAggregateOutput agg_output;

        // 启动后台受控非空聚合执行者
        shared_pool.start([&agg_input, agg_cancel_token, &agg_entered, &agg_completed, &late_callback_delivered, &agg_output, guard = QPointer<ModelEvaluationViewModel>(vm1.get())]() mutable {
            agg_entered.store(true, std::memory_order_release);
            while (!agg_cancel_token->load(std::memory_order_relaxed))
            {
                QThread::msleep(1);
            }
            agg_output = aggregateEvaluation(agg_input, agg_cancel_token);
            agg_completed.store(true, std::memory_order_release);

            // 模拟迟到回调投递至主线程
            QMetaObject::invokeMethod(guard.data(), [&late_callback_delivered, guard]() {
                if (guard != nullptr && !guard->isShuttingDown())
                {
                    late_callback_delivered.store(true, std::memory_order_release);
                }
            });
        });

        // 启动两个并发评估
        vm1->evaluate(true);
        vm2->evaluate(true);

        // 确保两个评估工作线程和一个聚合工作线程均已真实进入执行状态
        QTRY_VERIFY_WITH_TIMEOUT(ControlledShutdownEvaluationEngine::entered_count.load(std::memory_order_acquire) == 2, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(agg_entered.load(std::memory_order_acquire), 5000);
        QCOMPARE(ControlledShutdownEvaluationEngine::active_count.load(std::memory_order_acquire), 2);

        QSignalSpy vm1_completed_spy(vm1.get(), &ModelEvaluationViewModel::evaluationCompleted);
        QSignalSpy vm2_completed_spy(vm2.get(), &ModelEvaluationViewModel::evaluationCompleted);

        // 在评估与聚合高并发运行期间，执行生产级两阶段关闭
        QElapsedTimer shutdown_timer;
        shutdown_timer.start();

        // 第一阶段：非阻塞向全部组件（评估VM与聚合）发送取消请求
        vm1->beginShutdown();
        vm2->beginShutdown();
        agg_cancel_token->store(true, std::memory_order_release);

        // 第二阶段：生产关闭入口，并在共享线程池等待所有执行者收敛
        vm1->shutdown();
        vm2->shutdown();
        shared_pool.waitForDone();

        // 冲刷事件队列以检验是否有迟到回调发布
        QCoreApplication::processEvents();

        // 验证收敛耗时在 3 秒内完成（协作取消立即生效，没有挂起或死锁）
        QVERIFY2(shutdown_timer.elapsed() < 3000,
                 qPrintable(QString("评估与聚合收敛超时: %1 ms").arg(shutdown_timer.elapsed())));

        // 验证全部执行者观察到了取消信号并正常退出
        QCOMPARE(ControlledShutdownEvaluationEngine::cancel_observed_count.load(std::memory_order_acquire), 2);
        QCOMPARE(ControlledShutdownEvaluationEngine::active_count.load(std::memory_order_acquire), 0);
        QVERIFY(agg_completed.load(std::memory_order_acquire));

        // 验证非空聚合在取消时放弃并返回空结果
        QVERIFY(agg_output.instance_metrics.empty());
        QVERIFY(agg_output.image_metrics.empty());

        // 验证零迟到回调：已关闭的 VM 拒绝了迟到结果，没有激发完成信号
        QCOMPARE(vm1_completed_spy.count(), 0);
        QCOMPARE(vm2_completed_spy.count(), 0);
        QVERIFY(!late_callback_delivered.load(std::memory_order_acquire));
        QVERIFY(!vm1->available());
        QVERIFY(!vm2->available());
        QVERIFY(!vm1->loading());
        QVERIFY(!vm2->loading());
        QVERIFY(vm1->isShuttingDown());
        QVERIFY(vm2->isShuttingDown());
    }
};

REGISTER_TEST(ModelEvaluationParameterBehaviorTest)

#include "test_ModelEvaluationParameterBehavior.moc"
