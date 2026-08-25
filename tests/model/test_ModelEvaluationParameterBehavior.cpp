#include "../test_runner.h"

#include "TestFixture.h"

#include "database/DataBase.h"
#include "database/ModelTaskDataBase.h"
#include "model/IParams.h"
#include "model/ModelManager.h"
#include "model/ModelStorageService.h"
#include "model/ModelTestTaskManager.h"
#include "model/TaskManager.h"

#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTest>

using namespace dltool::model;
using namespace dltool::model::testsupport;

namespace {

ParamGroupModel *findGroup(ITestParams *params, const QString &name)
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
    return !write_prediction || task_database.upsertPrediction({image_id, prediction}, error);
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

        TaskManager *task_manager = TaskManager::getInstance();
        task_manager->clearTasks();
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
        evaluation->evaluate(false);
        QTRY_COMPARE_WITH_TIMEOUT(evaluation->stateKind(), ModelEvaluationViewModel::Ready, 5000);
        QVERIFY(evaluation->available());

        auto *inference = findGroup(manager.currentTestParams(), QStringLiteral("inference"));
        auto *evaluation_params = findGroup(manager.currentTestParams(), QStringLiteral("evaluation"));
        QVERIFY(inference != nullptr);
        QVERIFY(evaluation_params != nullptr);
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
        QCOMPARE(evaluation->confidenceThreshold(), 0.5);
        QCOMPARE(evaluation->iouThreshold(), 0.5);
        QCOMPARE(evaluation_changed.count(), 0);
        QCOMPARE(loading_changed.count(), 0);

        QVERIFY(evaluation_params->setValueForName(QStringLiteral("conf"), 0.6));
        QTRY_VERIFY_WITH_TIMEOUT(loading_changed.count() >= 2, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(evaluation->stateKind(), ModelEvaluationViewModel::Ready, 5000);
        QVERIFY(evaluation->available());
        QCOMPARE(evaluation->confidenceThreshold(), 0.6);

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

        TaskManager *task_manager = TaskManager::getInstance();
        task_manager->clearTasks();
        ModelTestTaskManager manager(fixture.rootPath(), &model_manager, nullptr, task_manager);
        manager.setModelUuid(record.uuid);
        QVERIFY2(prepareEvaluationInputs(fixture, manager, record, normal, anomalyPrediction(0.2), true, &error),
                 qPrintable(error));
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
        QCOMPARE(evaluation->confidenceThreshold(), 0.5);

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

        TaskManager *task_manager = TaskManager::getInstance();
        task_manager->clearTasks();
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

        TaskManager *task_manager = TaskManager::getInstance();
        task_manager->clearTasks();
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
                                                  manager.currentTaskUuid(), manager.currentTaskName(), true);
        QVERIFY(task_id > 0);
        QVERIFY(!manager.currentModelBusy());
        QVERIFY(task_manager->markTaskRunning(task_id));
        QVERIFY(manager.currentModelBusy());
        QVERIFY(task_manager->finishTask(task_id));
        QTRY_VERIFY_WITH_TIMEOUT(loading_changed.count() >= 2, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(evaluation->stateKind(), ModelEvaluationViewModel::Ready, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.currentModelBusy(), 5000);

        const int failed_id = task_manager->addTask(record.uuid, record.name, ModelTaskType::Train, true);
        QVERIFY(failed_id > 0);
        QVERIFY(task_manager->markTaskRunning(failed_id));
        QVERIFY(manager.currentModelBusy());
        QVERIFY(task_manager->failTask(failed_id));
        QVERIFY(!manager.currentModelBusy());

        const int stopped_id = task_manager->addTask(record.uuid, record.name, ModelTaskType::Train, true);
        QVERIFY(stopped_id > 0);
        QVERIFY(task_manager->startTask(stopped_id));
        QVERIFY(manager.currentModelBusy());
        QVERIFY(task_manager->stopTask(stopped_id));
        QVERIFY(manager.currentModelBusy());
        QVERIFY(task_manager->markTaskStopped(stopped_id));
        QVERIFY(!manager.currentModelBusy());
        task_manager->clearTasks();
    }
};

REGISTER_TEST(ModelEvaluationParameterBehaviorTest)

#include "test_ModelEvaluationParameterBehavior.moc"
