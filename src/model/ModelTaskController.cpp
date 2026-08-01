#include "model/ModelTaskController.h"

#include "common/Utils.h"
#include "common/YamlUtils.h"
#include "core/CoreDef.h"
#include "data/DataManager.h"
#include "data/DatasetExportSource.h"
#include "data/DataOperationWorkflow.h"
#include "model/ExternalModelTaskRunner.h"
#include "model/IModel.h"
#include "model/IModelConfig.h"
#include "model/IParams.h"
#include "model/ModelDatasetSelection.h"
#include "model/ModelManager.h"
#include "model/ModelStorageService.h"
#include "model/TaskManager.h"
#include "ui/SignalHelper.h"

#include <spdlog/spdlog.h>

#include <memory>
#include <exception>
#include <QCryptographicHash>
#include <QDateTime>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMetaObject>
#include <QThreadPool>
#include <QPointer>
#include <QThreadPool>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <set>
#include <utility>

namespace dltool::model {
using common::setError;
using common::cleanPath;

namespace {

QString taskManagerStatusName(const TaskManager::TaskStatus status)
{
    switch (status)
    {
    case TaskManager::Pending:
        return QStringLiteral("pending");
    case TaskManager::Preparing:
        return QStringLiteral("preparing");
    case TaskManager::Running:
        return QStringLiteral("running");
    case TaskManager::Paused:
        return QStringLiteral("paused");
    case TaskManager::Stopping:
        return QStringLiteral("stopping");
    case TaskManager::Stopped:
        return QStringLiteral("stopped");
    case TaskManager::Finished:
        return QStringLiteral("finished");
    case TaskManager::Failed:
        return QStringLiteral("failed");
    default:
        return {};
    }
}

QString fileDigest(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd())
    {
        const QByteArray chunk = file.read(1024 * 1024);
        if (chunk.isEmpty() && !file.atEnd())
            return {};
        hash.addData(chunk);
    }
    return QString("sha256:%1").arg(QString::fromLatin1(hash.result().toHex()));
}

QString checkpointPath(const ModelTaskRequest &request, const QString &project_dir)
{
    const QVariantMap inference = request.model_config.test_params.value(QString("inference")).toMap();
    const QString raw = inference.value(QString("checkpoint_path")).toString().trimmed();
    if (raw.isEmpty())
        return {};
    const QFileInfo info(raw);
    if (info.isAbsolute())
        return cleanPath(info.absoluteFilePath());
    const ModelStorageService storage(project_dir);
    const QString weights = storage.trainWeightsPath(request.model_config.model_name);
    const QString weight_candidate = cleanPath(QDir(weights).filePath(raw));
    if (QFileInfo::exists(weight_candidate))
        return weight_candidate;
    const QString model_root = storage.path(request.model_config.model_name, ModelStorageLocation::ModelRoot);
    return cleanPath(QDir(model_root).filePath(raw));
}

QVariantMap fileSignature(const QString &path)
{
    const QFileInfo info(path);
    const bool valid = info.exists() && info.isFile();
    return {{QString("path"), path.isEmpty() ? QString() : cleanPath(info.absoluteFilePath())},
            {QString("exists"), valid},
            {QString("size"), valid ? info.size() : qint64(-1)},
            {QString("mtime"), valid ? info.lastModified().toMSecsSinceEpoch() : qint64(-1)},
            {QString("content_hash"), valid ? fileDigest(path) : QString()}};
}

bool testSelectionContainsImage(const ModelDatasetSelection &selection, const qint64 image_id,
                                const dltool::data::DataManager *data_manager)
{
    if (data_manager == nullptr)
        return false;
    const qint64 dataset_id = data_manager->imageDatasetId(image_id);
    if (selection.containsDataset(dataset_id))
        return true;

    const QVariantMap image_level = data_manager->getImageLevelLabelData(image_id);
    const qint64 image_level_class = image_level.value(QStringLiteral("label_class_id"),
                                                       image_level.value(QStringLiteral("class_id"), -1)).toLongLong();
    if (selection.contains(dataset_id, image_level_class))
        return true;

    for (const qint64 label_id : data_manager->imageLabelIds(image_id))
    {
        if (selection.contains(dataset_id, data_manager->labelClassId(label_id)))
            return true;
    }
    return false;
}

QString inputDataDigest(const ModelTaskRequest &request, const dltool::data::DataManager *data_manager)
{
    if (data_manager == nullptr || !isTestModelTask(request.task_type))
        return {};

    const ModelDatasetSelection &selection = request.selections.test;
    std::set<qint64> dataset_set = selection.dataset_ids;
    for (const auto &[dataset_id, label_class_id] : selection.label_classes)
    {
        Q_UNUSED(label_class_id)
        dataset_set.insert(dataset_id);
    }
    if (dataset_set.empty())
        return {};

    const std::vector<int64_t> dataset_ids(dataset_set.cbegin(), dataset_set.cend());
    std::vector<int64_t> image_ids = data_manager->imageIdsForDatasets(dataset_ids);
    std::sort(image_ids.begin(), image_ids.end());

    QVariantList signatures;
    signatures.reserve(static_cast<int>(image_ids.size()));
    for (const qint64 image_id : image_ids)
    {
        if (!testSelectionContainsImage(selection, image_id, data_manager))
            continue;
        const QString path = cleanPath(data_manager->imagePath(image_id));
        const QFileInfo info(path);
        const bool valid = info.exists() && info.isFile();
        signatures.push_back(QVariantMap{{QStringLiteral("image_id"), image_id},
                                         {QStringLiteral("dataset_id"), data_manager->imageDatasetId(image_id)},
                                         {QStringLiteral("path"), path},
                                         {QStringLiteral("size"), valid ? info.size() : qint64(-1)},
                                         {QStringLiteral("mtime"), valid ? info.lastModified().toMSecsSinceEpoch()
                                                                           : qint64(-1)},
                                         {QStringLiteral("exists"), valid}});
    }
    if (signatures.isEmpty())
        return {};

    YAML::Emitter emitter;
    emitter << common::yaml::variantToYaml(signatures);
    return QString("sha256:%1").arg(QString::fromLatin1(
        QCryptographicHash::hash(QByteArray(emitter.c_str()), QCryptographicHash::Sha256).toHex()));
}

QString inferenceDigest(const ModelTaskRequest &request, const QString &project_dir,
                        const QString &input_data_digest)
{
    const auto selectionMap = [](const ModelDatasetSelection &selection)
    {
        QVariantList dataset_ids;
        for (const qint64 id : selection.dataset_ids)
            dataset_ids.push_back(id);
        QVariantList label_classes;
        for (const auto &entry : selection.label_classes)
            label_classes.push_back(QVariantMap{{QStringLiteral("dataset_id"), entry.first},
                                                {QStringLiteral("label_class_id"), entry.second}});
        return QVariantMap{{QStringLiteral("dataset_ids"), dataset_ids},
                           {QStringLiteral("label_classes"), label_classes}};
    };
    QVariantMap inference_params = request.model_config.test_params;
    // Evaluation thresholds/matching strategy affect only C++ diagnostics;
    // changing them must not invalidate the normalized PRED.
    inference_params.remove(QStringLiteral("evaluation"));
    const QVariantMap value = {
        {QStringLiteral("model_uuid"), request.model_config.model_uuid},
        {QStringLiteral("framework"), request.model_config.framework_name},
        {QStringLiteral("method"), request.evaluation_method},
        {QStringLiteral("architecture"), request.model_config.model_architecture},
        {QStringLiteral("task_type"), modelTaskKey(request.task_type)},
        {QStringLiteral("dataset_selection"), QVariantMap{{QStringLiteral("train"), selectionMap(request.selections.train)},
                                                           {QStringLiteral("validation"), selectionMap(request.selections.validation)},
                                                           {QStringLiteral("test"), selectionMap(request.selections.test)}}},
        {QStringLiteral("test_params"), inference_params},
        {QStringLiteral("scope_uuid"), request.scope_uuid},
        {QStringLiteral("input_data_digest"), input_data_digest},
        {QStringLiteral("checkpoint"), fileSignature(checkpointPath(request, project_dir))},
    };
    YAML::Emitter emitter;
    emitter << common::yaml::variantToYaml(value);
    return QStringLiteral("sha256:%1").arg(QString::fromLatin1(
        QCryptographicHash::hash(QByteArray(emitter.c_str()), QCryptographicHash::Sha256).toHex()));
}

QString evaluationMethodForProject(const int method)
{
    using Method = dltool::core::DeepLearningMethod;
    switch (method)
    {
    case Method::Detection: return QStringLiteral("object_detection");
    case Method::Segmentation: return QStringLiteral("segmentation");
    case Method::AnomalyDetection: return QStringLiteral("anomaly_detection");
    case Method::Classification: return QStringLiteral("classification");
    default: return QStringLiteral("unknown");
    }
}

bool isFewShotFramework(const QString &framework_name)
{
    return framework_name.compare(QString("FS-SAM2"), Qt::CaseInsensitive) == 0;
}

} // namespace

ModelTaskController::ModelTaskController(const int method, QString project_dir, ModelManager *model_manager,
                                         dltool::data::DataManager *data_manager, TaskManager *task_manager,
                                         QObject *parent)
    : QObject(parent)
    , method_(method)
    , project_dir_(std::move(project_dir))
    , model_manager_(model_manager)
    , data_manager_(data_manager)
    , task_manager_(task_manager)
    , external_task_runner_(std::make_unique<ExternalModelTaskRunner>(this))
    , test_task_repository_(project_dir_)
{
    connect(external_task_runner_.get(), &ExternalModelTaskRunner::taskStarted, this,
            &ModelTaskController::handleExternalTaskStarted);
    connect(external_task_runner_.get(), &ExternalModelTaskRunner::taskFinished, this,
            &ModelTaskController::handleExternalTaskFinished);
    connect(external_task_runner_.get(), &ExternalModelTaskRunner::taskStartFailed, this,
            &ModelTaskController::handleExternalTaskStartFailed);
    if (task_manager_ != nullptr)
    {
        connect(task_manager_, &TaskManager::taskStartRequested, this,
                &ModelTaskController::handleTaskStartRequested);
        connect(task_manager_, &TaskManager::taskStopRequested, this, &ModelTaskController::handleTaskStopRequested);
        connect(task_manager_, &TaskManager::taskMessageReceived, this, &ModelTaskController::handleTaskMessage);
    }
}

ModelTaskController::~ModelTaskController()
{
    shutdown();
}

void ModelTaskController::shutdown()
{
    if (task_manager_ != nullptr)
    {
        const int count = task_manager_->rowCount();
        for (int row = 0; row < count; ++row)
        {
            const QModelIndex index = task_manager_->index(row, 0);
            const int task_id = task_manager_->data(index, TaskManager::TaskIdRole).toInt();
            const TaskManager::Task *task = task_manager_->findTask(task_id);
            if (task == nullptr || model_manager_ == nullptr
                || !model_manager_->modelRecordViewForUuid(task->model_uuid).isValid()
                || TaskManager::isTerminal(task->status))
                continue;
            const auto token = evaluation_cancel_tokens_.value(task_id);
            if (token != nullptr)
                token->store(true, std::memory_order_relaxed);
            if (external_task_runner_ != nullptr && external_task_runner_->hasRunningTask(task_id))
                external_task_runner_->stop(task_id);
            else
                task_manager_->markTaskStopped(task_id);
        }
    }
    // Evaluation callbacks carry a QPointer guard, but wait here so project
    // teardown never leaves a worker writing after the project has gone away.
    QThreadPool::globalInstance()->waitForDone(5000);
    if (external_task_runner_ != nullptr)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 5000)
        {
            bool running = false;
            if (task_manager_ != nullptr)
            {
                for (int row = 0; row < task_manager_->rowCount(); ++row)
                {
                    const int task_id = task_manager_->data(task_manager_->index(row, 0), TaskManager::TaskIdRole).toInt();
                    if (external_task_runner_->hasRunningTask(task_id))
                    {
                        running = true;
                        break;
                    }
                }
            }
            if (!running)
                break;
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        }
    }
}

int ModelTaskController::addModelTask(const QString &model_uuid, const ModelTaskType task_type)
{
    QString error;
    const int task_id = ensureTaskRecord(model_uuid, task_type, {}, {}, &error);
    if (task_id < 0)
        spdlog::error("添加模型任务失败: {}", error.toUtf8().constData());
    return task_id;
}

int ModelTaskController::startModelTask(const QString &model_uuid, const ModelTaskType task_type)
{
    QString error;
    const int task_id = ensureTaskRecord(model_uuid, task_type, {}, {}, &error);
    if (task_id < 0)
    {
        spdlog::error("启动模型任务失败: {}", error.toUtf8().constData());
        return -1;
    }
    return task_manager_->startTask(task_id) ? task_id : -1;
}

int ModelTaskController::startModelTestTask(const QString &model_uuid, const QString &test_task_uuid)
{
    QString error;
    const int task_id = ensureTaskRecord(model_uuid, ModelTaskType::Test, test_task_uuid, {}, &error);
    if (task_id < 0)
    {
        spdlog::error("启动测试任务失败: {}", error.toUtf8().constData());
        return -1;
    }
    return task_manager_ != nullptr && task_manager_->startTask(task_id) ? task_id : -1;
}

bool ModelTaskController::stopModelTask(const QString &model_uuid, const ModelTaskType task_type)
{
    if (task_manager_ == nullptr)
        return false;

    const int task_id = task_manager_->findModelTask(model_uuid.trimmed(), task_type, false);
    return task_id >= 0 && stopTask(task_id);
}

bool ModelTaskController::stopModelTestTask(const QString &model_uuid, const QString &test_task_uuid)
{
    if (task_manager_ == nullptr)
        return false;
    const int task_id = task_manager_->findModelTask(model_uuid.trimmed(), ModelTaskType::Test,
                                                     test_task_uuid.trimmed(), false);
    return task_id >= 0 && stopTask(task_id);
}

bool ModelTaskController::deleteModelTask(const QString &model_uuid, const ModelTaskType task_type)
{
    if (task_manager_ == nullptr)
        return false;

    const int task_id = task_manager_->findModelTask(model_uuid.trimmed(), task_type, false);
    return task_id >= 0 && deleteTask(task_id);
}

int ModelTaskController::ensureTaskRecord(const QString &model_uuid, const ModelTaskType task_type,
                                          const QString &scope_uuid, const QString &scope_name, QString *err_msg)
{
    if (model_manager_ == nullptr)
    {
        setError(err_msg, QString("模型管理器为空"));
        return -1;
    }
    if (task_manager_ == nullptr)
    {
        setError(err_msg, QString("任务管理器为空"));
        return -1;
    }

    const QString uuid = model_uuid.trimmed();
    if (uuid.isEmpty())
    {
        setError(err_msg, QString("模型 uuid 为空"));
        return -1;
    }
    if (!isKnownModelTask(task_type))
    {
        setError(err_msg, QString("任务类型无效"));
        return -1;
    }

    const ModelManager::ModelRecordView record = model_manager_->modelRecordViewForUuid(uuid);
    if (!record.isValid() || record.name.trimmed().isEmpty())
    {
        setError(err_msg, QString("模型不存在: %1").arg(uuid));
        return -1;
    }

    const FrameworkDefinition framework = registeredFramework(method_, record.framework_name);
    if (framework.name.isEmpty())
    {
        setError(err_msg, QString("框架未注册: %1").arg(record.framework_name));
        return -1;
    }

    QString resolved_scope = scope_uuid.trimmed();
    QString resolved_scope_name = scope_name.trimmed();
    if (isTrainModelTask(task_type))
        resolved_scope = QStringLiteral("train");
    if (isTestModelTask(task_type) && resolved_scope.isEmpty() && !isFewShotFramework(record.framework_name))
    {
        setError(err_msg, QString("普通测试任务必须绑定测试任务 UUID"));
        return -1;
    }
    ModelTestTaskDefinition resolved_definition;
    bool has_resolved_definition = false;
    if (isTestModelTask(task_type) && !resolved_scope.isEmpty())
    {
        if (!test_task_repository_.loadTask(record.name, resolved_scope, resolved_definition, err_msg))
            return -1;
        resolved_scope_name = resolved_definition.name;
        has_resolved_definition = true;
    }

    int task_id = task_manager_->findModelTask(uuid, task_type, resolved_scope, false);
    if (task_id < 0)
    {
        task_id = task_manager_->addTask(uuid, record.name, task_type, resolved_scope, resolved_scope_name,
                                         !framework.supportsExternalTask(task_type));
    }
    if (task_id < 0)
        return -1;

    const ModelStorageService storage(project_dir_);
    QString config_path;
    QString log_path;
    if (isTrainModelTask(task_type))
    {
        config_path = storage.trainConfigPath(record.name);
        log_path = storage.trainLogPath(record.name);
    }
    else if (has_resolved_definition)
    {
        config_path = storage.testTaskConfigPath(record.name, resolved_definition.directory_name);
        log_path = storage.testTaskLogPath(record.name, resolved_definition.uuid);
    }
    task_manager_->setTaskPaths(task_id, config_path, log_path);
    return task_id;
}

bool ModelTaskController::prepareTask(const int task_id)
{
    if (task_manager_ == nullptr || model_manager_ == nullptr)
        return false;

    const TaskManager::Task *task = task_manager_->findTask(task_id);
    if (task == nullptr || task->status != TaskManager::Preparing)
        return false;

    const ModelManager::ModelRecordView record = model_manager_->modelRecordViewForUuid(task->model_uuid);
    const FrameworkDefinition framework = registeredFramework(method_, record.framework_name);
    if (record.name.isEmpty() || framework.name.isEmpty())
    {
        failTask(task_id, QString("模型或框架不存在"));
        return false;
    }

    if (!framework.supportsExternalTask(task->type))
    {
        if (!task_manager_->markTaskRunning(task_id))
        {
            failTask(task_id, QString("内部模型任务无法进入运行状态"));
            return false;
        }
        syncTaskModelState(task_id);
        touchTaskModelModifiedTime(task_id);
        return true;
    }

    QString server_error;
    if (!task_manager_->ensureTaskServer(&server_error))
    {
        failTask(task_id, QString("任务通信服务启动失败: %1").arg(server_error));
        return false;
    }

    ModelTaskRequest request;
    QString          request_error;
    if (!buildTaskRequest(task_id, request, &request_error))
    {
        failTask(task_id, request_error);
        return false;
    }

    // A change limited to evaluation parameters must not start Python again.
    // The current normalized PRED is already validated by buildTaskRequest;
    // evaluate it directly and commit result.yaml before exposing Finished.
    if (isTestModelTask(request.task_type) && request.reuse_prediction)
    {
        if (!task_manager_->markTaskRunning(task_id))
        {
            failTask(task_id, QString("测试任务无法进入运行状态"));
            return false;
        }
        task_manager_->updateTaskPhase(task_id, QStringLiteral("evaluating"));
        task_manager_->updateTaskProgress(task_id, 90);
        syncTaskModelState(task_id);
        runTestEvaluationAsync(task_id);
        return true;
    }

    const auto process_spec = std::make_shared<ExternalProcessSpec>();

    dltool::data::DataOperationWorkflow::Options options;
    options.title            = QString("准备模型任务");
    options.start_message    = QString("准备模型任务: %1").arg(modelTaskDisplayName(request.task_type));
    options.initial_progress = 5;

    const int method = method_;
    const QString project_dir = project_dir_;
    const auto prepare = [method, project_dir, request, process_spec](
                             const dltool::data::DatasetExportSource *dataset_source,
                             dltool::data::DataOperationWorkflow::Result &result)
    {
        QString error;
        if (!prepareModelTask(method, project_dir, request, dataset_source, *process_spec, &error))
        {
            result.error = error;
            return;
        }
        result.success = true;
    };
    const auto completion = [this, task_id, process_spec](const dltool::data::DataOperationWorkflow::Result &result)
    { handlePreparedTask(task_id, process_spec, result.success, result.error); };

    if (describeModelTask(request.task_type).requires_dataset_export)
    {
        if (data_manager_ == nullptr)
        {
            failTask(task_id, QString("数据管理器为空"));
            return false;
        }

        dltool::data::DatasetExportRequest export_request;
        export_request.dataset_ids = selectedDatasetIds(request.selections);
        data_manager_->runDatasetExportAsync(
            this, std::move(export_request), std::move(options),
            [prepare](const dltool::data::DatasetExportSource &source, dltool::data::DataOperationWorkflow::Result &result)
            { prepare(&source, result); },
            completion);
    }
    else
    {
        dltool::data::DataOperationWorkflow::start(
            this, std::move(options),
            [prepare](dltool::data::DataOperationWorkflow::Result &result) { prepare(nullptr, result); }, completion);
    }

    spdlog::info("模型任务进入后台准备, task_id: {}", task_id);
    return true;
}

bool ModelTaskController::stopTask(const int task_id)
{
    const auto token = evaluation_cancel_tokens_.value(task_id);
    if (token != nullptr)
        token->store(true, std::memory_order_relaxed);
    return task_manager_ != nullptr && task_manager_->stopTask(task_id);
}

bool ModelTaskController::deleteTask(const int task_id)
{
    const bool deleted = task_manager_ != nullptr && task_manager_->deleteTask(task_id);
    if (external_task_runner_ != nullptr)
        external_task_runner_->deleteTask(task_id);
    return deleted;
}

bool ModelTaskController::buildTaskRequest(const int task_id, ModelTaskRequest &request, QString *err_msg) const
{
    request = {};
    if (task_manager_ == nullptr || model_manager_ == nullptr)
        return setError(err_msg, QString("任务控制器未初始化"));

    const TaskManager::Task *task = task_manager_->findTask(task_id);
    if (task == nullptr)
        return setError(err_msg, QString("任务不存在"));

    const ModelManager::ModelRecordView record = model_manager_->modelRecordViewForUuid(task->model_uuid);
    if (!record.isValid())
        return setError(err_msg, QString("模型不存在: %1").arg(task->model_uuid));

    IModel *model = model_manager_->modelForUuid(task->model_uuid);
    if (model == nullptr)
        return setError(err_msg, QString("无法创建模型实例: %1").arg(task->model_uuid));

    const FrameworkDefinition framework = registeredFramework(method_, record.framework_name);
    if (framework.name.isEmpty())
        return setError(err_msg, QString("框架未注册: %1").arg(record.framework_name));

    request.task_id          = task->id;
    request.task_type        = task->type;
    request.scope_uuid       = task->scope_uuid;
    request.scope_name       = task->scope_name;
    request.evaluation_method = evaluationMethodForProject(method_);
    request.framework        = framework;
    request.task_server_host = task_manager_->taskServerHost();
    request.task_server_port = task_manager_->taskServerPort();
    request.selections       = modelDatasetSelections(model);
    request.model_config.model_uuid         = record.uuid;
    request.model_config.model_name         = record.name;
    request.model_config.framework_name     = record.framework_name;
    request.model_config.method              = request.evaluation_method;
    request.model_config.model_architecture = record.model_architecture;
    request.model_config.scope_uuid         = task->scope_uuid;
    request.model_config.scope_name         = task->scope_name;
    request.model_config.task_directory     = task->scope_name;

    if (const IModelConfig *config = model->config(); config != nullptr)
    {
        if (const ITrainParams *params = config->trainParams(); params != nullptr)
            request.model_config.train_params = params->valuesMap();
        if (const ITestParams *params = config->testParams(); params != nullptr)
            request.model_config.test_params = params->valuesMap();
    }

    if (isTestModelTask(task->type) && !task->scope_uuid.trimmed().isEmpty())
    {
        ModelTestTaskDefinition definition;
        QString error;
        if (!test_task_repository_.loadTask(record.name, task->scope_uuid, definition, &error))
            return setError(err_msg, error);
        request.selections = {};
        request.selections.test = definition.dataset_selection;
        request.model_config.test_params = definition.test_params;
        request.model_config.task_definition_test_params = definition.test_params;
        request.scope_name = definition.name;
        request.model_config.scope_name = definition.name;
        request.model_config.task_directory = definition.directory_name;
        request.model_config.test_dataset_selection = definition.dataset_selection;
        request.model_config.created_at = definition.created_at;
        request.model_config.modified_at = definition.modified_at;
    }
    if (isTestModelTask(task->type))
    {
        request.input_data_digest = inputDataDigest(request, data_manager_);
        request.inference_digest = inferenceDigest(request, project_dir_, request.input_data_digest);
        const ModelStorageService storage(project_dir_);
        const QString prediction_config =
            storage.testTaskPredictionConfigPath(record.name, request.model_config.task_directory);
        const QString prediction_images =
            storage.testTaskPredictionImagesPath(record.name, request.model_config.task_directory);
        const QString prediction_manifest =
            storage.testTaskPredictionManifestPath(record.name, request.model_config.task_directory);
        bool reusable = QFileInfo::exists(prediction_config) && QFileInfo::exists(prediction_images)
            && QFileInfo::exists(prediction_manifest);
        if (reusable)
        {
            try
            {
                const YAML::Node pred_root = common::yaml::loadFile(QFileInfo(prediction_config));
                reusable = pred_root && pred_root.IsMap()
                    && common::yaml::nodeString(pred_root["inference_digest"]) == request.inference_digest
                    && common::yaml::nodeString(pred_root["input_data_digest"]) == request.input_data_digest
                    && !request.input_data_digest.isEmpty();
            }
            catch (const std::exception &)
            {
                reusable = false;
            }
            if (reusable)
                reusable = ModelEvaluationService::validatePrediction(
                    prediction_images, prediction_manifest, nullptr, nullptr, nullptr,
                    request.model_config.model_uuid, request.scope_uuid, request.evaluation_method);
        }
        request.reuse_prediction = reusable;
    }
    return true;
}

void ModelTaskController::handlePreparedTask(const int task_id, const std::shared_ptr<ExternalProcessSpec> &process_spec,
                                             const bool success, const QString &error)
{
    if (task_manager_ == nullptr)
        return;

    const TaskManager::Task *task = task_manager_->findTask(task_id);
    // 停止或删除发生在后台准备期间时，任务已不再是 Preparing，完成回调只需丢弃。
    if (task == nullptr || task->status != TaskManager::Preparing)
        return;

    if (!success)
    {
        failTask(task_id, error.isEmpty() ? QString("准备模型任务失败") : error);
        return;
    }

    QString start_error;
    if (process_spec == nullptr || external_task_runner_ == nullptr
        || !external_task_runner_->start(*process_spec, &start_error))
    {
        failTask(task_id, start_error.isEmpty() ? QString("启动外部模型任务失败") : start_error);
    }
}

bool ModelTaskController::taskBelongsToCurrentModelManager(const int task_id) const
{
    if (task_manager_ == nullptr || model_manager_ == nullptr)
        return false;

    const TaskManager::Task *task = task_manager_->findTask(task_id);
    return task != nullptr && model_manager_->modelRecordViewForUuid(task->model_uuid).isValid();
}

void ModelTaskController::failTask(const int task_id, const QString &message) const
{
    if (task_manager_ == nullptr || !task_manager_->failTask(task_id))
        return;

    syncTaskModelState(task_id);

    if (!message.isEmpty())
    {
        spdlog::error("模型任务 {} 失败: {}", task_id, message.toUtf8().constData());
        ui::SignalHelper::notifyError(QString("模型任务 %1 失败").arg(task_id), message);
    }
}

void ModelTaskController::touchTaskModelModifiedTime(const int task_id) const
{
    if (task_manager_ == nullptr || model_manager_ == nullptr)
        return;

    const TaskManager::Task *task = task_manager_->findTask(task_id);
    if (task == nullptr)
        return;

    QString error;
    if (!model_manager_->touchModelModifiedTime(task->model_uuid, &error))
    {
        spdlog::error("更新任务对应模型修改时间失败, task_id: {}, uuid: {}, 错误: {}", task_id,
                      task->model_uuid.toUtf8().constData(), error.toUtf8().constData());
    }
}

void ModelTaskController::syncTaskModelState(const int task_id) const
{
    if (task_manager_ == nullptr || model_manager_ == nullptr)
        return;

    const TaskManager::Task *task = task_manager_->findTask(task_id);
    if (task == nullptr || (!isTrainModelTask(task->type) && !isTestModelTask(task->type)))
        return;

    const bool train_scope = isTrainModelTask(task->type);
    const bool legacy_few_shot_test = isTestModelTask(task->type) && task->scope_uuid.trimmed().isEmpty();
    const QString phase = train_scope ? QStringLiteral("train") : QStringLiteral("test_tasks");
    const QVariantMap current_model = model_manager_->modelRecordForUuid(task->model_uuid);
    const QVariantMap extra_data = current_model.value(QStringLiteral("extra_data")).toMap();
    QVariantMap test_tasks = extra_data.value(QStringLiteral("test_tasks")).toMap();
    QVariantMap section = train_scope ? extra_data.value(phase).toMap()
                                      : (legacy_few_shot_test ? extra_data.value(QStringLiteral("test")).toMap()
                                                              : test_tasks.value(task->scope_uuid).toMap());

    // TaskManager owns the overall task progress.  The model page previously
    // kept the last phase progress (for example, 90% after training), which
    // could differ from the 100% terminal value shown in TaskCenterWindow.
    section.insert(QStringLiteral("progress"), task->progress);

    if (TaskManager::isTerminal(task->status))
    {
        section.insert(QStringLiteral("started"), false);
        section.insert(QStringLiteral("status"), taskManagerStatusName(task->status));
    }
    else if (task->status == TaskManager::Running || task->status == TaskManager::Paused
             || task->status == TaskManager::Stopping)
    {
        section.insert(QStringLiteral("started"), true);
        section.insert(QStringLiteral("status"), taskManagerStatusName(task->status));
    }

    QString error;
    QVariantMap state_update;
    if (train_scope)
        state_update.insert(phase, section);
    else if (legacy_few_shot_test)
        state_update.insert(QStringLiteral("test"), section);
    else
    {
        test_tasks.insert(task->scope_uuid, section);
        state_update.insert(QStringLiteral("test_tasks"), test_tasks);
    }
    if (!model_manager_->updateModelExtraData(task->model_uuid, state_update, &error))
    {
        spdlog::error("同步模型任务状态失败, task_id: {}, uuid: {}, 错误: {}", task_id,
                      task->model_uuid.toUtf8().constData(), error.toUtf8().constData());
    }
}

void ModelTaskController::handleTaskStartRequested(const int task_id)
{
    if (taskBelongsToCurrentModelManager(task_id))
        prepareTask(task_id);
}

void ModelTaskController::handleTaskMessage(const TaskMessage &message)
{
    if (model_manager_ == nullptr || task_manager_ == nullptr || message.task_id < 0
        || message.type == TaskMessageType::Log || message.type == TaskMessageType::Command)
    {
        return;
    }

    const TaskManager::Task *task = task_manager_->findTask(message.task_id);
    if (task == nullptr || (!isTrainModelTask(task->type) && !isTestModelTask(task->type)))
        return;

    // The top-level model data is keyed by the software task type.  A train
    // runner may report validation/evaluation as phase "test", but that is
    // still part of the training task and must not update the separate Test
    // page's state.
    const bool train_scope = isTrainModelTask(task->type);
    const bool legacy_few_shot_test = isTestModelTask(task->type) && task->scope_uuid.trimmed().isEmpty();
    const QString phase = train_scope ? QStringLiteral("train") : QStringLiteral("test_tasks");

    QVariantMap updates;
    if (message.status == TaskProtocolStatus::Running || message.payload.contains(QStringLiteral("started")))
        updates.insert(QStringLiteral("started"), message.payload.value(QStringLiteral("started"), true).toBool());
    if (message.progress >= 0)
        updates.insert(QStringLiteral("progress"), message.progress);

    for (const QString &key : {QStringLiteral("epoch"), QStringLiteral("iter"), QStringLiteral("lr"),
                               QStringLiteral("loss"), QStringLiteral("elapsed"), QStringLiteral("eta")})
    {
        if (message.payload.contains(key))
            updates.insert(key, message.payload.value(key).toString());
    }
    if (message.payload.contains(QStringLiteral("metrics")))
        updates.insert(QStringLiteral("metrics"), message.payload.value(QStringLiteral("metrics")).toString());
    if (!message.message.isEmpty())
        updates.insert(QStringLiteral("message"), message.message);

    const QString status = taskProtocolStatusName(message.status);
    if (!status.isEmpty())
        updates.insert(QStringLiteral("status"), status);

    const bool terminal = message.status == TaskProtocolStatus::Stopped || message.status == TaskProtocolStatus::Finished
                       || message.status == TaskProtocolStatus::Failed || message.status == TaskProtocolStatus::Error;
    if (terminal)
    {
        // Some runners send the final status without a progress field (or
        // attach it to an internal evaluation phase), so explicitly close the
        // phase belonging to this software task here.
        updates.insert(QStringLiteral("started"), false);
        if (message.status == TaskProtocolStatus::Finished)
            updates.insert(QStringLiteral("progress"), 100);
        touchTaskModelModifiedTime(message.task_id);
    }

    const QVariantMap current_model = model_manager_->modelRecordForUuid(task->model_uuid);
    const QVariantMap extra_data = current_model.value(QStringLiteral("extra_data")).toMap();
    QVariantMap test_tasks = extra_data.value(QStringLiteral("test_tasks")).toMap();
    QVariantMap section = train_scope ? extra_data.value(phase).toMap()
                                      : (legacy_few_shot_test ? extra_data.value(QStringLiteral("test")).toMap()
                                                              : test_tasks.value(task->scope_uuid).toMap());
    for (auto it = updates.cbegin(); it != updates.cend(); ++it)
        section.insert(it.key(), it.value());

    // Keep the phase shown by ModelDelegate on the same overall progress as
    // TaskManager/TaskCenterWindow.  A training task can enter an internal
    // validation phase after its training progress reaches 90%, while the
    // task itself continues to 100%.
    const bool completed = message.status == TaskProtocolStatus::Finished;
    auto applyTaskState = [task, terminal, completed](QVariantMap &target) {
        target.insert(QStringLiteral("progress"), completed ? 100 : task->progress);
        if (terminal)
        {
            target.insert(QStringLiteral("started"), false);
            target.insert(QStringLiteral("status"), taskManagerStatusName(task->status));
        }
        else if (task->status == TaskManager::Running || task->status == TaskManager::Paused
                 || task->status == TaskManager::Stopping)
        {
            target.insert(QStringLiteral("started"), true);
            target.insert(QStringLiteral("status"), taskManagerStatusName(task->status));
        }
    };
    applyTaskState(section);

    QString error;
    QVariantMap state_update;
    if (train_scope)
        state_update.insert(phase, section);
    else if (legacy_few_shot_test)
        state_update.insert(QStringLiteral("test"), section);
    else
    {
        test_tasks.insert(task->scope_uuid, section);
        state_update.insert(QStringLiteral("test_tasks"), test_tasks);
    }
    if (!model_manager_->updateModelExtraData(task->model_uuid, state_update, &error))
    {
        spdlog::error("保存模型任务状态失败, task_id: {}, uuid: {}, 错误: {}", message.task_id,
                      task->model_uuid.toUtf8().constData(), error.toUtf8().constData());
    }
}

void ModelTaskController::handleTaskStopRequested(const int task_id)
{
    if (!taskBelongsToCurrentModelManager(task_id))
        return;

    if (external_task_runner_ != nullptr && external_task_runner_->hasRunningTask(task_id))
    {
        external_task_runner_->stop(task_id);
        return;
    }

    const auto evaluation_token = evaluation_cancel_tokens_.value(task_id);
    if (evaluation_token != nullptr)
        evaluation_token->store(true, std::memory_order_relaxed);

    if (task_manager_ != nullptr)
        task_manager_->markTaskStopped(task_id);
    syncTaskModelState(task_id);
    touchTaskModelModifiedTime(task_id);
}

void ModelTaskController::handleExternalTaskStarted(const int task_id)
{
    if (task_manager_ == nullptr || !taskBelongsToCurrentModelManager(task_id))
        return;

    const TaskManager::Task *task = task_manager_->findTask(task_id);
    if (task == nullptr)
        return;

    if (task->status == TaskManager::Preparing)
    {
        if (task_manager_->markTaskRunning(task_id))
        {
            syncTaskModelState(task_id);
            touchTaskModelModifiedTime(task_id);
        }
        return;
    }

    // 用户在 QProcess::Starting 阶段点击停止时，进程刚启动也必须继续收敛。
    if (task->status == TaskManager::Stopping && external_task_runner_ != nullptr)
        external_task_runner_->stop(task_id);
}

void ModelTaskController::handleExternalTaskStartFailed(const int task_id, const QString &error)
{
    if (task_manager_ == nullptr || !taskBelongsToCurrentModelManager(task_id))
        return;

    const TaskManager::Task *task = task_manager_->findTask(task_id);
    if (task == nullptr)
        return;

    if (task->status == TaskManager::Stopping)
    {
        task_manager_->markTaskStopped(task_id);
        syncTaskModelState(task_id);
        touchTaskModelModifiedTime(task_id);
        return;
    }
    if (task->status == TaskManager::Preparing)
        failTask(task_id, error.isEmpty() ? QString("外部模型任务进程启动失败") : error);
}

bool ModelTaskController::buildTestEvaluationOptions(const int task_id, ModelEvaluationOptions &options,
                                                      QString *err_msg) const
{
    if (task_manager_ == nullptr || model_manager_ == nullptr)
        return setError(err_msg, QString("评估上下文未初始化"));
    const TaskManager::Task *task = task_manager_->findTask(task_id);
    if (task == nullptr || !isTestModelTask(task->type) || task->scope_uuid.trimmed().isEmpty())
        return setError(err_msg, QString("测试任务上下文无效"));
    const QString model_uuid = task->model_uuid;
    const QString task_scope = task->scope_uuid;
    const QString task_name = task->scope_name;
    const ModelManager::ModelRecordView record = model_manager_->modelRecordViewForUuid(model_uuid);
    if (!record.isValid())
        return setError(err_msg, QString("模型不存在: %1").arg(model_uuid));
    ModelTestTaskDefinition definition;
    QString error;
    if (!test_task_repository_.loadTask(record.name, task_scope, definition, &error))
        return setError(err_msg, error);

    const ModelStorageService storage(project_dir_);
    const QString task_directory = definition.directory_name.isEmpty() ? task_name : definition.directory_name;
    options.model_uuid = model_uuid;
    options.test_task_uuid = task_scope;
    options.model_name = record.name;
    options.task_directory = task_directory;
    // 评估能力由项目方法决定，而不是由框架名决定（例如 anomalib
    // 框架既可能承载异常检测，也可能扩展其他方法）。
    options.method = evaluationMethodForProject(method_);
    options.dataset_manifest_path = cleanPath(QDir(storage.testTaskDatasetPath(record.name, task_directory))
                                                  .filePath(QStringLiteral("test/manifest.yaml")));
    if (!QFileInfo::exists(options.dataset_manifest_path))
        options.dataset_manifest_path = cleanPath(QDir(storage.testTaskDatasetPath(record.name, task_directory))
                                                      .filePath(QStringLiteral("manifest.yaml")));
    // Anomalib exports its split file as <split>.yaml directly under the
    // dataset directory and uses a `samples` sequence instead of `images`.
    // Keep the evaluator protocol-independent by accepting that generated
    // file as the final fallback.
    if (!QFileInfo::exists(options.dataset_manifest_path))
        options.dataset_manifest_path = cleanPath(QDir(storage.testTaskDatasetPath(record.name, task_directory))
                                                      .filePath(QStringLiteral("test.yaml")));
    options.prediction_manifest_path = storage.testTaskPredictionManifestPath(record.name, task_directory);
    options.prediction_images_path = storage.testTaskPredictionImagesPath(record.name, task_directory);
    options.evaluation_dir = storage.testTaskEvaluationPath(record.name, task_directory);
    options.evaluation_config_path = storage.testTaskEvaluationConfigPath(record.name, task_directory);
    options.report_path = storage.testTaskEvaluationReportPath(record.name, task_directory);
    options.instances_path = storage.testTaskEvaluationInstancesPath(record.name, task_directory);

    QVariantMap prediction_config;
    const QString prediction_config_path = storage.testTaskPredictionConfigPath(record.name, task_directory);
    try
    {
        const QFileInfo file(prediction_config_path);
        if (file.exists() && file.isFile())
            prediction_config = common::yaml::nodeVariant(common::yaml::loadFile(file)).toMap();
    }
    catch (const std::exception &)
    {
        return setError(err_msg, QString("读取当前预测配置失败"));
    }

    // Evaluation parameters are optional for old parameter schemas; defaults
    // are deterministic and the report records the actual values used.
    const QVariantMap evaluation = definition.test_params.value(QStringLiteral("evaluation")).toMap();
    options.evaluation_config = evaluation;
    // The generated test manifest and image list are the current GT/image
    // revisions.  They participate in evaluation (not inference) freshness,
    // so changing annotations can reuse PRED while forcing a new report.
    options.evaluation_config.insert(QStringLiteral("ground_truth_digest"),
                                     fileDigest(options.dataset_manifest_path));
    options.evaluation_config.insert(QStringLiteral("image_list_digest"),
                                     fileDigest(options.prediction_images_path));
    options.evaluation_config.insert(QStringLiteral("inference_digest"),
                                     prediction_config.value(QStringLiteral("inference_digest")));
    options.evaluation_config.insert(QStringLiteral("input_data_digest"),
                                     prediction_config.value(QStringLiteral("input_data_digest")));
    options.evaluation_config.insert(QStringLiteral("weight_digest"),
                                     prediction_config.value(QStringLiteral("weight_digest")));
    options.evaluation_config.insert(QStringLiteral("checkpoint_path"),
                                     prediction_config.value(QStringLiteral("checkpoint_path")));
    options.evaluation_config.insert(QStringLiteral("checkpoint_signature"),
                                     prediction_config.value(QStringLiteral("checkpoint_signature")));
    options.evaluation_config.insert(QStringLiteral("ground_truth_revision"),
                                     options.evaluation_config.value(QStringLiteral("ground_truth_digest")));
    options.evaluation_config.insert(QStringLiteral("input_digest"),
                                     options.evaluation_config.value(QStringLiteral("image_list_digest")));
    options.confidence_threshold = evaluation.value(QStringLiteral("confidence_threshold"), 0.5).toDouble();
    options.iou_threshold = evaluation.value(QStringLiteral("iou_threshold"), 0.5).toDouble();
    options.matching_strategy = evaluation.value(QStringLiteral("matching_strategy"), QStringLiteral("greedy_iou"))
                                    .toString();
    return true;
}

bool ModelTaskController::commitTestEvaluationResult(const int task_id, const ModelEvaluationOptions &options,
                                                     const ModelEvaluationResult &evaluation_result,
                                                     QVariantMap &result, QString *err_msg)
{
    if (task_manager_ == nullptr)
        return setError(err_msg, QString("评估上下文未初始化"));
    const TaskManager::Task *task = task_manager_->findTask(task_id);
    if (task == nullptr || !isTestModelTask(task->type))
        return setError(err_msg, QString("测试任务上下文无效"));
    result = evaluation_result.result;
    result.insert(QStringLiteral("schema_version"), 1);
    result.insert(QStringLiteral("model_uuid"), options.model_uuid);
    result.insert(QStringLiteral("test_task_uuid"), options.test_task_uuid);
    result.insert(QStringLiteral("method"), options.method);
    result.insert(QStringLiteral("status"), QStringLiteral("finished"));
    result.insert(QStringLiteral("evaluated_at"), QDateTime::currentSecsSinceEpoch());
    result.insert(QStringLiteral("prediction_dir"), QStringLiteral("pred"));
    result.insert(QStringLiteral("prediction_images"), QStringLiteral("pred/images.txt"));
    result.insert(QStringLiteral("prediction_manifest"), QStringLiteral("pred/manifest.yaml"));
    result.insert(QStringLiteral("evaluation_report"), QStringLiteral("evaluation/report.yaml"));
    result.insert(QStringLiteral("inference_digest"), options.evaluation_config.value(QStringLiteral("inference_digest")));
    result.insert(QStringLiteral("input_data_digest"), options.evaluation_config.value(QStringLiteral("input_data_digest")));
    result.insert(QStringLiteral("weight_digest"), options.evaluation_config.value(QStringLiteral("weight_digest")));
    result.insert(QStringLiteral("checkpoint_path"), options.evaluation_config.value(QStringLiteral("checkpoint_path")));
    result.insert(QStringLiteral("checkpoint_signature"),
                  options.evaluation_config.value(QStringLiteral("checkpoint_signature")));
    result.insert(QStringLiteral("ground_truth_digest"),
                  options.evaluation_config.value(QStringLiteral("ground_truth_digest")));
    result.insert(QStringLiteral("ground_truth_revision"),
                  options.evaluation_config.value(QStringLiteral("ground_truth_revision")));
    result.insert(QStringLiteral("image_list_digest"),
                  options.evaluation_config.value(QStringLiteral("image_list_digest")));
    result.insert(QStringLiteral("input_digest"), options.evaluation_config.value(QStringLiteral("input_digest")));
    result.insert(QStringLiteral("evaluation_digest"), result.value(QStringLiteral("evaluation_digest")));
    ModelTestTaskRepository repository(project_dir_);
    if (!repository.writeResult(options.model_name, options.task_directory, result, err_msg))
    {
        // report.yaml is not a committed result on its own.  Remove the
        // just-generated evaluation directory so a failed result commit
        // cannot be shown as an apparently valid report on next launch.
        QDir(options.evaluation_dir).removeRecursively();
        return false;
    }
    return true;
}

bool ModelTaskController::runTestEvaluation(const int task_id, QVariantMap &result, QString *err_msg)
{
    ModelEvaluationOptions options;
    if (!buildTestEvaluationOptions(task_id, options, err_msg))
        return false;
    ModelEvaluationResult evaluation_result;
    QString error;
    if (!ModelEvaluationService::evaluate(options, &evaluation_result, &error))
        return setError(err_msg, error);
    return commitTestEvaluationResult(task_id, options, evaluation_result, result, err_msg);
}

void ModelTaskController::runTestEvaluationAsync(const int task_id)
{
    if (evaluation_tasks_.contains(task_id))
        return;
    ModelEvaluationOptions options;
    QString preparation_error;
    if (!buildTestEvaluationOptions(task_id, options, &preparation_error))
    {
        failTask(task_id, preparation_error.isEmpty() ? QString("准备测试评估失败") : preparation_error);
        return;
    }
    const auto cancel_token = std::make_shared<std::atomic_bool>(false);
    options.cancel_token = cancel_token;
    evaluation_tasks_.insert(task_id);
    evaluation_cancel_tokens_.insert(task_id, cancel_token);
    const QPointer<ModelTaskController> guard(this);
    QThreadPool::globalInstance()->start([guard, task_id, options, cancel_token]()
    {
        if (guard.isNull())
            return;
        ModelEvaluationResult evaluation_result;
        QString error;
        const bool success = ModelEvaluationService::evaluate(options, &evaluation_result, &error);
        QMetaObject::invokeMethod(guard, [guard, task_id, options, success, evaluation_result, error]()
        {
            if (guard.isNull())
                return;
            guard->evaluation_tasks_.remove(task_id);
            guard->evaluation_cancel_tokens_.remove(task_id);
            if (guard->task_manager_ == nullptr)
                return;
            const TaskManager::Task *task = guard->task_manager_->findTask(task_id);
            if (task == nullptr || TaskManager::isTerminal(task->status))
                return;
            if (!success)
            {
                guard->failTask(task_id, error.isEmpty() ? QString("测试评估失败") : error);
                return;
            }
            QVariantMap result;
            QString commit_error;
            if (!guard->commitTestEvaluationResult(task_id, options, evaluation_result, result, &commit_error))
            {
                guard->failTask(task_id, commit_error.isEmpty() ? QString("提交测试评估结果失败") : commit_error);
                return;
            }
            guard->task_manager_->updateTaskPhase(task_id, QStringLiteral("saving_result"));
            guard->task_manager_->updateTaskProgress(task_id, 99);
            guard->task_manager_->updateTaskPhase(task_id, QStringLiteral("finished"));
            guard->task_manager_->finishTask(task_id);
            guard->syncTaskModelState(task_id);
        }, Qt::QueuedConnection);
    });
}

void ModelTaskController::handleExternalTaskFinished(const int task_id, const int exit_code, const bool normal_exit,
                                                     const bool stop_requested)
{
    if (task_manager_ == nullptr || !taskBelongsToCurrentModelManager(task_id))
        return;

    const TaskManager::Task *task = task_manager_->findTask(task_id);
    if (task == nullptr)
        return;
    if (TaskManager::isTerminal(task->status))
    {
        syncTaskModelState(task_id);
        return;
    }

    touchTaskModelModifiedTime(task_id);
    if (task->status == TaskManager::Stopping || stop_requested || (normal_exit && exit_code == 2))
    {
        task_manager_->markTaskStopped(task_id);
        syncTaskModelState(task_id);
        return;
    }
    if (normal_exit && exit_code == 0)
    {
        if (task->status == TaskManager::Preparing)
            task_manager_->markTaskRunning(task_id);
        if (isTestModelTask(task->type) && !task->scope_uuid.trimmed().isEmpty())
        {
            task_manager_->updateTaskPhase(task_id, QStringLiteral("evaluating"));
            task_manager_->updateTaskProgress(task_id, 90);
            syncTaskModelState(task_id);
            runTestEvaluationAsync(task_id);
            return;
        }
        task_manager_->updateTaskPhase(task_id, QStringLiteral("finished"));
        task_manager_->finishTask(task_id);
        syncTaskModelState(task_id);
        return;
    }

    task = task_manager_->findTask(task_id);
    if (task != nullptr && !TaskManager::isTerminal(task->status))
    {
        const QString name = modelTaskDisplayName(task->type);
        failTask(task_id, normal_exit ? QString("%1失败（退出码 %2），请查看模型日志。").arg(name).arg(exit_code)
                                      : QString("%1异常退出，请查看模型日志。").arg(name));
    }
}

} // namespace dltool::model
