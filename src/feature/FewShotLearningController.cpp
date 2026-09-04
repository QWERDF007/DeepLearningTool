#include "feature/FewShotLearningController.h"

#include "common/Utils.h"
#include "core/CoreDef.h"
#include "data/DataManager.h"
#include "data/DataSelectionTreeModel.h"
#include "data/DatasetViewModelFactory.h"
#include "feature/Utils.h"
#include "model/IModel.h"
#include "model/IModelConfig.h"
#include "model/IParams.h"
#include "model/ModelManager.h"
#include "model/ModelStorageService.h"
#include "model/ModelTaskController.h"
#include "model/TaskManager.h"
#include "settings/GlobalSettings.h"
#include "settings/SettingsKeys.h"
#include "settings/SettingsValue.h"
#include "ui/SignalHelper.h"

#include <spdlog/spdlog.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QStringConverter>
#include <QTextStream>
#include <algorithm>
#include <map>
#include <utility>

namespace dltool::feature {

using dltool::common::cleanPath;
using dltool::common::ensureDirectory;
using dltool::common::runtimePath;
using dltool::common::setError;

namespace {

constexpr const char *kFsSam2FrameworkName = "FS-SAM2";
constexpr const char *kFsSam2ModelName     = "FS-SAM2";

struct Sam2ConfigNameParts
{
    QString prefix;
    QString size_token;
};

QVariantList variantListFromViewModel(QObject *view_model, const char *method_name)
{
    if (view_model == nullptr)
        return {};

    QVariantList ids;
    QMetaObject::invokeMethod(view_model, method_name, Q_RETURN_ARG(QVariantList, ids));
    return ids;
}

QVariantList selectedDatasetIdsFromViewModel(QObject *view_model)
{
    QVariantList ids = variantListFromViewModel(view_model, "selectedDatasetIds");
    if (ids.empty())
        ids = variantListFromViewModel(view_model, "selectedIds");
    return ids;
}

QVariantList selectedLabelClassIdsFromViewModel(QObject *view_model)
{
    QVariantList ids = variantListFromViewModel(view_model, "selectedLabelClassIds");
    if (ids.empty())
        ids = variantListFromViewModel(view_model, "selectedIds");
    return ids;
}

dltool::data::DataSelectionTreeModel *selectionTree(QObject *view_model)
{
    return qobject_cast<dltool::data::DataSelectionTreeModel *>(view_model);
}

void selectDatasetClasses(dltool::data::DataSelectionTreeModel *model, const std::vector<int64_t> &dataset_ids,
                          const std::vector<int64_t> &label_class_ids)
{
    if (model == nullptr)
        return;

    model->clearSelection();
    for (int64_t dataset_id : dataset_ids)
    {
        for (int64_t label_class_id : label_class_ids) model->setNodeSelected(dataset_id, label_class_id, true);
    }
}

void selectDatasets(dltool::data::DataSelectionTreeModel *model, const std::vector<int64_t> &dataset_ids)
{
    if (model == nullptr)
        return;

    model->clearSelection();
    for (int64_t dataset_id : dataset_ids) model->setNodeSelected(dataset_id, -1, true);
}

QString fixedSam2ConfigRoot()
{
    return runtimePath(QStringLiteral("python/facebookresearch/sam2/sam2/configs"));
}

Sam2ConfigNameParts sam2ConfigNameParts(const QString &architecture_name)
{
    const QString architecture = architecture_name.trimmed();
    const QString sam21_prefix = QStringLiteral("sam2.1_hiera_");
    const QString sam2_prefix  = QStringLiteral("sam2_hiera_");

    QString prefix;
    QString size_name;
    if (architecture.startsWith(sam21_prefix))
    {
        prefix    = QStringLiteral("sam2.1");
        size_name = architecture.mid(sam21_prefix.size());
    }
    else if (architecture.startsWith(sam2_prefix))
    {
        prefix    = QStringLiteral("sam2");
        size_name = architecture.mid(sam2_prefix.size());
    }
    else
    {
        return {};
    }

    QString size_token;
    if (size_name == QStringLiteral("tiny"))
        size_token = QStringLiteral("t");
    else if (size_name == QStringLiteral("small"))
        size_token = QStringLiteral("s");
    else if (size_name == QStringLiteral("base_plus"))
        size_token = QStringLiteral("b+");
    else if (size_name == QStringLiteral("large"))
        size_token = QStringLiteral("l");

    return size_token.isEmpty() ? Sam2ConfigNameParts{} : Sam2ConfigNameParts{prefix, size_token};
}

QString sam2ConfigPathFromArchitecture(const QString &architecture_name, QString *err_msg)
{
    const Sam2ConfigNameParts parts = sam2ConfigNameParts(architecture_name);
    if (parts.prefix.isEmpty() || parts.size_token.isEmpty())
    {
        setError(err_msg, QString("不支持的 SAM2 架构: %1").arg(architecture_name));
        return {};
    }

    const QString path
        = cleanPath(QDir(fixedSam2ConfigRoot())
                        .filePath(QStringLiteral("%1/%1_hiera_%2.yaml").arg(parts.prefix, parts.size_token)));
    if (!QFileInfo::exists(path))
    {
        setError(err_msg, QString("SAM2 配置文件不存在: %1").arg(path));
        return {};
    }
    return path;
}

bool writeTextFile(const QString &path, const QStringList &lines, QString *err_msg)
{
    QFileInfo file_info(path);
    if (!ensureDirectory(file_info.dir().absolutePath(), err_msg, QString("目录路径为空"), QString("创建目录失败: %1")))
    {
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return setError(err_msg, QString("无法写入文件: %1, %2").arg(path, file.errorString()));

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    for (const QString &line : lines) stream << line << '\n';
    return true;
}

bool isTerminalStatus(const dltool::model::TaskManager::TaskStatus status)
{
    return dltool::model::TaskManager::isTerminal(status);
}

} // namespace

FewShotLearningController::FewShotLearningController(dltool::data::DataManager          *data_manager,
                                                     dltool::model::ModelManager        *model_manager,
                                                     dltool::model::ModelTaskController *model_task_controller,
                                                     dltool::model::TaskManager *task_manager, QObject *parent)
    : QObject(parent)
    , data_manager_(data_manager)
    , model_manager_(model_manager)
    , model_task_controller_(model_task_controller)
    , task_manager_(task_manager)
{
    auto *gs = dltool::settings::GlobalSettings::getInstance();
    enabled_ = gs->valueForField(dltool::settings::generated::field::FewShotLearning::Key::Enabled, true).toBool();

    setTrainDatasetViewModel(dltool::data::DatasetViewModelFactory::createDatasetSelectionModel(data_manager, this));
    setValidationDatasetViewModel(
        dltool::data::DatasetViewModelFactory::createDatasetSelectionModel(data_manager, this));
    setTestDatasetViewModel(dltool::data::DatasetViewModelFactory::createDatasetSelectionModel(data_manager, this));

    if (task_manager_ != nullptr)
    {
        connect(task_manager_, &dltool::model::TaskManager::revisionChanged, this,
                &FewShotLearningController::handleTaskTableRevision);
    }

    connect(gs->catalog(), &dltool::settings::SettingsCatalog::fieldValueChanged, this,
            [this](const QString &group_key, const QString &name, const QVariant &value)
            {
                if (group_key == QStringLiteral("FewShotLearningSettings") && name == QStringLiteral("enabled"))
                {
                    const bool v = value.toBool();
                    if (v != enabled_)
                    {
                        enabled_ = v;
                        emit enabledChanged();
                    }
                }
            });
}

FewShotLearningController::~FewShotLearningController()
{
    disconnectPredictionImport();
}

bool FewShotLearningController::enabled() const
{
    return enabled_;
}

bool FewShotLearningController::running() const
{
    return running_;
}

QString FewShotLearningController::lastError() const
{
    return last_error_;
}

QObject *FewShotLearningController::trainDatasetViewModel() const
{
    return train_dataset_view_model_;
}

void FewShotLearningController::setTrainDatasetViewModel(QObject *view_model)
{
    if (train_dataset_view_model_ == view_model)
        return;
    train_dataset_view_model_ = view_model;
    if (view_model != nullptr && view_model->parent() == nullptr)
        view_model->setParent(this);
    emit trainDatasetViewModelChanged();
}

QObject *FewShotLearningController::validationDatasetViewModel() const
{
    return validation_dataset_view_model_;
}

void FewShotLearningController::setValidationDatasetViewModel(QObject *view_model)
{
    if (validation_dataset_view_model_ == view_model)
        return;
    validation_dataset_view_model_ = view_model;
    if (view_model != nullptr && view_model->parent() == nullptr)
        view_model->setParent(this);
    emit validationDatasetViewModelChanged();
}

QObject *FewShotLearningController::testDatasetViewModel() const
{
    return test_dataset_view_model_;
}

void FewShotLearningController::setTestDatasetViewModel(QObject *view_model)
{
    if (test_dataset_view_model_ == view_model)
        return;
    test_dataset_view_model_ = view_model;
    if (view_model != nullptr && view_model->parent() == nullptr)
        view_model->setParent(this);
    emit testDatasetViewModelChanged();
}

QObject *FewShotLearningController::labelClassViewModel() const
{
    return label_class_view_model_;
}

void FewShotLearningController::setLabelClassViewModel(QObject *view_model)
{
    if (label_class_view_model_ == view_model)
        return;
    label_class_view_model_ = view_model;
    if (view_model != nullptr && view_model->parent() == nullptr)
        view_model->setParent(this);
    emit labelClassViewModelChanged();
}

bool FewShotLearningController::startFsSam2()
{
    QVariantList label_class_ids = selectedLabelClassIdsFromViewModel(train_dataset_view_model_);
    if (label_class_ids.empty())
        label_class_ids = selectedLabelClassIdsFromViewModel(label_class_view_model_);

    return startFsSam2WithIds(selectedDatasetIdsFromViewModel(train_dataset_view_model_),
                              selectedDatasetIdsFromViewModel(validation_dataset_view_model_),
                              selectedDatasetIdsFromViewModel(test_dataset_view_model_), label_class_ids);
}

QString FewShotLearningController::validationError() const
{
    if (running_)
    {
        return QString("小样本学习正在运行");
    }

    QVariantList label_class_ids = selectedLabelClassIdsFromViewModel(train_dataset_view_model_);
    if (label_class_ids.empty())
    {
        label_class_ids = selectedLabelClassIdsFromViewModel(label_class_view_model_);
    }

    return validateStartRequest(
        parseInt64Ids(selectedDatasetIdsFromViewModel(train_dataset_view_model_), true, true),
        parseInt64Ids(selectedDatasetIdsFromViewModel(validation_dataset_view_model_), true, true),
        parseInt64Ids(selectedDatasetIdsFromViewModel(test_dataset_view_model_), true, true),
        parseInt64Ids(label_class_ids, true, true));
}

bool FewShotLearningController::startFsSam2WithIds(const QVariantList &train_dataset_ids,
                                                   const QVariantList &validation_dataset_ids,
                                                   const QVariantList &test_dataset_ids,
                                                   const QVariantList &label_class_ids)
{
    if (running_)
    {
        const QString message = QString("小样本学习正在运行");
        setLastError(message);
        ui::SignalHelper::notifyWarn(QString("小样本学习"), message);
        return false;
    }

    setLastError({});

    QString err_msg;
    if (!startRun(parseInt64Ids(train_dataset_ids, true, true), parseInt64Ids(validation_dataset_ids, true, true),
                  parseInt64Ids(test_dataset_ids, true, true), parseInt64Ids(label_class_ids, true, true), &err_msg))
    {
        const bool already_reported = !err_msg.isEmpty() && last_error_ == err_msg;
        setLastError(err_msg);
        spdlog::error("启动小样本学习失败: {}", err_msg.toUtf8().constData());
        if (!already_reported)
            ui::SignalHelper::notifyError(QString("小样本学习失败"), err_msg);
        return false;
    }

    setRunning(true);
    return true;
}

void FewShotLearningController::clearLastError()
{
    setLastError({});
}

void FewShotLearningController::cancel()
{
    if (!running_)
        return;

    current_run_.stop_requested = true;
    stopRunTasks();
    finishRun(false, QString("小样本学习任务已停止"));
}

bool FewShotLearningController::startRun(const std::vector<int64_t> &train_dataset_ids,
                                         const std::vector<int64_t> &validation_dataset_ids,
                                         const std::vector<int64_t> &test_dataset_ids,
                                         const std::vector<int64_t> &label_class_ids, QString *err_msg)
{
    const QString validation_error
        = validateStartRequest(train_dataset_ids, validation_dataset_ids, test_dataset_ids, label_class_ids);
    if (!validation_error.isEmpty())
    {
        return setError(err_msg, validation_error);
    }

    QString       model_err;
    const QString model_name
        = QString("FS-SAM2_%1").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss_zzz")));
    const dltool::model::ModelManager::ModelRecordView record = model_manager_->addModelRecord(
        model_name, QString::fromUtf8(kFsSam2FrameworkName), QString::fromUtf8(kFsSam2ModelName), &model_err);
    if (!record.isValid())
        return setError(err_msg, model_err.isEmpty() ? QString("创建 FS-SAM2 模型记录失败") : model_err);

    RunState run;
    run.model_uuid = record.uuid;
    if (!configureFsSam2Model(record.uuid, train_dataset_ids, validation_dataset_ids, test_dataset_ids, label_class_ids,
                              run, err_msg))
    {
        model_manager_->deleteModel(record.model_id);
        return false;
    }

    const bool requires_box_to_mask = data_manager_->method() == dltool::core::DeepLearningMethod::Detection;
    if (requires_box_to_mask)
    {
        run.box_to_mask_task_id = addOrdinaryTask(record.uuid, dltool::model::ModelTaskType::BoxToMask, err_msg);
        if (run.box_to_mask_task_id < 0)
        {
            model_manager_->deleteModel(record.model_id);
            return false;
        }
    }

    run.train_task_id   = addOrdinaryTask(record.uuid, dltool::model::ModelTaskType::Train, err_msg);
    run.predict_task_id = addOrdinaryTask(record.uuid, dltool::model::ModelTaskType::Test, err_msg);
    if (run.train_task_id < 0 || run.predict_task_id < 0)
    {
        model_manager_->deleteModel(record.model_id);
        return false;
    }

    current_run_       = std::move(run);
    current_run_.stage = requires_box_to_mask ? RunStage::PreparingMask : RunStage::Training;

    const dltool::model::ModelTaskType first_task_type
        = requires_box_to_mask ? dltool::model::ModelTaskType::BoxToMask : dltool::model::ModelTaskType::Train;
    const int first_task_id = requires_box_to_mask ? current_run_.box_to_mask_task_id : current_run_.train_task_id;
    if (!startOrdinaryTask(first_task_type, first_task_id, err_msg))
    {
        finishRun(false, err_msg != nullptr ? *err_msg : QString());
        return false;
    }
    return true;
}

QString FewShotLearningController::validateStartRequest(const std::vector<int64_t> &train_dataset_ids,
                                                        const std::vector<int64_t> &validation_dataset_ids,
                                                        const std::vector<int64_t> &test_dataset_ids,
                                                        const std::vector<int64_t> &label_class_ids) const
{
    Q_UNUSED(validation_dataset_ids)

    if (!enabled_)
        return QString("小样本学习未启用");
    if (data_manager_ == nullptr)
        return QString("数据管理器未初始化");
    if (model_manager_ == nullptr)
        return QString("模型管理器未初始化");
    if (model_task_controller_ == nullptr)
        return QString("模型任务控制器未初始化");
    if (task_manager_ == nullptr)
        return QString("任务管理器未初始化");

    const int method = data_manager_->method();
    if (method != dltool::core::DeepLearningMethod::Detection
        && method != dltool::core::DeepLearningMethod::Segmentation
        && method != dltool::core::DeepLearningMethod::AnomalyDetection)
    {
        return QString("小样本学习仅支持检测、分割和异常检测项目");
    }
    if (train_dataset_ids.empty())
        return QString("请至少选择一个训练数据集");
    if (test_dataset_ids.empty())
        return QString("请至少选择一个测试数据集");
    if (label_class_ids.empty())
        return QString("请至少选择一个类别");

    auto *settings = dltool::settings::GlobalSettings::getInstance();
    if (settings == nullptr)
        return QString("小样本学习设置未加载");

    namespace generated_field     = dltool::settings::generated::field;
    const QString python_env_path = dltool::settings::GlobalSettings::pythonEnvironmentPath();
    if (python_env_path.trimmed().isEmpty())
        return QString("请先在软件设置中配置 Python 环境目录");

    const QFileInfo python_env_info(cleanPath(python_env_path));
    if (!python_env_info.exists() || !python_env_info.isDir())
        return QString("Python 环境目录无效: %1").arg(python_env_path);

    const QString python_executable = dltool::common::pythonExecutableFromEnvPath(python_env_path);
    if (python_executable.isEmpty() || !QFileInfo(python_executable).isFile())
        return QString("Python 环境中未找到 Python 可执行文件: %1").arg(python_env_path);

    const QString sam2_checkpoint
        = runtimePath(dltool::settings::settingString(settings, generated_field::FewShotLearning::Sam2Checkpoint));
    if (sam2_checkpoint.trimmed().isEmpty())
        return QString("请先配置 SAM2 权重");
    if (!QFileInfo(sam2_checkpoint).isFile())
        return QString("SAM2 checkpoint 不存在: %1").arg(sam2_checkpoint);

    const QString sam2_architecture
        = dltool::settings::settingString(settings, generated_field::FewShotLearning::Sam2Architecture);
    if (sam2_architecture.trimmed().isEmpty())
        return QString("请先配置 SAM2 架构");

    QString       sam2_cfg_error;
    const QString sam2_cfg = sam2ConfigPathFromArchitecture(sam2_architecture, &sam2_cfg_error);
    if (sam2_cfg.isEmpty())
        return sam2_cfg_error;

    return {};
}

bool FewShotLearningController::configureFsSam2Model(const QString              &model_uuid,
                                                     const std::vector<int64_t> &train_dataset_ids,
                                                     const std::vector<int64_t> &validation_dataset_ids,
                                                     const std::vector<int64_t> &test_dataset_ids,
                                                     const std::vector<int64_t> &label_class_ids, RunState &run,
                                                     QString *err_msg)
{
    const dltool::model::ModelManager::ModelRecordView record = model_manager_ != nullptr
                                                                  ? model_manager_->modelRecordViewForUuid(model_uuid)
                                                                  : dltool::model::ModelManager::ModelRecordView{};
    if (!record.isValid() || record.name.trimmed().isEmpty())
        return setError(err_msg, QString("FS-SAM2 模型记录不存在"));

    dltool::model::IModel *model = model_manager_ != nullptr ? model_manager_->modelForUuid(model_uuid) : nullptr;
    if (model == nullptr || model->config() == nullptr)
        return setError(err_msg, QString("无法创建 FS-SAM2 模型实例"));

    auto *train_selection      = selectionTree(model->trainDatasetViewModel());
    auto *validation_selection = selectionTree(model->validationDatasetViewModel());
    auto *test_selection       = selectionTree(model->testDatasetViewModel());
    if (train_selection == nullptr || validation_selection == nullptr || test_selection == nullptr)
        return setError(err_msg, QString("FS-SAM2 模型数据选择模型未初始化"));

    selectDatasetClasses(train_selection, train_dataset_ids, label_class_ids);
    selectDatasetClasses(validation_selection, validation_dataset_ids, label_class_ids);
    selectDatasets(test_selection, test_dataset_ids);

    namespace generated_field = dltool::settings::generated::field;
    auto *settings            = dltool::settings::GlobalSettings::getInstance();

    const QString sam2_checkpoint
        = runtimePath(dltool::settings::settingString(settings, generated_field::FewShotLearning::Sam2Checkpoint));
    if (sam2_checkpoint.trimmed().isEmpty())
        return setError(err_msg, QString("请先配置 SAM2 权重"));
    if (!QFileInfo::exists(sam2_checkpoint))
        return setError(err_msg, QString("SAM2 checkpoint 不存在: %1").arg(sam2_checkpoint));

    const QString sam2_architecture
        = dltool::settings::settingString(settings, generated_field::FewShotLearning::Sam2Architecture);
    if (sam2_architecture.trimmed().isEmpty())
        return setError(err_msg, QString("请先配置 SAM2 架构"));

    QString       sam2_cfg_err;
    const QString sam2_cfg = sam2ConfigPathFromArchitecture(sam2_architecture, &sam2_cfg_err);
    if (sam2_cfg.isEmpty())
        return setError(err_msg, sam2_cfg_err);

    const int kshot
        = std::clamp(dltool::settings::settingInt(settings, generated_field::FewShotLearning::Kshot, 1), 1, 16);
    const int epochs
        = std::clamp(dltool::settings::settingInt(settings, generated_field::FewShotLearning::Epochs, 50), 1, 10000);
    const int batch_size
        = std::clamp(dltool::settings::settingInt(settings, generated_field::FewShotLearning::BatchSize, 2), 1, 128);
    const int num_workers
        = std::clamp(dltool::settings::settingInt(settings, generated_field::FewShotLearning::NumWorkers, 0), 0, 128);
    const int image_size = std::clamp(
        dltool::settings::settingInt(settings, generated_field::FewShotLearning::ImageSize, 1024), 64, 8192);
    const double learning_rate
        = dltool::settings::settingDouble(settings, generated_field::FewShotLearning::LearningRate, 1e-4);
    const double weight_decay
        = dltool::settings::settingDouble(settings, generated_field::FewShotLearning::WeightDecay, 1e-6);
    const bool prediction_enhancement_enabled = dltool::settings::settingBool(
        settings, generated_field::FewShotLearning::PredictionEnhancementEnabled, false);
    const bool box_to_mask_prediction_enhancement_enabled = dltool::settings::settingBool(
        settings, generated_field::FewShotLearning::BoxToMaskPredictionEnhancementEnabled, false);
    const bool prediction_horizontal_flip
        = dltool::settings::settingBool(settings, generated_field::FewShotLearning::PredictionHorizontalFlip, false);
    const bool prediction_vertical_flip
        = dltool::settings::settingBool(settings, generated_field::FewShotLearning::PredictionVerticalFlip, false);
    const double prediction_scale = std::clamp(
        dltool::settings::settingDouble(settings, generated_field::FewShotLearning::PredictionScale, 0.0), -0.9, 1.0);
    const double prediction_brightness = std::clamp(
        dltool::settings::settingDouble(settings, generated_field::FewShotLearning::PredictionBrightness, 0.0), -1.0,
        1.0);
    const double prediction_contrast = std::clamp(
        dltool::settings::settingDouble(settings, generated_field::FewShotLearning::PredictionContrast, 0.0), -1.0,
        1.0);
    const double prediction_hue = std::clamp(
        dltool::settings::settingDouble(settings, generated_field::FewShotLearning::PredictionHue, 0.0), -0.5, 0.5);
    const double prediction_rotation = std::clamp(
        dltool::settings::settingDouble(settings, generated_field::FewShotLearning::PredictionRotation, 0.0), -180.0,
        180.0);
    const double prediction_iou_threshold = std::clamp(
        dltool::settings::settingDouble(settings, generated_field::FewShotLearning::PredictionIouThreshold, 0.5), 0.0,
        1.0);
    const int prediction_min_vote_count
        = dltool::settings::settingInt(settings, generated_field::FewShotLearning::PredictionMinVoteCount, 2);

    const dltool::model::ModelStorageService storage(model_manager_->projectDirectory());
    const QString output_dir = storage.testTaskPredictionPath(record.name, QString("fs_sam2"));
    const QString checkpoint_path
        = cleanPath(QDir(storage.trainWeightsPath(record.name)).filePath(QStringLiteral("fs_sam2/best_model.pt")));
    QString dir_err;
    if (!ensureDirectory(output_dir, &dir_err, QString("目录路径为空"), QString("创建目录失败: %1")))
        return setError(err_msg, dir_err);

    QVariantMap train_model;
    train_model.insert(QStringLiteral("sam2_checkpoint"), sam2_checkpoint);
    train_model.insert(QStringLiteral("sam2_cfg"), sam2_cfg);
    train_model.insert(QStringLiteral("box_to_mask_prediction_enhancement_enabled"),
                       box_to_mask_prediction_enhancement_enabled);
    train_model.insert(QStringLiteral("prediction_horizontal_flip"), prediction_horizontal_flip);
    train_model.insert(QStringLiteral("prediction_vertical_flip"), prediction_vertical_flip);
    train_model.insert(QStringLiteral("prediction_scale"), prediction_scale);
    train_model.insert(QStringLiteral("prediction_brightness"), prediction_brightness);
    train_model.insert(QStringLiteral("prediction_contrast"), prediction_contrast);
    train_model.insert(QStringLiteral("prediction_hue"), prediction_hue);
    train_model.insert(QStringLiteral("prediction_rotation"), prediction_rotation);
    train_model.insert(QStringLiteral("prediction_iou_threshold"), prediction_iou_threshold);
    train_model.insert(QStringLiteral("prediction_min_vote_count"), prediction_min_vote_count);

    QVariantMap training;
    training.insert(QStringLiteral("kshot"), kshot);
    training.insert(QStringLiteral("epochs"), epochs);
    training.insert(QStringLiteral("batch_size"), batch_size);
    training.insert(QStringLiteral("num_workers"), num_workers);
    training.insert(QStringLiteral("learning_rate"), learning_rate);
    training.insert(QStringLiteral("weight_decay"), weight_decay);

    QVariantMap network;
    network.insert(QStringLiteral("image_size"), image_size);

    QVariantMap train_params;
    train_params.insert(QStringLiteral("model"), train_model);
    train_params.insert(QStringLiteral("training"), training);
    train_params.insert(QStringLiteral("network"), network);

    QVariantMap inference;
    inference.insert(QStringLiteral("checkpoint_path"), checkpoint_path);
    inference.insert(QStringLiteral("output_dir"), output_dir);
    inference.insert(QStringLiteral("kshot"), kshot);
    inference.insert(QStringLiteral("image_size"), image_size);
    inference.insert(QStringLiteral("prediction_enhancement_enabled"), prediction_enhancement_enabled);
    inference.insert(QStringLiteral("prediction_horizontal_flip"), prediction_horizontal_flip);
    inference.insert(QStringLiteral("prediction_vertical_flip"), prediction_vertical_flip);
    inference.insert(QStringLiteral("prediction_scale"), prediction_scale);
    inference.insert(QStringLiteral("prediction_brightness"), prediction_brightness);
    inference.insert(QStringLiteral("prediction_contrast"), prediction_contrast);
    inference.insert(QStringLiteral("prediction_hue"), prediction_hue);
    inference.insert(QStringLiteral("prediction_rotation"), prediction_rotation);
    inference.insert(QStringLiteral("prediction_iou_threshold"), prediction_iou_threshold);
    inference.insert(QStringLiteral("prediction_min_vote_count"), prediction_min_vote_count);

    QVariantMap test_params;
    test_params.insert(QStringLiteral("model"), train_model);
    test_params.insert(QStringLiteral("inference"), inference);
    train_params.insert(QStringLiteral("inference"), inference);

    dltool::model::IModelConfig *config = model->config();
    if (config->trainParams() == nullptr || config->testParams() == nullptr)
        return setError(err_msg, QString("FS-SAM2 模型参数未初始化"));

    config->trainParams()->setValuesMap(train_params);
    config->testParams()->setValuesMap(test_params);

    run.output_dir = output_dir;
    return writePredictionImportTargets(model_uuid, test_dataset_ids, run, err_msg);
}

bool FewShotLearningController::writePredictionImportTargets(const QString              &model_uuid,
                                                             const std::vector<int64_t> &test_dataset_ids,
                                                             RunState &run, QString *err_msg) const
{
    if (data_manager_ == nullptr || model_manager_ == nullptr)
        return setError(err_msg, QString("数据管理器或模型管理器未初始化"));

    const dltool::model::ModelManager::ModelRecordView record = model_manager_->modelRecordViewForUuid(model_uuid);
    if (!record.isValid() || record.name.trimmed().isEmpty())
        return setError(err_msg, QString("FS-SAM2 模型记录不存在"));

    const dltool::model::ModelStorageService storage(model_manager_->projectDirectory());
    const QString task_root = storage.testTaskRoot(record.name, QString("fs_sam2"));
    if (!ensureDirectory(task_root, err_msg, QString("目录路径为空"), QString("创建 FS-SAM2 任务目录失败: %1")))
        return false;

    std::map<int64_t, QStringList> lines_by_dataset;
    for (int64_t dataset_id : test_dataset_ids) lines_by_dataset[dataset_id] = {};

    const std::vector<int64_t> test_image_ids
        = data_manager_->imageIdsForDatasets(test_dataset_ids);
    for (int64_t image_id : test_image_ids)
    {
        const int64_t dataset_id = data_manager_->imageDatasetId(image_id);
        const QString image_path = data_manager_->imagePath(image_id);
        if (image_path.trimmed().isEmpty())
            continue;

        lines_by_dataset[dataset_id].push_back(QStringLiteral("%1,%2").arg(image_id).arg(cleanPath(image_path)));
    }

    run.import_targets.clear();
    for (int64_t dataset_id : test_dataset_ids)
    {
        const QStringList lines = lines_by_dataset[dataset_id];
        if (lines.empty())
        {
            const QString dataset_name = data_manager_->datasetName(dataset_id);
            return setError(err_msg, QString("测试数据集 %1 没有图像")
                                         .arg(dataset_name.isEmpty() ? QString::number(dataset_id) : dataset_name));
        }

        const QString import_file_list_path
            = cleanPath(QDir(task_root).filePath(QStringLiteral("prediction_import_%1.txt").arg(dataset_id)));
        if (!writeTextFile(import_file_list_path, lines, err_msg))
            return false;
        run.import_targets.push_back(PredictionImportTarget{dataset_id, import_file_list_path});
    }
    return true;
}

int FewShotLearningController::addOrdinaryTask(const QString &model_uuid, dltool::model::ModelTaskType task_type,
                                               QString *err_msg) const
{
    if (model_task_controller_ == nullptr)
    {
        setError(err_msg, QString("模型任务控制器未初始化"));
        return -1;
    }

    const int task_id = model_task_controller_->addModelTask(model_uuid, task_type);
    if (task_id < 0)
        setError(err_msg, QString("创建模型任务失败: %1").arg(dltool::model::modelTaskDisplayName(task_type)));
    return task_id;
}

bool FewShotLearningController::startOrdinaryTask(dltool::model::ModelTaskType task_type, int expected_task_id,
                                                  QString *err_msg)
{
    if (model_task_controller_ == nullptr)
        return setError(err_msg, QString("模型任务控制器未初始化"));
    if (current_run_.model_uuid.trimmed().isEmpty())
        return setError(err_msg, QString("小样本学习模型 uuid 为空"));

    const int task_id = model_task_controller_->startModelTask(current_run_.model_uuid, task_type);
    if (task_id < 0)
        return setError(err_msg, QString("启动模型任务失败: %1").arg(dltool::model::modelTaskDisplayName(task_type)));
    if (expected_task_id >= 0 && task_id != expected_task_id)
    {
        return setError(err_msg, QString("启动模型任务不匹配: 期望 %1, 实际 %2").arg(expected_task_id).arg(task_id));
    }
    return true;
}

void FewShotLearningController::handleTaskTableRevision()
{
    if (!running_ || current_run_.stage == RunStage::Idle || task_manager_ == nullptr)
    {
        return;
    }

    using Status = dltool::model::TaskManager::TaskStatus;

    if (current_run_.stage == RunStage::PreparingMask)
    {
        const auto *task = task_manager_->findTask(current_run_.box_to_mask_task_id);
        if (task == nullptr)
        {
            finishRun(false, QString("BoxToMask 任务不存在"));
            return;
        }
        if (!isTerminalStatus(task->status))
            return;
        if (task->status == Status::Finished)
        {
            advanceFinishedTask(dltool::model::ModelTaskType::Train, current_run_.train_task_id, RunStage::Training);
            return;
        }
        finishRun(false,
                  task->status == Status::Stopped ? QString("小样本学习任务已停止") : QString("BoxToMask 任务失败"));
        return;
    }

    if (current_run_.stage == RunStage::Training)
    {
        const auto *task = task_manager_->findTask(current_run_.train_task_id);
        if (task == nullptr)
        {
            finishRun(false, QString("小样本训练任务不存在"));
            return;
        }
        if (!isTerminalStatus(task->status))
            return;
        if (task->status == Status::Finished)
        {
            advanceFinishedTask(dltool::model::ModelTaskType::Test, current_run_.predict_task_id, RunStage::Predicting);
            return;
        }
        finishRun(false,
                  task->status == Status::Stopped ? QString("小样本学习任务已停止") : QString("小样本训练任务失败"));
        return;
    }

    if (current_run_.stage == RunStage::Predicting)
    {
        const auto *task = task_manager_->findTask(current_run_.predict_task_id);
        if (task == nullptr)
        {
            finishRun(false, QString("小样本推理任务不存在"));
            return;
        }
        if (!isTerminalStatus(task->status))
            return;
        if (task->status == Status::Finished)
        {
            finishRun(true);
            return;
        }
        finishRun(false,
                  task->status == Status::Stopped ? QString("小样本学习任务已停止") : QString("小样本推理任务失败"));
    }
}

void FewShotLearningController::advanceFinishedTask(dltool::model::ModelTaskType next_task_type, int next_task_id,
                                                    RunStage next_stage)
{
    current_run_.stage = next_stage;
    QString err_msg;
    if (!startOrdinaryTask(next_task_type, next_task_id, &err_msg))
        finishRun(false, err_msg);
}

void FewShotLearningController::finishRun(bool success, const QString &message)
{
    std::vector<PredictionImportTarget> import_targets;
    QString                             output_dir;
    if (success)
    {
        import_targets = current_run_.import_targets;
        output_dir     = current_run_.output_dir;
    }

    if (!success)
    {
        const QString err_msg = message.isEmpty() ? QString("小样本学习任务失败") : message;
        stopRunTasks();
        setLastError(err_msg);
        spdlog::error("小样本学习任务失败: {}", err_msg.toUtf8().constData());
    }

    current_run_ = {};
    setRunning(false);

    if (success)
    {
        startPredictionImports(std::move(import_targets), output_dir);
    }
    else
    {
        ui::SignalHelper::notifyError(QString("小样本学习失败"),
                                      message.isEmpty() ? QString("小样本学习任务失败") : message);
    }
}

void FewShotLearningController::stopRunTasks()
{
    if (model_task_controller_ == nullptr || current_run_.model_uuid.trimmed().isEmpty())
        return;

    if (current_run_.box_to_mask_task_id >= 0)
        model_task_controller_->stopModelTask(current_run_.model_uuid, dltool::model::ModelTaskType::BoxToMask);
    if (current_run_.train_task_id >= 0)
        model_task_controller_->stopModelTask(current_run_.model_uuid, dltool::model::ModelTaskType::Train);
    if (current_run_.predict_task_id >= 0)
        model_task_controller_->stopModelTask(current_run_.model_uuid, dltool::model::ModelTaskType::Test);
}

void FewShotLearningController::startPredictionImports(std::vector<PredictionImportTarget> targets,
                                                       const QString                      &output_dir)
{
    if (data_manager_ == nullptr || targets.empty() || output_dir.trimmed().isEmpty())
    {
        spdlog::info("小样本学习完成");
        ui::SignalHelper::notifySuccess(QString("小样本学习完成"), QString("小样本学习任务完成"), 4500);
        return;
    }

    disconnectPredictionImport();
    pending_import_targets_       = std::move(targets);
    pending_import_output_dir_    = output_dir;
    current_import_index_         = 0;
    prediction_import_connection_ = data_manager_->connectImportFinished(
        this, [this](bool success, const QString &message) { handlePredictionImportFinished(success, message); });
    startNextPredictionImport();
}

void FewShotLearningController::startNextPredictionImport()
{
    if (data_manager_ == nullptr)
    {
        disconnectPredictionImport();
        const QString message = QString("数据管理器未初始化，无法导入小样本预测结果");
        setLastError(message);
        spdlog::error("{}", message.toUtf8().constData());
        ui::SignalHelper::notifyError(QString("小样本学习失败"), message);
        return;
    }
    if (current_import_index_ >= static_cast<int>(pending_import_targets_.size()))
    {
        const int imported_count = current_import_index_;
        disconnectPredictionImport();
        const QString message = QString("小样本学习完成，已导入 %1 个测试数据集的预测结果").arg(imported_count);
        spdlog::info("{}", message.toUtf8().constData());
        ui::SignalHelper::notifySuccess(QString("小样本学习完成"), message, 4500);
        return;
    }

    const PredictionImportTarget &target = pending_import_targets_.at(static_cast<size_t>(current_import_index_));
    data_manager_->importMaskData(target.dataset_id, target.file_list_path, pending_import_output_dir_);
}

void FewShotLearningController::handlePredictionImportFinished(bool success, const QString &message)
{
    if (!success)
    {
        const QString detail = message.isEmpty() ? QString("导入小样本预测结果失败") : message;
        spdlog::error("导入小样本预测结果失败: {}", detail.toUtf8().constData());
        setLastError(detail);
        disconnectPredictionImport();
        ui::SignalHelper::notifyError(QString("小样本学习失败"),
                                      QString("小样本学习完成，但预测结果导入失败: %1").arg(detail));
        return;
    }

    if (current_import_index_ >= 0 && current_import_index_ < static_cast<int>(pending_import_targets_.size()))
        QFile::remove(pending_import_targets_.at(static_cast<size_t>(current_import_index_)).file_list_path);
    ++current_import_index_;
    startNextPredictionImport();
}

void FewShotLearningController::disconnectPredictionImport()
{
    if (data_manager_ != nullptr && prediction_import_connection_)
        data_manager_->disconnectImportFinished(prediction_import_connection_);
    prediction_import_connection_ = {};
    pending_import_targets_.clear();
    pending_import_output_dir_.clear();
    current_import_index_ = 0;
}

void FewShotLearningController::setRunning(bool running)
{
    if (running_ == running)
        return;
    running_ = running;
    emit runningChanged();
}

void FewShotLearningController::setLastError(const QString &last_error)
{
    if (last_error_ == last_error)
        return;
    last_error_ = last_error;
    emit lastErrorChanged();
}

} // namespace dltool::feature
