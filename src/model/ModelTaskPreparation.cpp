#include "model/ModelTaskPreparation.h"

#include "common/Utils.h"
#include "data/DatasetExportSource.h"
#include "database/ModelDataBase.h"
#include "database/ModelTaskDataBase.h"
#include "model/ModelDatasetOrganizer.h"
#include "model/ModelDatasetSelection.h"
#include "model/ModelStorageService.h"
#include "model/TaskCommunication.h"
#include "settings/GlobalSettings.h"

#include <QDir>
#include <QDirIterator>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <set>

using dltool::common::cleanPath;

namespace dltool::model {
using common::ensureDirectory;
using common::setError;

namespace {

QList<qint64> allLabelClassIdsForDataset(const dltool::data::DatasetExportSource &source, qint64 dataset_id)
{
    std::set<qint64> class_ids;
    for (const int64_t image_id : source.allImageIds())
    {
        if (source.imageDatasetId(image_id) != dataset_id)
            continue;

        const QVariantMap image_level = source.imageLevelLabelData(image_id);
        const qint64 image_class_id
            = image_level.value(QStringLiteral("label_class_id"), -1).toLongLong();
        if (image_class_id >= 0)
            class_ids.insert(image_class_id);

        for (const int64_t label_id : source.imageLabelIds(image_id))
        {
            const qint64 class_id = source.labelClassId(label_id);
            if (class_id >= 0)
                class_ids.insert(class_id);
        }
    }
    return {class_ids.begin(), class_ids.end()};
}

std::function<QList<qint64>(qint64)> datasetClassIdsResolver(const dltool::data::DatasetExportSource *source)
{
    return [source](const qint64 dataset_id) -> QList<qint64>
    {
        return source != nullptr ? allLabelClassIdsForDataset(*source, dataset_id) : QList<qint64>{};
    };
}

bool clearTensorBoardEventFiles(const QString &log_dir, QString *err_msg)
{
    const QString root = cleanPath(QFileInfo(log_dir).absoluteFilePath());
    if (root.isEmpty())
        return setError(err_msg, QString("TensorBoard 日志目录为空"));

    const QFileInfo root_info(root);
    if (!root_info.exists())
        return true;
    if (!root_info.isDir())
        return setError(err_msg, QString("TensorBoard 日志路径不是目录: %1").arg(root));

    QStringList failed_files;
    QDirIterator iterator(root, {QStringLiteral("events.out.tfevents.*")}, QDir::Files | QDir::NoSymLinks,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext())
    {
        const QString event_file = cleanPath(QFileInfo(iterator.next()).absoluteFilePath());
        if (event_file.isEmpty() || !event_file.startsWith(root + QStringLiteral("/"), Qt::CaseInsensitive))
        {
            failed_files.push_back(event_file);
            continue;
        }
        if (!QFile::remove(event_file))
            failed_files.push_back(event_file);
    }

    if (!failed_files.isEmpty())
        return setError(err_msg, QString("删除 TensorBoard 历史 event 文件失败: %1")
                                     .arg(failed_files.join(QStringLiteral(", "))));
    return true;
}

bool persistModelDatabase(const ModelStorageService &storage, const QString &model_name,
                          const ModelTaskRequest &request, const QVariantMap &train_params,
                          const dltool::data::DatasetExportSource *dataset_source, QString *err_msg)
{
    database::ModelDataBase database(storage.modelDatabasePath(model_name));
    QString database_error;
    if (!database.replaceTrainParams(train_params, &database_error)
        || !database.replaceDatasets(databaseDatasetSelections(request.selections, datasetClassIdsResolver(dataset_source)),
                                     &database_error))
        return setError(err_msg, QString("写入模型数据库失败: %1").arg(database_error));
    return true;
}

bool persistTaskDatabase(const ModelStorageService &storage, const QString &model_name,
                         const ModelTaskRequest &request, const dltool::data::DatasetExportSource *dataset_source,
                         QString *err_msg)
{
    const QString task_id = request.scope_uuid.trimmed();
    const QString task_directory = request.model_config.task_directory.trimmed();
    if (task_id.isEmpty() || task_directory.isEmpty())
        return setError(err_msg, QString("测试任务 ID 或目录为空"));

    const QString database_path = storage.testTaskDatabasePath(model_name, task_directory);
    if (database_path.isEmpty())
        return setError(err_msg, QString("测试任务数据库路径为空"));

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const qint64 ctime = request.model_config.created_at > 0 ? request.model_config.created_at : now;
    const qint64 mtime = request.model_config.modified_at > 0 ? request.model_config.modified_at : now;
    database::ModelTaskDataBase database(database_path);
    QString database_error;
    if (!database.upsertTaskInfo({task_id, ctime, mtime}, &database_error)
        || !database.replaceTestParams(request.model_config.test_params, &database_error)
        || !database.replaceDatasets(databaseDatasetSelections(request.selections, datasetClassIdsResolver(dataset_source)),
                                     &database_error))
        return setError(err_msg, QString("写入测试任务数据库失败: %1").arg(database_error));
    return true;
}

bool isFsSam2Request(const ModelTaskRequest &request)
{
    return request.framework.name.compare(QString("FS-SAM2"), Qt::CaseInsensitive) == 0
        && (request.task_type == ModelTaskType::Train || request.task_type == ModelTaskType::Test
            || request.task_type == ModelTaskType::BoxToMask);
}

QString fsSam2PredictionDirectory(const ModelStorageService &storage, const QString &model_name,
                                  const QVariantMap &test_params)
{
    const QVariantMap inference = test_params.value(QStringLiteral("inference")).toMap();
    const QString configured = cleanPath(inference.value(QStringLiteral("output_dir")).toString());
    return configured.isEmpty() ? cleanPath(storage.testTaskPredictionPath(model_name, QString("fs_sam2")))
                                : configured;
}

bool prepareFsSam2Task(int method, const QString &project_dir, const ModelTaskRequest &request,
                       const dltool::data::DatasetExportSource *dataset_source, ExternalProcessSpec &process_spec,
                       QString *err_msg)
{
    const ModelStorageService storage(project_dir);
    const QString model_name = request.model_config.model_name.trimmed();
    const QString model_root = storage.path(model_name, ModelStorageLocation::ModelRoot);
    const QString model_db = storage.modelDatabasePath(model_name);
    const QString dataset_dir = storage.sharedDatasetPath(model_name);
    if (model_root.isEmpty() || model_db.isEmpty() || dataset_dir.isEmpty())
        return setError(err_msg, QString("FS-SAM2 模型存储路径为空"));

    QVariantMap train_params = request.model_config.train_params;
    const QVariantMap inference = request.model_config.test_params.value(QStringLiteral("inference")).toMap();
    if (!inference.isEmpty())
        train_params.insert(QStringLiteral("inference"), inference);
    if (!persistModelDatabase(storage, model_name, request, train_params, dataset_source, err_msg))
        return false;

    const QString task_directory = request.model_config.task_directory.trimmed().isEmpty()
        ? QString("fs_sam2")
        : request.model_config.task_directory.trimmed();
    QString prediction_dir;
    if (request.task_type == ModelTaskType::Test)
    {
        QString storage_error;
        if (!storage.ensureTestTaskStorage(model_name, task_directory, &storage_error))
            return setError(err_msg, QString("创建 FS-SAM2 测试任务目录失败: %1").arg(storage_error));

        // FS-SAM2 does not expose ordinary evaluation, but its test inputs
        // still use the task-level database layout.  Its dedicated task has
        // no UUID-backed test-task record, so use the stable directory name
        // as task_info.task_id.
        ModelTaskRequest task_request = request;
        task_request.scope_uuid = request.scope_uuid.trimmed().isEmpty() ? task_directory : request.scope_uuid;
        task_request.model_config.scope_uuid = task_request.scope_uuid;
        task_request.model_config.task_directory = task_directory;
        if (!persistTaskDatabase(storage, model_name, task_request, dataset_source, err_msg))
            return false;

        prediction_dir = fsSam2PredictionDirectory(storage, model_name, request.model_config.test_params);
        const QString clean_model_root = cleanPath(QFileInfo(model_root).absoluteFilePath());
        const QString clean_prediction_dir = cleanPath(QFileInfo(prediction_dir).absoluteFilePath());
        if (clean_prediction_dir.isEmpty()
            || !clean_prediction_dir.startsWith(clean_model_root + QStringLiteral("/"), Qt::CaseInsensitive))
            return setError(err_msg, QString("FS-SAM2 预测目录非法"));
        if (QDir(clean_prediction_dir).exists() && !QDir(clean_prediction_dir).removeRecursively())
            return setError(err_msg, QString("清理 FS-SAM2 历史预测结果失败"));
        if (!QDir().mkpath(clean_prediction_dir))
            return setError(err_msg, QString("创建 FS-SAM2 预测目录失败: %1").arg(clean_prediction_dir));
        const QString file_list_path = storage.testTaskFileListPath(model_name, task_directory);
        if (QFileInfo::exists(file_list_path) && !QFile::remove(file_list_path))
            return setError(err_msg, QString("清理 FS-SAM2 历史测试文件列表失败"));
    }

    const ModelTaskDescriptor descriptor = describeModelTask(request.task_type);
    if (descriptor.requires_dataset_export)
    {
        if (dataset_source == nullptr)
            return setError(err_msg, QString("FS-SAM2 数据集导出源为空"));

        ModelDatasetExportRequest export_request;
        export_request.method             = method;
        export_request.framework_name     = request.model_config.framework_name;
        export_request.model_architecture = request.model_config.model_architecture;
        export_request.model_uuid         = request.model_config.model_uuid;
        export_request.task_type          = request.task_type;
        export_request.dataset_dir        = dataset_dir;
        export_request.train_dir          = storage.trainRoot(model_name);
        if (request.task_type == ModelTaskType::Test)
        {
            export_request.test_file_list_path = storage.testTaskFileListPath(model_name, task_directory);
        }
        export_request.selections         = request.selections;
        export_request.source             = dataset_source;
        QString dataset_error;
        if (ModelDatasetOrganizer::organize(export_request, &dataset_error).isEmpty())
            return setError(err_msg, QString("FS-SAM2 数据集组织失败: %1").arg(dataset_error));
    }

    const QString script_path = request.framework.scriptFor(request.task_type);
    if (script_path.isEmpty())
        return setError(err_msg, QString("FS-SAM2 未定义任务脚本: %1").arg(modelTaskKey(request.task_type)));
    if (!QFileInfo::exists(script_path))
        return setError(err_msg, QString("FS-SAM2 脚本不存在: %1").arg(script_path));

    const QString python_env_path = dltool::settings::GlobalSettings::pythonEnvironmentPath();
    const QFileInfo python_env_info(cleanPath(python_env_path));
    if (python_env_path.trimmed().isEmpty() || !python_env_info.exists() || !python_env_info.isDir())
        return setError(err_msg, QString("Python 环境目录无效: %1").arg(python_env_path));
    const QString python_executable = dltool::common::pythonExecutableFromEnvPath(python_env_path);
    if (python_executable.isEmpty())
        return setError(err_msg, QString("未配置 Python 环境目录"));

    const QString log_dir = request.task_type == ModelTaskType::Train
        ? storage.trainLogsPath(model_name)
        : storage.testLogsPath(model_name);
    const QString log_path = request.task_type == ModelTaskType::Train
        ? storage.trainLogPath(model_name)
        : storage.testTaskLogPath(model_name, request.task_type == ModelTaskType::Test ? QString("fs_sam2")
                                                                                       : QString("fs_sam2_box_to_mask"));
    if (log_path.isEmpty())
        return setError(err_msg, QString("FS-SAM2 日志路径为空"));

    process_spec.task_id = request.task_id;
    process_spec.program = python_executable;
    process_spec.arguments = {
        script_path,
        QStringLiteral("--model_db"), model_db,
        QStringLiteral("--project_db"), request.project_database_path,
        QStringLiteral("--model_root"), model_root,
        QStringLiteral("--dataset_dir"), dataset_dir,
        QStringLiteral("--weight_dir"), storage.trainWeightsPath(model_name),
        QStringLiteral("--log_dir"), log_dir,
        QStringLiteral("--model_uuid"), request.model_config.model_uuid,
        QStringLiteral("--model_architecture"), request.model_config.model_architecture,
        QStringLiteral("--method"), QString::number(method),
    };
    if (request.task_type == ModelTaskType::Test)
    {
        process_spec.arguments << QStringLiteral("--prediction_dir") << prediction_dir
                               << QStringLiteral("--test_file_list")
                               << storage.testTaskFileListPath(model_name, task_directory);
    }
    process_spec.arguments << QStringLiteral("--dltool_task_host") << request.task_server_host
                           << QStringLiteral("--dltool_task_port") << QString::number(request.task_server_port)
                           << QStringLiteral("--dltool_task_id") << QString::number(request.task_id);
    if (request.task_type == ModelTaskType::BoxToMask)
    {
        process_spec.arguments << QStringLiteral("--dltool_progress_base") << QStringLiteral("0")
                               << QStringLiteral("--dltool_progress_span") << QStringLiteral("100");
    }
    process_spec.working_directory = request.framework.root;
    process_spec.python_paths = request.framework.python_paths;
    process_spec.log_path = log_path;
    return true;
}

bool prepareRegularTask(int method, const QString &project_dir, const ModelTaskRequest &request,
                        const dltool::data::DatasetExportSource *dataset_source, ExternalProcessSpec &process_spec,
                        QString *err_msg)
{
    const ModelStorageService storage(project_dir);
    const QString model_name = request.model_config.model_name.trimmed();
    const bool is_train = isTrainModelTask(request.task_type);
    const QString task_directory = request.model_config.task_directory.trimmed();
    if (!is_train && (request.scope_uuid.trimmed().isEmpty() || task_directory.isEmpty()))
        return setError(err_msg, QString("测试任务 ID 或目录为空"));

    QString storage_error;
    if (is_train)
    {
        if (!storage.ensureTrainStorage(model_name, &storage_error))
            return setError(err_msg, QString("创建训练目录失败: %1").arg(storage_error));
    }
    else if (!storage.ensureTestTaskStorage(model_name, task_directory, &storage_error))
    {
        return setError(err_msg, QString("创建测试任务目录失败: %1").arg(storage_error));
    }

    const QString model_db = storage.modelDatabasePath(model_name);
    const QString dataset_dir = storage.sharedDatasetPath(model_name);
    if (model_db.isEmpty() || dataset_dir.isEmpty())
        return setError(err_msg, QString("模型数据库或数据集目录为空"));

    if (is_train)
    {
        if (!persistModelDatabase(storage, model_name, request, request.model_config.train_params, dataset_source,
                                  err_msg))
            return false;
    }
    else if (!persistTaskDatabase(storage, model_name, request, dataset_source, err_msg))
    {
        return false;
    }
    else
    {
        database::ModelTaskDataBase task_database(
            storage.testTaskDatabasePath(model_name, task_directory));
        QString prediction_error;
        if (!task_database.clearPredictions(&prediction_error))
            return setError(err_msg, QString("清理历史预测结果失败: %1").arg(prediction_error));
    }

    QString prediction_dir;
    if (!is_train)
    {
        prediction_dir = storage.testTaskPredictionPath(model_name, task_directory);
        const QString task_root = cleanPath(QFileInfo(storage.testTaskRoot(model_name, task_directory)).absoluteFilePath());
        const QString clean_prediction_dir = cleanPath(QFileInfo(prediction_dir).absoluteFilePath());
        if (task_root.isEmpty() || clean_prediction_dir.isEmpty()
            || !clean_prediction_dir.startsWith(task_root + QStringLiteral("/"), Qt::CaseInsensitive))
            return setError(err_msg, QString("测试预测路径非法"));
        if (QDir(clean_prediction_dir).exists() && !QDir(clean_prediction_dir).removeRecursively())
            return setError(err_msg, QString("清理旧预测目录失败"));
        if (!QDir().mkpath(clean_prediction_dir))
            return setError(err_msg, QString("重建测试结果目录失败"));
        const QString file_list_path = storage.testTaskFileListPath(model_name, task_directory);
        if (QFileInfo::exists(file_list_path) && !QFile::remove(file_list_path))
            return setError(err_msg, QString("清理旧测试文件列表失败"));
    }

    if (dataset_source == nullptr)
        return setError(err_msg, QString("数据集导出源为空"));
    ModelDatasetExportRequest export_request;
    export_request.method             = method;
    export_request.framework_name     = request.model_config.framework_name;
    export_request.model_architecture = request.model_config.model_architecture;
    export_request.model_uuid         = request.model_config.model_uuid;
    export_request.task_type          = request.task_type;
    export_request.dataset_dir        = dataset_dir;
    export_request.train_dir          = storage.trainRoot(model_name);
    if (!is_train)
        export_request.test_file_list_path = storage.testTaskFileListPath(model_name, task_directory);
    export_request.selections         = request.selections;
    export_request.source             = dataset_source;
    QString dataset_error;
    const QVariantMap datasets = ModelDatasetOrganizer::organize(export_request, &dataset_error);
    if (datasets.isEmpty())
        return setError(err_msg, QString("数据集组织失败: %1").arg(dataset_error));

    const QString script_path = request.framework.scriptFor(request.task_type);
    if (script_path.isEmpty())
        return setError(err_msg, QString("框架未定义脚本, 框架: %1, 任务: %2")
                                     .arg(request.framework.name, modelTaskKey(request.task_type)));
    if (!QFileInfo::exists(script_path))
        return setError(err_msg, QString("脚本不存在: %1").arg(script_path));

    const QString python_env_path = dltool::settings::GlobalSettings::pythonEnvironmentPath();
    const QFileInfo python_env_info(cleanPath(python_env_path));
    if (python_env_path.trimmed().isEmpty() || !python_env_info.exists() || !python_env_info.isDir())
        return setError(err_msg, QString("Python 环境目录无效: %1").arg(python_env_path));
    const QString python_executable = dltool::common::pythonExecutableFromEnvPath(python_env_path);
    if (python_executable.isEmpty())
        return setError(err_msg, QString("未配置 Python 环境目录"));

    const QString log_dir = is_train ? storage.trainLogsPath(model_name) : storage.testLogsPath(model_name);
    QString log_path = is_train ? storage.trainLogPath(model_name) : storage.testTaskLogPath(model_name, request.scope_uuid);
    if (log_path.isEmpty())
        return setError(err_msg, QString("日志路径为空"));
    QString tensorboard_error;
    if (is_train && !clearTensorBoardEventFiles(log_dir, &tensorboard_error))
        return setError(err_msg, tensorboard_error);

    process_spec.task_id = request.task_id;
    process_spec.program = python_executable;
    process_spec.arguments = {
        script_path,
        QStringLiteral("--model_db"), model_db,
        QStringLiteral("--project_db"), request.project_database_path,
        QStringLiteral("--model_root"), storage.path(model_name, ModelStorageLocation::ModelRoot),
        QStringLiteral("--dataset_dir"), dataset_dir,
        QStringLiteral("--train_dir"), storage.trainRoot(model_name),
        QStringLiteral("--masks_dir"), dataset_dir,
        QStringLiteral("--weight_dir"), storage.trainWeightsPath(model_name),
        QStringLiteral("--log_dir"), log_dir,
        QStringLiteral("--model_uuid"), request.model_config.model_uuid,
        QStringLiteral("--model_architecture"), request.model_config.model_architecture,
        QStringLiteral("--method"), QString::number(method),
    };
    if (!is_train)
    {
        process_spec.arguments << QStringLiteral("--task_db")
                               << storage.testTaskDatabasePath(model_name, task_directory)
                               << QStringLiteral("--test_file_list")
                               << storage.testTaskFileListPath(model_name, task_directory)
                               << QStringLiteral("--prediction_dir") << prediction_dir;
    }
    process_spec.arguments << QStringLiteral("--dltool_task_host") << request.task_server_host
                           << QStringLiteral("--dltool_task_port") << QString::number(request.task_server_port)
                           << QStringLiteral("--dltool_task_id") << QString::number(request.task_id);
    process_spec.working_directory = request.framework.root;
    process_spec.python_paths = request.framework.python_paths;
    process_spec.log_path = log_path;
    return true;
}

} // namespace

bool prepareModelTask(const int method, const QString &project_dir, const ModelTaskRequest &request,
                      const dltool::data::DatasetExportSource *dataset_source, ExternalProcessSpec &process_spec,
                      QString *err_msg)
{
    process_spec = {};
    if (request.task_id < 0)
        return setError(err_msg, QString("任务 id 无效"));
    if (!isKnownModelTask(request.task_type))
        return setError(err_msg, QString("任务类型无效"));
    if (request.task_server_host.trimmed().isEmpty() || request.task_server_port == 0)
        return setError(err_msg, QString("任务通信端点无效"));
    if (request.model_config.model_uuid.trimmed().isEmpty())
        return setError(err_msg, QString("模型 uuid 为空"));
    if (request.model_config.model_name.trimmed().isEmpty())
        return setError(err_msg, QString("模型名称为空"));

    ModelStorageService storage(project_dir);
    QString storage_error;
    if (!storage.ensureModelStorage(request.model_config.model_name, &storage_error))
        return setError(err_msg, QString("创建模型目录失败: %1").arg(storage_error));

    if (isFsSam2Request(request))
        return prepareFsSam2Task(method, project_dir, request, dataset_source, process_spec, err_msg);
    return prepareRegularTask(method, project_dir, request, dataset_source, process_spec, err_msg);
}

} // namespace dltool::model
