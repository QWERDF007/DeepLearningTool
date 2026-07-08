#include "model/ModelTaskPreparationService.h"

#include "common/Utils.h"
#include "data/DataManager.h"
#include "model/IModel.h"
#include "model/ModelDatasetOrganizer.h"
#include "model/ModelDatasetSelection.h"
#include "model/ModelStorageService.h"
#include "model/ModelTaskConfigService.h"
#include "settings/GlobalSettings.h"
#include "settings/SettingsKeys.h"
#include "settings/SettingsValue.h"

#include <QDir>
#include <QFileInfo>
#include <QVariantMap>
#include <utility>

namespace dltool::model {

namespace {

class DataManagerDatasetSource final : public IModelDatasetSource
{
public:
    explicit DataManagerDatasetSource(dltool::data::DataManager &data_manager)
        : data_manager_(data_manager)
    {
    }

    std::vector<int64_t> allImageIds() const override
    {
        return data_manager_.allImageIds();
    }

    qint64 imageDatasetId(qint64 image_id) const override
    {
        return data_manager_.imageDatasetId(image_id);
    }

    QString imagePath(qint64 image_id) const override
    {
        return data_manager_.imagePath(image_id);
    }

    QVariantMap imageLevelLabelData(qint64 image_id) const override
    {
        return data_manager_.getImageLevelLabelData(image_id);
    }

    std::vector<int64_t> imageLabelIds(qint64 image_id) const override
    {
        return data_manager_.imageLabelIds(image_id);
    }

    qint64 labelClassId(qint64 label_id) const override
    {
        return data_manager_.labelClassId(label_id);
    }

    QVariantMap labelData(qint64 label_id) const override
    {
        return data_manager_.labelData(label_id);
    }

    QString labelClassName(qint64 label_class_id) const override
    {
        return data_manager_.labelClassName(label_class_id);
    }

    QString labelClassGroup(qint64 label_class_id) const override
    {
        return data_manager_.labelClassGroup(label_class_id);
    }

    QString datasetName(qint64 dataset_id) const override
    {
        return data_manager_.datasetName(dataset_id);
    }

private:
    dltool::data::DataManager &data_manager_;
};

bool setError(QString *err_msg, const QString &message)
{
    if (err_msg != nullptr)
        *err_msg = message;
    return false;
}

} // namespace

ModelTaskPreparationService::ModelTaskPreparationService(int method, QString project_dir,
                                                         dltool::data::DataManager *data_manager)
    : method_(method)
    , project_dir_(cleanModelPath(std::move(project_dir)))
    , data_manager_(data_manager)
{
}

bool ModelTaskPreparationService::prepare(const ExternalModelTaskRequest &request, ExternalProcessSpec &process_spec,
                                          QString *err_msg) const
{
    process_spec = {};

    if (request.model == nullptr)
        return setError(err_msg, QStringLiteral("模型实例为空"));
    if (request.task_server_host.trimmed().isEmpty() || request.task_server_port == 0)
        return setError(err_msg, QStringLiteral("任务通信端点无效"));

    const QString model_uuid = request.model->uuid().trimmed();
    if (model_uuid.isEmpty())
        return setError(err_msg, QStringLiteral("模型 uuid 为空"));

    ModelStorageService storage(project_dir_);
    QString             storage_err;
    if (!storage.ensureModelStorage(model_uuid, &storage_err))
        return setError(err_msg, QStringLiteral("创建模型目录失败: %1").arg(storage_err));

    const QString script_path = request.framework.scriptFor(request.task_type);
    if (script_path.isEmpty())
    {
        return setError(err_msg, QStringLiteral("框架未定义脚本, 框架: %1, 任务: %2")
                                     .arg(request.model->frameworkName(), modelTaskKey(request.task_type)));
    }
    if (!QFileInfo::exists(script_path))
        return setError(err_msg, QStringLiteral("脚本不存在: %1").arg(script_path));

    namespace generated_field       = dltool::settings::generated::field;
    const QString python_executable = dltool::common::pythonExecutableFromEnvPath(dltool::settings::settingString(
        dltool::settings::GlobalSettings::getInstance(), generated_field::Software::PythonEnvPath));
    if (python_executable.isEmpty())
        return setError(err_msg, QStringLiteral("未配置 Python 环境目录"));

    QVariantMap               datasets;
    const ModelTaskDescriptor task_descriptor = describeModelTask(request.task_type);
    if (task_descriptor.requires_dataset_export)
    {
        if (data_manager_ == nullptr)
            return setError(err_msg, QStringLiteral("数据管理器为空"));

        QString                   dataset_err;
        DataManagerDatasetSource  source(*data_manager_);
        ModelDatasetExportRequest dataset_request;
        dataset_request.method             = method_;
        dataset_request.framework_name     = request.model->frameworkName();
        dataset_request.model_architecture = request.model->modelArchitecture();
        dataset_request.model_uuid         = model_uuid;
        dataset_request.task_type          = request.task_type;
        dataset_request.dataset_dir        = storage.path(model_uuid, ModelStorageLocation::Datasets);
        dataset_request.selections         = modelDatasetSelectionsSnapshot(request.model);
        dataset_request.source             = &source;
        datasets                           = ModelDatasetOrganizer::organize(dataset_request, &dataset_err);
        if (datasets.isEmpty())
            return setError(err_msg, QStringLiteral("数据集组织失败: %1").arg(dataset_err));
    }

    ModelTaskConfigService config_service(project_dir_);
    QString                config_err;
    const QString          config_path = config_service.write(
        model_uuid, request.task_type,
        config_service.build(request.model, request.model_name, request.task_type, datasets), &config_err);
    if (config_path.isEmpty())
        return setError(err_msg, config_err);

    const QString log_path
        = cleanModelPath(QDir(storage.path(model_uuid, ModelStorageLocation::Logs))
                             .filePath(modelTaskLogStem(request.task_type) + QStringLiteral(".log")));
    if (log_path.isEmpty())
        return setError(err_msg, QStringLiteral("日志路径为空"));

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
