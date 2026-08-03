#include "model/ModelTaskPreparation.h"

#include "common/Utils.h"
#include "common/YamlUtils.h"
#include "data/DatasetExportSource.h"
#include "model/ModelDatasetOrganizer.h"
#include "model/ModelDatasetSelection.h"
#include "model/ModelStorageService.h"
#include "model/ModelTaskConfigService.h"
#include "settings/GlobalSettings.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QVariantMap>
#include <QSaveFile>
#include <yaml-cpp/yaml.h>

using dltool::common::cleanPath;

namespace dltool::model {
using common::setError;
using common::ensureDirectory;

namespace {

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
        if (event_file.isEmpty() || (event_file != root && !event_file.startsWith(root + QStringLiteral("/"),
                                                                       Qt::CaseInsensitive)))
        {
            failed_files.push_back(event_file);
            continue;
        }
        if (!QFile::remove(event_file))
            failed_files.push_back(event_file);
    }

    if (!failed_files.isEmpty())
    {
        return setError(err_msg, QString("删除 TensorBoard 历史 event 文件失败: %1")
                                     .arg(failed_files.join(QStringLiteral(", "))));
    }
    return true;
}

YAML::Node datasetManifestEntries(const YAML::Node &root)
{
    if (!root)
        return {};
    if (root.IsSequence())
        return root;
    if (!root.IsMap())
        return {};

    const YAML::Node images = root[evaluation::fieldName(evaluation::Field::Images).toStdString()];
    if (images && images.IsSequence())
        return images;
    const YAML::Node samples = root[evaluation::fieldName(evaluation::Field::Samples).toStdString()];
    return samples && samples.IsSequence() ? samples : YAML::Node{};
}

QByteArray predictionImagesData(const QVariantMap &datasets, QString *err_msg)
{
    const QVariantMap test_dataset = datasets.value(QStringLiteral("test")).toMap();
    const QString manifest_path = test_dataset.value(QStringLiteral("manifest"),
                                                     test_dataset.value(QStringLiteral("file_list"))).toString();
    if (manifest_path.isEmpty())
        return setError(err_msg, QString("测试数据集 manifest 路径为空")), QByteArray();
    QByteArray output("image_id,image_path\n");
    try
    {
        YAML::Node root = common::yaml::loadFile(QFileInfo(manifest_path));
        const YAML::Node images = datasetManifestEntries(root);
        if (!images || !images.IsSequence())
            return setError(err_msg, QString("测试数据集 manifest 缺少 images")), QByteArray();
        for (const YAML::Node &image : images)
        {
            const QVariantMap value = common::yaml::nodeVariant(image).toMap();
            QString image_path = value.value(QStringLiteral("path")).toString();
            image_path.replace(QStringLiteral("\""), QStringLiteral("\"\""));
            if (image_path.contains(QChar(',')) || image_path.contains(QChar('"'))
                || image_path.contains(QChar('\n')) || image_path.contains(QChar('\r')))
                image_path = QStringLiteral("\"") + image_path + QStringLiteral("\"");
            const QByteArray line = QStringLiteral("%1,%2\n")
                                        .arg(value.value(QStringLiteral("id")).toLongLong())
                                        .arg(image_path)
                                        .toUtf8();
            output.append(line);
        }
    }
    catch (const std::exception &e)
    {
        return setError(err_msg, QString("生成 pred/images.txt 失败: %1").arg(QString(e.what()))), QByteArray();
    }
    return output;
}

bool writePredictionImagesFile(const QString &path, const QVariantMap &datasets, QString *err_msg)
{
    const QByteArray output = predictionImagesData(datasets, err_msg);
    if (output.isEmpty())
        return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return setError(err_msg, QString("打开 pred/images.txt 失败: %1").arg(file.errorString()));
    if (file.write(output) != output.size())
        return setError(err_msg, QString("写入 pred/images.txt 失败: %1").arg(file.errorString()));
    if (!file.commit())
        return setError(err_msg, QString("提交 pred/images.txt 失败: %1").arg(file.errorString()));
    return true;
}

QString relativePath(const QString &root, const QString &path)
{
    const QString clean_root = cleanPath(QFileInfo(root).absoluteFilePath());
    const QString clean_value = cleanPath(path);
    if (clean_root.isEmpty() || clean_value.isEmpty() || !QFileInfo(clean_value).isAbsolute())
        return clean_value;
    return QDir::fromNativeSeparators(QDir(clean_root).relativeFilePath(clean_value));
}

void makeDatasetPathsRelative(QVariantMap &datasets, const QString &root)
{
    for (const QString &split : {QStringLiteral("train"), QStringLiteral("validation"), QStringLiteral("test")})
    {
        QVariantMap entry = datasets.value(split).toMap();
        for (const QString &field : {QStringLiteral("manifest"), QStringLiteral("file_list"),
                                     QStringLiteral("masks_dir")})
        {
            const QString value = entry.value(field).toString();
            if (!value.isEmpty())
                entry.insert(field, relativePath(root, value));
        }
        if (!entry.isEmpty())
            datasets.insert(split, entry);
    }
}

bool isLegacyFewShotRequest(const ModelTaskRequest &request)
{
    return isTestModelTask(request.task_type) && request.scope_uuid.trimmed().isEmpty()
        && request.framework.name.compare(QString("FS-SAM2"), Qt::CaseInsensitive) == 0;
}

QVariantMap inferenceParams(const QVariantMap &test_params)
{
    QVariantMap result = test_params;
    result.remove(QStringLiteral("evaluation"));
    return result;
}

QString checkpointPath(const ModelTaskRequest &request, const QString &model_root,
                       const QString &train_weights_root)
{
    const QVariantMap inference = request.model_config.test_params.value(QStringLiteral("inference")).toMap();
    const QString raw = inference.value(QStringLiteral("checkpoint_path")).toString().trimmed();
    if (raw.isEmpty())
        return {};
    const QFileInfo info(raw);
    if (info.isAbsolute())
        return cleanPath(info.absoluteFilePath());
    const QString model_candidate = cleanPath(QDir(model_root).filePath(raw));
    const QString weights_candidate = cleanPath(QDir(train_weights_root).filePath(raw));
    return QFileInfo::exists(model_candidate) ? model_candidate : weights_candidate;
}

QVariantMap checkpointInfo(const QString &path)
{
    const QFileInfo info(path);
    const bool valid = info.exists() && info.isFile();
    return {{QStringLiteral("path"), valid ? cleanPath(info.absoluteFilePath()) : QString()},
            {QStringLiteral("size"), valid ? info.size() : qint64(-1)},
            {QStringLiteral("mtime"), valid ? info.lastModified().toMSecsSinceEpoch() : qint64(-1)}};
}

bool predictionInputsMatch(const QString &prediction_dir, const QString &config_path, const QString &task_root,
                           const QString &model_root, const QString &train_weights_root,
                           const ModelTaskRequest &request, const QByteArray &images_data)
{
    QFile images_file(QDir(prediction_dir).filePath(QStringLiteral("images.txt")));
    if (!images_file.open(QIODevice::ReadOnly) || images_file.readAll() != images_data)
        return false;

    const QFileInfo config_file(config_path);
    if (!config_file.exists() || !config_file.isFile())
        return false;

    QVariantMap previous;
    try
    {
        previous = common::yaml::nodeVariant(common::yaml::loadFile(config_file)).toMap();
    }
    catch (const std::exception &)
    {
        return false;
    }

    if (previous.value(evaluation::fieldName(evaluation::Field::ModelUuid)).toString()
            != request.model_config.model_uuid
        || previous.value(evaluation::fieldName(evaluation::Field::TestTaskUuid)).toString() != request.scope_uuid
        || previous.value(QStringLiteral("task_type")).toString() != modelTaskKey(request.task_type)
        || previous.value(QStringLiteral("framework")).toString()
               != request.model_config.framework_name
        || previous.value(evaluation::fieldName(evaluation::Field::Method)).toString()
               != evaluation::methodKey(request.evaluation_method)
        || previous.value(QStringLiteral("model_architecture")).toString()
               != request.model_config.model_architecture
        || previous.value(QStringLiteral("model_params")).toMap()
               != request.model_config.train_params.value(QStringLiteral("model")).toMap()
        || inferenceParams(previous.value(QStringLiteral("test_params")).toMap())
               != inferenceParams(request.model_config.test_params))
        return false;

    const QString current_checkpoint = checkpointPath(request, model_root, train_weights_root);
    const QVariantMap current_info = checkpointInfo(current_checkpoint);
    const QString previous_checkpoint = previous.value(QStringLiteral("checkpoint_path")).toString();
    const QString previous_checkpoint_absolute = previous_checkpoint.isEmpty()
        ? QString()
        : (QFileInfo(previous_checkpoint).isAbsolute()
               ? cleanPath(previous_checkpoint)
               : cleanPath(QDir(task_root).filePath(previous_checkpoint)));
    if (current_info.value(QStringLiteral("path")).toString() != previous_checkpoint_absolute
        || current_info.value(QStringLiteral("size")) != previous.value(QStringLiteral("checkpoint_size"))
        || current_info.value(QStringLiteral("mtime")) != previous.value(QStringLiteral("checkpoint_mtime")))
        return false;
    return true;
}

bool writePredictionConfig(const QString &path, const QString &task_root, const QString &model_root,
                           const QString &train_weights_root,
                           const ModelTaskRequest &request,
                           const QVariantMap &datasets, const QByteArray &images_data, QString *err_msg)
{
    QVariantMap relative_datasets = datasets;
    makeDatasetPathsRelative(relative_datasets, task_root);
    const QVariantMap inference = request.model_config.test_params.value(QStringLiteral("inference")).toMap();
    const QString checkpoint = inference.value(QStringLiteral("checkpoint_path")).toString();
    QString checkpoint_path;
    if (!checkpoint.isEmpty())
    {
        if (QFileInfo(checkpoint).isAbsolute())
            checkpoint_path = cleanPath(checkpoint);
        else
        {
            const QString model_candidate = cleanPath(QDir(model_root).filePath(checkpoint));
            const QString weights_candidate
                = cleanPath(QDir(train_weights_root).filePath(checkpoint));
            checkpoint_path = QFileInfo::exists(model_candidate) ? model_candidate
                : (QFileInfo::exists(weights_candidate) ? weights_candidate : model_candidate);
        }
    }
    const QVariantMap value = {
        {evaluation::fieldName(evaluation::Field::SchemaVersion), 1},
        {evaluation::fieldName(evaluation::Field::ModelUuid), request.model_config.model_uuid},
        {evaluation::fieldName(evaluation::Field::TestTaskUuid), request.scope_uuid},
        {QStringLiteral("task_type"), modelTaskKey(request.task_type)},
        {QStringLiteral("framework"), request.model_config.framework_name},
        {evaluation::fieldName(evaluation::Field::Method), evaluation::methodKey(request.evaluation_method)},
        {QStringLiteral("model_architecture"), request.model_config.model_architecture},
        {QStringLiteral("model_params"), request.model_config.train_params.value(QStringLiteral("model")).toMap()},
        {QStringLiteral("test_params"), request.model_config.test_params},
        {QStringLiteral("datasets"), relative_datasets},
        {QStringLiteral("checkpoint_path"), relativePath(task_root, checkpoint_path)},
        {QStringLiteral("checkpoint_size"), checkpointInfo(checkpoint_path).value(QStringLiteral("size"))},
        {QStringLiteral("checkpoint_mtime"), checkpointInfo(checkpoint_path).value(QStringLiteral("mtime"))},
        {evaluation::fieldName(evaluation::Field::ImageCount), images_data.isEmpty() ? 0 : images_data.count('\n') - 1},
        {evaluation::fieldName(evaluation::Field::PredictionImages), QStringLiteral("images.txt")},
        {evaluation::fieldName(evaluation::Field::PredictionManifest), QStringLiteral("manifest.yaml")},
        {QStringLiteral("config_root"), QStringLiteral(".")},
        {evaluation::fieldName(evaluation::Field::PredictionDir), QStringLiteral(".")},
    };
    return common::yaml::writeFileAtomic(path, common::yaml::variantToYaml(value), err_msg,
                                         QString("打开预测配置失败"), QString("生成预测配置失败"),
                                         QString("提交预测配置失败"));
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

    const QString model_uuid = request.model_config.model_uuid.trimmed();
    if (model_uuid.isEmpty())
        return setError(err_msg, QString("模型 uuid 为空"));
    const QString model_name = request.model_config.model_name.trimmed();
    if (model_name.isEmpty())
        return setError(err_msg, QString("模型名称为空"));

    ModelStorageService storage(project_dir);
    QString             storage_err;
    if (!storage.ensureModelStorage(model_name, &storage_err))
        return setError(err_msg, QString("创建模型目录失败: %1").arg(storage_err));

    const bool is_train = isTrainModelTask(request.task_type);
    const bool legacy_few_shot_test = isLegacyFewShotRequest(request);
    if (!is_train && request.scope_uuid.trimmed().isEmpty() && !legacy_few_shot_test)
        return setError(err_msg, QString("测试任务 UUID 为空"));
    const QString task_directory = is_train ? QString() : request.model_config.task_directory;
    if (legacy_few_shot_test)
    {
        // FS-SAM2 owns a separate few-shot pipeline and still consumes its
        // model-level result/config/weight directories.  Keep those folders
        // isolated from the ordinary test-task layout.
        for (const ModelStorageLocation location : {ModelStorageLocation::Results,
                                                     ModelStorageLocation::Logs,
                                                     ModelStorageLocation::Weights,
                                                     ModelStorageLocation::Datasets,
                                                     ModelStorageLocation::Configs})
        {
            if (!ensureDirectory(storage.path(model_name, location), &storage_err,
                                 QString("小样本目录为空"), QString("创建小样本目录失败: %1")))
                return false;
        }
    }
    if (is_train)
    {
        if (!storage.ensureTrainStorage(model_name, &storage_err))
            return setError(err_msg, QString("创建训练目录失败: %1").arg(storage_err));
    }
    else if (!legacy_few_shot_test && !storage.ensureTestTaskStorage(model_name, task_directory, &storage_err))
    {
        return setError(err_msg, QString("创建测试任务目录失败: %1").arg(storage_err));
    }

    // A full inference run replaces the current PRED.  Evaluation-only runs
    // keep that PRED and replace only the C++ evaluation output.
    QString prediction_dir;
    QString evaluation_dir;
    QString result_path;
    const QString task_root = cleanPath(QFileInfo(storage.testTaskRoot(model_name, task_directory)).absoluteFilePath());
    bool evaluation_only = request.evaluation_only;
    QByteArray prediction_images_data;
    if (!is_train && !legacy_few_shot_test)
    {
        prediction_dir = storage.testTaskPredictionPath(model_name, task_directory);
        evaluation_dir = storage.testTaskEvaluationPath(model_name, task_directory);
        result_path = storage.testTaskResultPath(model_name, task_directory);
        const auto insideTaskRoot = [&task_root](const QString &path)
        {
            const QString value = cleanPath(QFileInfo(path).absoluteFilePath());
            return !task_root.isEmpty() && !value.isEmpty()
                && (value.compare(task_root, Qt::CaseInsensitive) == 0
                    || value.startsWith(task_root + QStringLiteral("/"), Qt::CaseInsensitive));
        };
        if (prediction_dir.isEmpty() || evaluation_dir.isEmpty() || result_path.isEmpty()
            || !insideTaskRoot(prediction_dir) || !insideTaskRoot(evaluation_dir) || !insideTaskRoot(result_path))
            return setError(err_msg, QString("测试结果路径非法"));

    }

    QVariantMap datasets;
    if (const ModelTaskDescriptor descriptor = describeModelTask(request.task_type);
        descriptor.requires_dataset_export)
    {
        if (dataset_source == nullptr)
            return setError(err_msg, QString("数据集导出源为空"));

        QString dataset_err;
        const QString dataset_dir = is_train
            ? storage.trainDatasetPath(model_name)
            : (legacy_few_shot_test ? storage.path(model_name, ModelStorageLocation::Datasets)
                                    : storage.testTaskDatasetPath(model_name, task_directory));
        if (!writeModelDatasetSelectionsFile(dataset_dir, request.selections, &dataset_err))
            return setError(err_msg, QString("写入数据集选择配置失败: %1").arg(dataset_err));

        ModelDatasetExportRequest dataset_request;
        dataset_request.method             = method;
        dataset_request.framework_name     = request.model_config.framework_name;
        dataset_request.model_architecture = request.model_config.model_architecture;
        dataset_request.model_uuid         = model_uuid;
        dataset_request.task_type          = request.task_type;
        dataset_request.dataset_dir        = dataset_dir;
        dataset_request.selections         = request.selections;
        dataset_request.source             = dataset_source;
        datasets                           = ModelDatasetOrganizer::organize(dataset_request, &dataset_err);
        if (datasets.isEmpty())
            return setError(err_msg, QString("数据集组织失败: %1").arg(dataset_err));
    }

    if (!is_train && !legacy_few_shot_test)
    {
        QString images_error;
        prediction_images_data = predictionImagesData(datasets, &images_error);
        if (prediction_images_data.isEmpty())
            return setError(err_msg, images_error.isEmpty() ? QString("生成 pred/images.txt 失败") : images_error);

        if (evaluation_only
            && !predictionInputsMatch(prediction_dir,
                                      storage.testTaskPredictionConfigPath(model_name, task_directory),
                                      task_root,
                                      storage.path(model_name, ModelStorageLocation::ModelRoot),
                                      storage.trainWeightsPath(model_name), request, prediction_images_data))
            evaluation_only = false;

        if (!evaluation_only && QDir(prediction_dir).exists() && !QDir(prediction_dir).removeRecursively())
            return setError(err_msg, QString("清理旧预测目录失败"));
        if (QDir(evaluation_dir).exists() && !QDir(evaluation_dir).removeRecursively())
            return setError(err_msg, QString("清理旧评估目录失败"));
        if (QFileInfo::exists(result_path) && !QFile::remove(result_path))
            return setError(err_msg, QString("清理旧测试结果失败"));
        if (!QDir().mkpath(prediction_dir) || !QDir().mkpath(evaluation_dir))
            return setError(err_msg, QString("重建测试结果目录失败"));
    }

    ModelTaskConfigService config_service(project_dir);
    QString                config_err;
    const QString config_path = config_service.write(
        model_name, request.task_type, task_directory,
        config_service.build(request.model_config, request.task_type, datasets), &config_err);
    if (config_path.isEmpty())
        return setError(err_msg, config_err);

    const QString log_dir = is_train
        ? storage.trainLogsPath(model_name)
        : (legacy_few_shot_test ? storage.path(model_name, ModelStorageLocation::Logs)
                                : storage.testLogsPath(model_name));
    QString       tensorboard_err;
    if (is_train && !clearTensorBoardEventFiles(log_dir, &tensorboard_err))
        return setError(err_msg, tensorboard_err);

    const QString log_path = is_train
        ? storage.trainLogPath(model_name)
        : (legacy_few_shot_test ? cleanPath(QDir(log_dir).filePath(QString("test.log")))
                                : storage.testTaskLogPath(model_name, request.scope_uuid));
    if (log_path.isEmpty())
        return setError(err_msg, QString("日志路径为空"));

    if (!is_train && !legacy_few_shot_test)
    {
        if (!evaluation_only)
        {
            if (!writePredictionImagesFile(storage.testTaskPredictionImagesPath(model_name, task_directory), datasets,
                                           err_msg)
                || !writePredictionConfig(storage.testTaskPredictionConfigPath(model_name, task_directory),
                                           storage.testTaskRoot(model_name, task_directory),
                                           storage.path(model_name, ModelStorageLocation::ModelRoot),
                                           storage.trainWeightsPath(model_name), request, datasets,
                                           prediction_images_data, err_msg))
                return false;
        }
    }

    if (evaluation_only)
    {
        process_spec.task_id = request.task_id;
        process_spec.evaluation_only = true;
        return true;
    }

    const QString script_path = request.framework.scriptFor(request.task_type);
    if (script_path.isEmpty())
    {
        return setError(err_msg, QString("框架未定义脚本, 框架: %1, 任务: %2")
                                     .arg(request.framework.name, modelTaskKey(request.task_type)));
    }
    if (!QFileInfo::exists(script_path))
        return setError(err_msg, QString("脚本不存在: %1").arg(script_path));

    const QString python_env_path = dltool::settings::GlobalSettings::pythonEnvironmentPath();
    if (python_env_path.trimmed().isEmpty())
        return setError(err_msg, QString("未配置 Python 环境目录"));

    const QFileInfo python_env_info(cleanPath(python_env_path));
    if (!python_env_info.exists() || !python_env_info.isDir())
        return setError(err_msg, QString("Python 环境目录无效: %1").arg(python_env_path));

    const QString python_executable = dltool::common::pythonExecutableFromEnvPath(python_env_path);
    if (python_executable.isEmpty())
        return setError(err_msg, QString("未配置 Python 环境目录"));

    process_spec.task_id   = request.task_id;
    process_spec.program   = python_executable;
    process_spec.arguments = {
        script_path,
        QStringLiteral("--config"),
        config_path,
        QStringLiteral("--dltool_task_host"),
        request.task_server_host,
        QStringLiteral("--dltool_task_port"),
        QString::number(request.task_server_port),
        QStringLiteral("--dltool_task_id"),
        QString::number(request.task_id),
    };
    process_spec.working_directory = request.framework.root;
    process_spec.python_paths      = request.framework.python_paths;
    process_spec.log_path          = log_path;
    return true;
}

} // namespace dltool::model
