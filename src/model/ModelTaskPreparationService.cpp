#include "model/ModelTaskPreparationService.h"

#include "model/IModel.h"
#include "model/ModelDatasetOrganizer.h"
#include "model/ModelStorageService.h"
#include "model/ModelTaskConfigService.h"
#include "model/TaskManager.h"
#include "settings/GlobalSettings.h"
#include "settings/SettingsKeys.h"
#include "settings/SettingsValue.h"

#include <QDir>
#include <QFileInfo>
#include <QVariantMap>
#include <utility>

namespace dltool::model {

namespace {

bool setError(QString *err_msg, const QString &message)
{
    if (err_msg != nullptr)
        *err_msg = message;
    return false;
}

QString pythonExecutableFromEnvPath(const QString &env_path)
{
    const QFileInfo info(cleanModelPath(env_path));
    if (info.isFile())
        return info.absoluteFilePath();

    const QDir dir(info.absoluteFilePath());
    for (const QString &candidate :
         {QStringLiteral("python.exe"), QStringLiteral("Scripts/python.exe"), QStringLiteral("bin/python"),
          QStringLiteral("python")})
    {
        const QString path = dir.filePath(candidate);
        if (QFileInfo::exists(path))
            return cleanModelPath(path);
    }
    return {};
}

} // namespace

ModelTaskPreparationService::ModelTaskPreparationService(int method, QString project_dir,
                                                         dltool::data::DataManager *data_manager)
    : method_(method)
    , project_dir_(cleanModelPath(std::move(project_dir)))
    , data_manager_(data_manager)
{
}

bool ModelTaskPreparationService::prepare(const Request &request, PreparedExternalModelTask &prepared,
                                          QString *err_msg) const
{
    prepared = {};

    auto *task_manager = TaskManager::getInstance();
    if (task_manager == nullptr)
        return setError(err_msg, QStringLiteral("任务管理器为空"));
    if (request.model == nullptr)
        return setError(err_msg, QStringLiteral("模型实例为空"));

    ModelStorageService storage(project_dir_);
    QString             storage_err;
    if (!storage.ensureModelStorage(request.model_uuid, &storage_err))
        return setError(err_msg, QStringLiteral("创建模型目录失败: %1").arg(storage_err));

    const QString script_path = scriptForTask(request.framework, request.task_type);
    if (script_path.isEmpty())
    {
        return setError(err_msg, QStringLiteral("框架未定义脚本, 框架: %1, 任务: %2")
                                     .arg(request.model->frameworkName(), modelTaskKey(request.task_type)));
    }
    if (!QFileInfo::exists(script_path))
        return setError(err_msg, QStringLiteral("脚本不存在: %1").arg(script_path));

    QString server_err;
    if (!task_manager->ensureTaskServer(&server_err))
        return setError(err_msg, QStringLiteral("任务通信服务启动失败: %1").arg(server_err));

    namespace generated_field = dltool::settings::generated::field;
    const QString python_executable = pythonExecutableFromEnvPath(dltool::settings::settingString(
        dltool::settings::GlobalSettings::getInstance(), generated_field::Software::PythonEnvPath));
    if (python_executable.isEmpty())
        return setError(err_msg, QStringLiteral("未配置 Python 环境目录"));

    QString dataset_err;
    ModelDatasetExportContext dataset_context;
    dataset_context.method             = method_;
    dataset_context.framework_name     = request.model->frameworkName();
    dataset_context.model_architecture = request.model->modelArchitecture();
    dataset_context.model_uuid         = request.model_uuid;
    dataset_context.task_type          = request.task_type;
    dataset_context.dataset_dir        = storage.path(request.model_uuid, ModelStorageLocation::Datasets);
    dataset_context.model              = request.model;
    dataset_context.data_manager       = data_manager_;
    const QVariantMap datasets = ModelDatasetOrganizer::organize(dataset_context, &dataset_err);
    if (datasets.isEmpty() && describeModelTask(request.task_type).requires_dataset_export)
        return setError(err_msg, QStringLiteral("数据集组织失败: %1").arg(dataset_err));

    ModelTaskConfigService config_service(project_dir_);
    QString                config_err;
    const QString          config_path = config_service.write(
        request.model_uuid, request.task_type,
        config_service.build(request.model, request.model_name, request.task_type, datasets), &config_err);
    if (config_path.isEmpty())
        return setError(err_msg, config_err);

    const QString log_path =
        cleanModelPath(QDir(storage.path(request.model_uuid, ModelStorageLocation::Logs))
                           .filePath(modelTaskLogStem(request.task_type) + QStringLiteral(".log")));
    if (log_path.isEmpty())
        return setError(err_msg, QStringLiteral("日志路径为空"));

    prepared.task_id           = request.task_id;
    prepared.program           = python_executable;
    prepared.arguments         = {
        script_path,
        QStringLiteral("--config"),
        config_path,
        QStringLiteral("--dltool_task_host"),
        task_manager->taskServerHost(),
        QStringLiteral("--dltool_task_port"),
        QString::number(task_manager->taskServerPort()),
        QStringLiteral("--dltool_task_id"),
        QString::number(request.task_id),
    };
    prepared.working_directory = request.framework.root;
    prepared.python_paths      = request.framework.python_paths;
    prepared.log_path          = log_path;
    return true;
}

bool ModelTaskPreparationService::frameworkHasScript(const ModelManager::FrameworkDefinition &framework,
                                                     ModelTaskType task_type)
{
    return !scriptForTask(framework, task_type).isEmpty();
}

QString ModelTaskPreparationService::scriptForTask(const ModelManager::FrameworkDefinition &framework,
                                                   ModelTaskType task_type)
{
    if (framework.name.isEmpty())
        return {};

    if (task_type == ModelTaskType::Train)
        return framework.train_script;
    if (task_type == ModelTaskType::Test)
        return framework.predict_script;
    return {};
}

} // namespace dltool::model
