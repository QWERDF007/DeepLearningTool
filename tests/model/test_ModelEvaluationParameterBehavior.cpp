#include "../test_runner.h"

#include "TestFixture.h"

#include "database/DataBase.h"
#include "database/ModelTaskDataBase.h"
#include "model/AnomalyPreprocessingTransform.h"
#include "model/EvaluationViewModelRegistry.h"
#include "model/IParams.h"
#include "model/ModelManager.h"
#include "model/ModelStorageService.h"
#include "model/ModelTestTaskManager.h"
#include "model/TaskManager.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

#include <opencv2/opencv.hpp>

#include <cmath>

using namespace dltool::model;
using namespace dltool::model::testsupport;

namespace {

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
        const qint64 image = fixture.addImage(QStringLiteral("cat_sample"));
        QVERIFY(fixture.addDetectionLabel(image, cat, 0, 0, 10, 10) >= 0);
        QVERIFY(fixture.writeImageList());

        dltool::database::ProjectDataBase database(fixture.projectDatabasePath());
        ModelManager model_manager(static_cast<int>(evaluation::Method::Detection), &database, nullptr);
        QString      error;
        const auto record = model_manager.addModelRecord(QStringLiteral("PerfModel"), QStringLiteral("ultralytics"),
                                                         QStringLiteral("YOLOv8"), &error);
        QVERIFY2(record.isValid(), qPrintable(error));

        ModelTestTaskManager manager(fixture.rootPath(), &model_manager, nullptr, nullptr);
        manager.setModelUuid(record.uuid);
        QVERIFY2(prepareEvaluationInputs(fixture, manager, record, image,
                                         detectionPrediction(static_cast<int>(cat), QStringLiteral("Cat"), 0.9, 0, 0, 10, 10),
                                         true, &error),
                 qPrintable(error));

        ModelEvaluationOptions options;
        QVERIFY(manager.buildEvaluationOptions(options));

        auto *evaluation = manager.currentEvaluation();
        QVERIFY(evaluation != nullptr);

        // 1. 冷打开：执行后台评估并记录耗时与执行次数
        QCOMPARE(evaluation->evaluationCount(), 0);
        evaluation->evaluate(false);
        QTRY_COMPARE_WITH_TIMEOUT(evaluation->stateKind(), ModelEvaluationViewModel::Ready, 5000);
        QCOMPARE(evaluation->evaluationCount(), 1);
        QVERIFY(evaluation->lastEvaluationElapsedMs() >= 0);

        const double cold_conf = evaluation->confidenceThreshold();
        const double cold_best = evaluation->hasBestThreshold() ? evaluation->bestThreshold() : 0.0;
        const auto *matrix = evaluation->confusionMatrix();
        QVERIFY(matrix != nullptr);
        const int cold_cols = matrix->columnCount();

        // 2. 热打开：相同输入下重复 setEvaluationOptions，GUI 立即响应（无需重新触发后台执行），数值完全一致
        QElapsedTimer hot_timer;
        hot_timer.start();
        ModelEvaluationOptions hot_options;
        QVERIFY(manager.buildEvaluationOptions(hot_options));
        evaluation->setEvaluationOptions(hot_options);
        const qint64 hot_gui_elapsed = hot_timer.elapsed();
        QVERIFY(hot_gui_elapsed < 100);
        QCOMPARE(evaluation->evaluationCount(), 1);
        QCOMPARE(evaluation->stateKind(), ModelEvaluationViewModel::Ready);
        QCOMPARE(evaluation->confidenceThreshold(), cold_conf);
        QCOMPARE(evaluation->hasBestThreshold() ? evaluation->bestThreshold() : 0.0, cold_best);
        QCOMPARE(matrix->columnCount(), cold_cols);

        // 3. 显式重新评估：计数递增，结果数值依然一致
        evaluation->refreshEvaluation();
        QTRY_COMPARE_WITH_TIMEOUT(evaluation->stateKind(), ModelEvaluationViewModel::Ready, 5000);
        QCOMPARE(evaluation->evaluationCount(), 2);
        QCOMPARE(evaluation->confidenceThreshold(), cold_conf);
        QCOMPARE(evaluation->hasBestThreshold() ? evaluation->bestThreshold() : 0.0, cold_best);
        QCOMPARE(matrix->columnCount(), cold_cols);
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
};

REGISTER_TEST(ModelEvaluationParameterBehaviorTest)

#include "test_ModelEvaluationParameterBehavior.moc"
