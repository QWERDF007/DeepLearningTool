#include "model/ModelTaskPreparation.h"

#include "common/Utils.h"
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

using dltool::common::cleanPath;

namespace dltool::model {
using common::setError;

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

    QVariantMap datasets;
    if (const ModelTaskDescriptor descriptor = describeModelTask(request.task_type);
        descriptor.requires_dataset_export)
    {
        if (dataset_source == nullptr)
            return setError(err_msg, QString("数据集导出源为空"));

        QString dataset_err;
        const QString dataset_dir = storage.path(model_name, ModelStorageLocation::Datasets);
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

    ModelTaskConfigService config_service(project_dir);
    QString                config_err;
    const QString config_path = config_service.write(
        model_name, request.task_type, config_service.build(request.model_config, request.task_type, datasets),
        &config_err);
    if (config_path.isEmpty())
        return setError(err_msg, config_err);

    const QString log_dir = cleanPath(storage.path(model_name, ModelStorageLocation::Logs));
    QString       tensorboard_err;
    if (!clearTensorBoardEventFiles(log_dir, &tensorboard_err))
        return setError(err_msg, tensorboard_err);

    const QString log_path = cleanPath(QDir(log_dir).filePath(modelTaskLogStem(request.task_type)
                                                                  + QStringLiteral(".log")));
    if (log_path.isEmpty())
        return setError(err_msg, QString("日志路径为空"));

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
