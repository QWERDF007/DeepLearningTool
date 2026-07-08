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

using dltool::common::cleanPath;

namespace dltool::model {
using common::setError;

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

} // namespace

ModelTaskPreparationService::ModelTaskPreparationService(int method, QString project_dir,
                                                         dltool::data::DataManager *data_manager)
    : method_(method)
    , project_dir_(cleanPath(project_dir))
    , data_manager_(data_manager)
{
}

bool ModelTaskPreparationService::prepare(const ModelTaskContext &context, ExternalProcessSpec &process_spec,
                                          QString *err_msg) const
{
    process_spec = {};

    if (context.model == nullptr)
        return setError(err_msg, QString("模型实例为空"));
    if (context.task_server_host.trimmed().isEmpty() || context.task_server_port == 0)
        return setError(err_msg, QString("任务通信端点无效"));

    const QString model_uuid
        = context.model_uuid.trimmed().isEmpty() ? context.model->uuid().trimmed() : context.model_uuid.trimmed();
    if (model_uuid.isEmpty())
        return setError(err_msg, QString("模型 uuid 为空"));

    ModelStorageService storage(project_dir_);
    QString             storage_err;
    if (!storage.ensureModelStorage(model_uuid, &storage_err))
        return setError(err_msg, QString("创建模型目录失败: %1").arg(storage_err));

    const QString script_path = context.framework.scriptFor(context.task_type);
    if (script_path.isEmpty())
    {
        return setError(err_msg, QString("框架未定义脚本, 框架: %1, 任务: %2")
                                     .arg(context.model->frameworkName(), modelTaskKey(context.task_type)));
    }
    if (!QFileInfo::exists(script_path))
        return setError(err_msg, QString("脚本不存在: %1").arg(script_path));

    namespace generated_field       = dltool::settings::generated::field;
    const QString python_executable = dltool::common::pythonExecutableFromEnvPath(dltool::settings::settingString(
        dltool::settings::GlobalSettings::getInstance(), generated_field::Software::PythonEnvPath));
    if (python_executable.isEmpty())
        return setError(err_msg, QString("未配置 Python 环境目录"));

    QVariantMap               datasets;
    const ModelTaskDescriptor task_descriptor = describeModelTask(context.task_type);
    if (task_descriptor.requires_dataset_export)
    {
        if (data_manager_ == nullptr)
            return setError(err_msg, QString("数据管理器为空"));

        QString                      dataset_err;
        DataManagerDatasetSource     source(*data_manager_);
        const ModelDatasetSelections selections  = modelDatasetSelectionsSnapshot(context.model);
        const QString                dataset_dir = storage.path(model_uuid, ModelStorageLocation::Datasets);
        if (!writeModelDatasetSelectionsFile(dataset_dir, selections, &dataset_err))
            return setError(err_msg, QString("写入数据集选择配置失败: %1").arg(dataset_err));

        ModelDatasetExportRequest dataset_request;
        dataset_request.method             = method_;
        dataset_request.framework_name     = context.model->frameworkName();
        dataset_request.model_architecture = context.model->modelArchitecture();
        dataset_request.model_uuid         = model_uuid;
        dataset_request.task_type          = context.task_type;
        dataset_request.dataset_dir        = dataset_dir;
        dataset_request.selections         = selections;
        dataset_request.source             = &source;
        datasets                           = ModelDatasetOrganizer::organize(dataset_request, &dataset_err);
        if (datasets.isEmpty())
            return setError(err_msg, QString("数据集组织失败: %1").arg(dataset_err));
    }

    ModelTaskConfigService config_service(project_dir_);
    QString                config_err;
    const QString          config_path = config_service.write(
        model_uuid, context.task_type,
        config_service.build(context.model, context.model_name, context.task_type, datasets), &config_err);
    if (config_path.isEmpty())
        return setError(err_msg, config_err);

    const QString log_path = cleanPath(QDir(storage.path(model_uuid, ModelStorageLocation::Logs))
                                           .filePath(modelTaskLogStem(context.task_type) + QStringLiteral(".log")));
    if (log_path.isEmpty())
        return setError(err_msg, QString("日志路径为空"));

    process_spec.task_id   = context.task_id;
    process_spec.program   = python_executable;
    process_spec.arguments = {
        script_path,
        QStringLiteral("--config"),
        config_path,
        QStringLiteral("--dltool_task_host"),
        context.task_server_host,
        QStringLiteral("--dltool_task_port"),
        QString::number(context.task_server_port),
        QStringLiteral("--dltool_task_id"),
        QString::number(context.task_id),
    };
    process_spec.working_directory = context.framework.root;
    process_spec.python_paths      = context.framework.python_paths;
    process_spec.log_path          = log_path;
    return true;
}

} // namespace dltool::model
