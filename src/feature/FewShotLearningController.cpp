#include "feature/FewShotLearningController.h"

#include "core/CoreDef.h"
#include "feature/FewShotLearningDataProvider.h"
#include "feature/Utils.h"
#include "model/TaskManager.h"
#include "settings/GlobalSettings.h"
#include "settings/SettingsKeys.h"
#include "settings/SettingsValue.h"

#include <spdlog/spdlog.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPointF>
#include <QPolygonF>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTextStream>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace dltool::feature {

namespace {

struct ClassBuildData
{
    int64_t                       label_class_id{-1};
    QString                       label_class_name;
    QString                       class_dir_name;
    std::map<int64_t, QImage>     masks_by_image_id;
    std::map<int64_t, QJsonArray> boxes_by_image_id;
};

struct PredictionImportTarget
{
    int64_t dataset_id{-1};
    QString manifest_path;
};

QString cleanPath(const QString &path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
}

QString runtimePath(const QString &path)
{
    const QString cleaned = cleanPath(path);
    if (cleaned.isEmpty() || QFileInfo(cleaned).isAbsolute())
        return cleaned;
    return cleanPath(QDir(QCoreApplication::applicationDirPath()).filePath(cleaned));
}

QString fixedFsSam2Root()
{
    return runtimePath(QStringLiteral("python/fornib/FS-SAM2"));
}

QString fixedSam2ConfigRoot()
{
    return runtimePath(QStringLiteral("python/facebookresearch/sam2/sam2/configs"));
}

QString sanitizeFileName(QString value, const QString &fallback)
{
    value = value.trimmed();
    if (value.isEmpty())
        value = fallback;
    value.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")), QStringLiteral("_"));
    value.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral("_"));
    return value.isEmpty() ? fallback : value;
}

using dltool::settings::settingDouble;
using dltool::settings::settingInt;
using dltool::settings::settingString;

QString pythonExecutableFromEnvPath(const QString &env_path)
{
    const QFileInfo info(cleanPath(env_path));
    if (info.isFile())
        return info.absoluteFilePath();

    const QDir        dir(info.absoluteFilePath());
    const QStringList candidates = {
        QStringLiteral("python.exe"),
        QStringLiteral("Scripts/python.exe"),
        QStringLiteral("bin/python"),
        QStringLiteral("python"),
    };
    for (const QString &candidate : candidates)
    {
        const QString path = dir.filePath(candidate);
        if (QFileInfo::exists(path))
            return cleanPath(path);
    }
    return {};
}

bool ensureDir(const QString &path, QString &err_msg)
{
    QDir dir(path);
    if (dir.exists())
        return true;
    if (!dir.mkpath(QStringLiteral(".")))
    {
        err_msg = QString("无法创建目录: %1").arg(path);
        return false;
    }
    return true;
}

bool writeTextFile(const QString &path, const QStringList &lines, QString &err_msg)
{
    QFileInfo info(path);
    if (!ensureDir(info.dir().absolutePath(), err_msg))
        return false;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        err_msg = QString("无法写入文件: %1, %2").arg(path, file.errorString());
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    for (const QString &line : lines) stream << line << '\n';
    return true;
}

bool copyFile(const QString &source_path, const QString &target_path, QString &err_msg)
{
    QFileInfo target_info(target_path);
    if (!ensureDir(target_info.dir().absolutePath(), err_msg))
        return false;

    if (QFile::exists(target_path) && !QFile::remove(target_path))
    {
        err_msg = QString("无法覆盖文件: %1").arg(target_path);
        return false;
    }

    if (!QFile::copy(source_path, target_path))
    {
        err_msg = QString("复制文件失败: %1 -> %2").arg(source_path, target_path);
        return false;
    }
    return true;
}

bool copyImageToAlias(const QString &source_path, const QString &target_dir, const QString &alias, QString &copied_path,
                      QString &err_msg)
{
    const QFileInfo source_info(source_path);
    QString         suffix = source_info.suffix().toLower();
    if (suffix.isEmpty())
        suffix = QStringLiteral("png");

    copied_path = QDir(target_dir).filePath(QString("%1.%2").arg(alias, suffix));
    return copyFile(source_path, copied_path, err_msg);
}

std::vector<QPointF> variantListToPoints(const QVariant &value)
{
    std::vector<QPointF> points;
    const QVariantList   list = value.toList();
    points.reserve(static_cast<size_t>(list.size()));

    for (const QVariant &item : list)
    {
        if (item.canConvert<QVariantMap>())
        {
            const QVariantMap map = item.toMap();
            points.emplace_back(map.value(QStringLiteral("x")).toDouble(), map.value(QStringLiteral("y")).toDouble());
        }
        else if (item.canConvert<QVariantList>())
        {
            const QVariantList pair = item.toList();
            if (pair.size() >= 2)
                points.emplace_back(pair[0].toDouble(), pair[1].toDouble());
        }
    }

    return points;
}

bool getImageDimensions(const QString &image_path, int &width, int &height)
{
    QImageReader reader(image_path);
    const QSize  size = reader.size();
    if (!size.isValid())
    {
        spdlog::warn("无法读取图像尺寸: {}, 错误: {}", image_path.toUtf8().constData(),
                     reader.errorString().toUtf8().constData());
        return false;
    }

    width  = size.width();
    height = size.height();
    return true;
}

QPolygonF variantPointsToPolygon(const QVariant &value)
{
    QPolygonF polygon;
    for (const QPointF &point : variantListToPoints(value)) polygon << point;
    return polygon;
}

void paintLabelToMask(QImage &mask, const QVariantMap &label_data)
{
    if (mask.isNull())
        return;

    QPainter painter(&mask);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);

    const QPolygonF polygon = variantPointsToPolygon(label_data.value(QStringLiteral("points")));
    if (polygon.size() >= 3)
    {
        painter.drawPolygon(polygon);
        return;
    }

    const QRectF rect(
        label_data.value(QStringLiteral("x")).toDouble(), label_data.value(QStringLiteral("y")).toDouble(),
        label_data.value(QStringLiteral("width")).toDouble(), label_data.value(QStringLiteral("height")).toDouble());
    if (rect.width() > 0 && rect.height() > 0)
        painter.fillRect(rect, Qt::white);
}

enum class FsSam2Script
{
    Train,
    Predict,
    BoxToMask,
};

enum class FewShotTaskKind
{
    Train,
    Predict,
    BoxToMask,
};

enum class FewShotClassField
{
    Id,
    Name,
    Dir,
};

QString fsSam2ScriptName(FsSam2Script script)
{
    switch (script)
    {
    case FsSam2Script::Train:
        return QStringLiteral("train.py");
    case FsSam2Script::Predict:
        return QStringLiteral("predict.py");
    case FsSam2Script::BoxToMask:
        return QStringLiteral("box_to_mask.py");
    default:
        return {};
    }
}

QString fsSam2ScriptPath(const QString &fs_sam2_root, FsSam2Script script)
{
    return cleanPath(QDir(fs_sam2_root).filePath(fsSam2ScriptName(script)));
}

QString fewShotTaskName(FewShotTaskKind kind)
{
    switch (kind)
    {
    case FewShotTaskKind::Train:
        return QString("小样本学习 训练");
    case FewShotTaskKind::Predict:
        return QString("小样本学习 推理");
    case FewShotTaskKind::BoxToMask:
        return QString("小样本学习 框转Mask");
    default:
        return {};
    }
}

QString fewShotTaskType(FewShotTaskKind kind)
{
    switch (kind)
    {
    case FewShotTaskKind::Train:
        return QStringLiteral("few-shot-train");
    case FewShotTaskKind::Predict:
        return QStringLiteral("few-shot-predict");
    case FewShotTaskKind::BoxToMask:
        return QStringLiteral("few-shot-box-to-mask");
    default:
        return {};
    }
}

QString fewShotClassFieldName(FewShotClassField field)
{
    switch (field)
    {
    case FewShotClassField::Id:
        return QStringLiteral("id");
    case FewShotClassField::Name:
        return QStringLiteral("name");
    case FewShotClassField::Dir:
        return QStringLiteral("dir");
    default:
        return {};
    }
}

struct Sam2ConfigNameParts
{
    QString prefix;
    QString size_token;
};

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
    {
        size_token = QStringLiteral("t");
    }
    else if (size_name == QStringLiteral("small"))
    {
        size_token = QStringLiteral("s");
    }
    else if (size_name == QStringLiteral("base_plus"))
    {
        size_token = QStringLiteral("b+");
    }
    else if (size_name == QStringLiteral("large"))
    {
        size_token = QStringLiteral("l");
    }

    return size_token.isEmpty() ? Sam2ConfigNameParts{} : Sam2ConfigNameParts{prefix, size_token};
}

QString sam2ConfigPathFromArchitecture(const QString &architecture_name, QString &err_msg)
{
    const Sam2ConfigNameParts parts = sam2ConfigNameParts(architecture_name);
    if (parts.prefix.isEmpty() || parts.size_token.isEmpty())
    {
        err_msg = QString("不支持的 SAM2 架构: %1").arg(architecture_name);
        return {};
    }

    const QString path = cleanPath(
        QDir(fixedSam2ConfigRoot()).filePath(QString("%1/%1_hiera_%2.yaml").arg(parts.prefix, parts.size_token)));
    if (!QFileInfo::exists(path))
    {
        err_msg = QString("SAM2 配置文件不存在: %1").arg(path);
        return {};
    }
    return path;
}

QString checkpointPath(const QString &fs_sam2_root, const QString &logpath)
{
    return cleanPath(QDir(fs_sam2_root).filePath(QString("logs/%1.log/best_model.pt").arg(logpath)));
}

} // namespace

struct FewShotLearningController::RunContext
{
    QString python_executable;
    QString fs_sam2_root;
    QString sam2_checkpoint;
    QString sam2_cfg;
    QString run_dir;
    QString custom_dataset_dir;
    QString query_dir;
    QString output_dir;
    QString query_txt_path;
    QString train_script;
    QString predict_script;
    QString box_to_mask_script;
    int     box_to_mask_task_id{-1};
    QString checkpoint_path;
    QString exp_id;
    QString logpath;

    int    kshot{1};
    int    epochs{50};
    int    batch_size{2};
    int    num_workers{0};
    int    image_size{1024};
    double lr{1e-4};
    double weight_decay{1e-6};
    double support_ratio{0.5};

    int     train_task_id{-1};
    int     predict_task_id{-1};
    QString task_host;
    quint16 task_port{0};

    QJsonArray                          classes;
    std::vector<PredictionImportTarget> import_targets;
};

FewShotLearningController::FewShotLearningController(FewShotLearningDataProvider *data_provider, QObject *parent)
    : QObject(parent)
    , data_provider_(data_provider)
{
}

FewShotLearningController::~FewShotLearningController()
{
    if (process_ != nullptr)
    {
        process_->kill();
        process_->deleteLater();
        process_ = nullptr;
    }
}

bool FewShotLearningController::running() const
{
    return running_;
}

QString FewShotLearningController::lastError() const
{
    return last_error_;
}

void FewShotLearningController::setTaskManager(dltool::model::TaskManager *task_manager)
{
    if (task_manager_ == task_manager)
        return;
    if (task_manager_)
        disconnect(task_manager_, &dltool::model::TaskManager::taskStopRequested, this,
                   &FewShotLearningController::handleTaskStopRequested);
    task_manager_ = task_manager;
    if (task_manager_)
        connect(task_manager_, &dltool::model::TaskManager::taskStopRequested, this,
                &FewShotLearningController::handleTaskStopRequested);
}

bool FewShotLearningController::startFsSam2(const QVariantList &train_dataset_ids, const QVariantList &test_dataset_ids,
                                            const QVariantList &label_class_ids)
{
    if (running_)
        return false;

    setLastError({});
    stop_requested_ = false;

    RunContext context;
    QString    err_msg;
    if (!prepareRun(parseInt64Ids(train_dataset_ids, true, true), parseInt64Ids(test_dataset_ids, true, true),
                    parseInt64Ids(label_class_ids, true, true), context, err_msg))
    {
        setLastError(err_msg);
        spdlog::error("启动小样本学习失败: {}", err_msg.toUtf8().constData());
        return false;
    }

    active_context_                   = std::make_unique<RunContext>(std::move(context));
    const RunContext &started_context = *active_context_;
    train_task_id_                    = started_context.train_task_id;
    predict_task_id_                  = started_context.predict_task_id;
    box_to_mask_task_id_              = started_context.box_to_mask_task_id;
    prediction_output_dir_            = started_context.output_dir;
    checkpoint_path_                  = started_context.checkpoint_path;

    bool started = false;
    if (data_provider_->method() == dltool::core::DeepLearningMethod::Detection)
    {
        stage_                       = RunStage::PreparingMask;
        current_prepare_class_index_ = 0;
        started                      = startBoxToMask(*active_context_, 0, err_msg);
    }
    else
    {
        stage_                       = RunStage::Training;
        current_predict_class_index_ = 0;
        started                      = startTraining(*active_context_, err_msg);
    }

    if (!started)
    {
        active_context_.reset();
        stage_ = RunStage::Idle;
        setLastError(err_msg);
        spdlog::error("启动小样本学习失败: {}", err_msg.toUtf8().constData());
        return false;
    }

    setRunning(true);
    return true;
}

void FewShotLearningController::cancel()
{
    stop_requested_ = true;
    if (task_manager_ && box_to_mask_task_id_ >= 0)
        task_manager_->stopTask(box_to_mask_task_id_);
    if (task_manager_ && train_task_id_ >= 0)
        task_manager_->stopTask(train_task_id_);
    if (task_manager_ && predict_task_id_ >= 0)
        task_manager_->stopTask(predict_task_id_);
}

bool FewShotLearningController::prepareRun(const std::vector<int64_t> &train_dataset_ids,
                                           const std::vector<int64_t> &test_dataset_ids,
                                           const std::vector<int64_t> &label_class_ids, RunContext &context,
                                           QString &err_msg) const
{
    if (data_provider_ == nullptr)
    {
        err_msg = QString("数据管理器未初始化");
        return false;
    }
    if (task_manager_ == nullptr)
    {
        err_msg = QString("任务管理器未初始化");
        return false;
    }
    if (data_provider_->method() != dltool::core::DeepLearningMethod::Detection
        && data_provider_->method() != dltool::core::DeepLearningMethod::Segmentation)
    {
        err_msg = QString("小样本学习仅支持检测和分割项目");
        return false;
    }
    if (train_dataset_ids.empty())
    {
        err_msg = QString("请至少选择一个训练数据集");
        return false;
    }
    if (test_dataset_ids.empty())
    {
        err_msg = QString("请至少选择一个测试数据集");
        return false;
    }

    namespace generated_field = dltool::settings::generated::field;

    auto *global_settings = dltool::settings::GlobalSettings::getInstance();

    context.python_executable
        = pythonExecutableFromEnvPath(settingString(global_settings, generated_field::Software::PythonEnvPath));
    if (context.python_executable.isEmpty())
    {
        err_msg = QString("请先在软件设置中配置 Python 环境目录");
        return false;
    }

    context.fs_sam2_root = fixedFsSam2Root();
    context.sam2_checkpoint
        = runtimePath(settingString(global_settings, generated_field::FewShotLearning::Sam2Checkpoint));
    const QString sam2_architecture
        = settingString(global_settings, generated_field::FewShotLearning::Sam2Architecture);
    if (sam2_architecture.isEmpty())
    {
        err_msg = QString("请先配置 SAM2 架构");
        return false;
    }
    context.sam2_cfg = sam2ConfigPathFromArchitecture(sam2_architecture, err_msg);
    if (context.sam2_cfg.isEmpty())
        return false;
    if (!QDir(context.fs_sam2_root).exists())
    {
        err_msg = QString("FS-SAM2 目录不存在: %1").arg(context.fs_sam2_root);
        return false;
    }
    context.train_script       = fsSam2ScriptPath(context.fs_sam2_root, FsSam2Script::Train);
    context.predict_script     = fsSam2ScriptPath(context.fs_sam2_root, FsSam2Script::Predict);
    context.box_to_mask_script = fsSam2ScriptPath(context.fs_sam2_root, FsSam2Script::BoxToMask);
    if (!QFileInfo::exists(context.train_script) || !QFileInfo::exists(context.predict_script))
    {
        err_msg = QString("FS-SAM2 目录缺少 train.py 或 predict.py: %1").arg(context.fs_sam2_root);
        return false;
    }
    if (!context.sam2_checkpoint.isEmpty() && !QFileInfo::exists(context.sam2_checkpoint))
    {
        err_msg = QString("SAM2 checkpoint 不存在: %1").arg(context.sam2_checkpoint);
        return false;
    }

    context.kshot  = std::clamp(settingInt(global_settings, generated_field::FewShotLearning::Kshot, 1), 1, 16);
    context.epochs = std::clamp(settingInt(global_settings, generated_field::FewShotLearning::Epochs, 50), 1, 10000);
    context.batch_size
        = std::clamp(settingInt(global_settings, generated_field::FewShotLearning::BatchSize, 2), 1, 128);
    context.num_workers
        = std::clamp(settingInt(global_settings, generated_field::FewShotLearning::NumWorkers, 0), 0, 128);
    context.image_size
        = std::clamp(settingInt(global_settings, generated_field::FewShotLearning::ImageSize, 1024), 64, 8192);
    context.lr           = settingDouble(global_settings, generated_field::FewShotLearning::LearningRate, 1e-4);
    context.weight_decay = settingDouble(global_settings, generated_field::FewShotLearning::WeightDecay, 1e-6);
    context.support_ratio
        = std::clamp(settingDouble(global_settings, generated_field::FewShotLearning::SupportRatio, 0.5), 0.1, 0.9);

    const QString output_root_setting
        = cleanPath(settingString(global_settings, generated_field::FewShotLearning::OutputDir));
    const QString project_dir  = QFileInfo(data_provider_->databasePath()).absoluteDir().absolutePath();
    const QString run_id       = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss_zzz"));
    context.exp_id             = QStringLiteral("dltool_%1").arg(run_id);
    context.logpath            = QStringLiteral("dltool/%1/fold0").arg(context.exp_id);
    context.run_dir            = output_root_setting.isEmpty()
                                   ? QDir(project_dir).filePath(QStringLiteral(".dltool/few_shot/%1").arg(run_id))
                                   : QDir(output_root_setting).filePath(run_id);
    context.custom_dataset_dir = QDir(context.run_dir).filePath(QStringLiteral("custom"));
    context.query_dir          = QDir(context.run_dir).filePath(QStringLiteral("query"));
    context.output_dir         = QDir(context.run_dir).filePath(QStringLiteral("predictions"));
    context.query_txt_path     = QDir(context.query_dir).filePath(QStringLiteral("query.txt"));
    context.checkpoint_path    = checkpointPath(context.fs_sam2_root, context.logpath);

    const std::set<int64_t>           selected_classes(label_class_ids.begin(), label_class_ids.end());
    const std::set<int64_t>           selected_train_datasets(train_dataset_ids.begin(), train_dataset_ids.end());
    const std::set<int64_t>           selected_test_datasets(test_dataset_ids.begin(), test_dataset_ids.end());
    std::map<int64_t, ClassBuildData> classes;
    for (int64_t label_class_id : label_class_ids)
    {
        const QString class_name = data_provider_->labelClassName(label_class_id);
        if (class_name.isEmpty())
            continue;
        ClassBuildData data;
        data.label_class_id   = label_class_id;
        data.label_class_name = class_name;
        data.class_dir_name   = sanitizeFileName(class_name, QStringLiteral("class_%1").arg(label_class_id));
        classes.emplace(label_class_id, std::move(data));
    }
    if (classes.empty())
    {
        err_msg = QString("没有有效类别");
        return false;
    }

    std::map<int64_t, QString> train_images;
    std::map<int64_t, QString> test_images;
    std::map<int64_t, int64_t> test_image_dataset_ids;
    for (int64_t image_id : data_provider_->allImageIds())
    {
        const int64_t dataset_id = data_provider_->imageDatasetId(image_id);
        if (selected_train_datasets.find(dataset_id) != selected_train_datasets.end())
            train_images[image_id] = data_provider_->imagePath(image_id);
        if (selected_test_datasets.find(dataset_id) != selected_test_datasets.end())
        {
            test_images[image_id]            = data_provider_->imagePath(image_id);
            test_image_dataset_ids[image_id] = dataset_id;
        }
    }
    if (train_images.empty() || test_images.empty())
    {
        err_msg = QString("训练数据集或测试数据集没有图像");
        return false;
    }

    const bool is_detection = data_provider_->method() == dltool::core::DeepLearningMethod::Detection;

    for (int64_t label_id : data_provider_->allLabelIds())
    {
        const int64_t image_id = data_provider_->labelImageId(label_id);
        if (train_images.find(image_id) == train_images.end())
            continue;

        const int64_t label_class_id = data_provider_->labelClassId(label_id);
        if (selected_classes.find(label_class_id) == selected_classes.end())
            continue;

        if (is_detection)
        {
            const QVariantMap label_data = data_provider_->labelData(label_id);
            QJsonObject       box;
            box[QStringLiteral("x")]      = label_data.value(QStringLiteral("x")).toDouble();
            box[QStringLiteral("y")]      = label_data.value(QStringLiteral("y")).toDouble();
            box[QStringLiteral("width")]  = label_data.value(QStringLiteral("width")).toDouble();
            box[QStringLiteral("height")] = label_data.value(QStringLiteral("height")).toDouble();
            classes[label_class_id].boxes_by_image_id[image_id].append(box);
        }
        else
        {
            const QString image_path = train_images[image_id];
            int           width      = 0;
            int           height     = 0;
            if (!getImageDimensions(image_path, width, height))
                continue;

            QImage &mask = classes[label_class_id].masks_by_image_id[image_id];
            if (mask.isNull())
            {
                mask = QImage(width, height, QImage::Format_Grayscale8);
                mask.fill(0);
            }
            paintLabelToMask(mask, data_provider_->labelData(label_id));
        }
    }

    for (const auto &[label_class_id, class_data] : classes)
    {
        Q_UNUSED(label_class_id)
        const size_t image_count
            = is_detection ? class_data.boxes_by_image_id.size() : class_data.masks_by_image_id.size();
        if (image_count < static_cast<size_t>(context.kshot + 1))
        {
            err_msg
                = QString("类别 %1 至少需要 %2 张带标注图像").arg(class_data.label_class_name).arg(context.kshot + 1);
            return false;
        }
    }

    if (!ensureDir(context.custom_dataset_dir, err_msg) || !ensureDir(context.query_dir, err_msg)
        || !ensureDir(context.output_dir, err_msg))
    {
        return false;
    }

    for (const auto &[label_class_id, class_data] : classes)
    {
        Q_UNUSED(label_class_id)
        const QString class_dir = QDir(context.custom_dataset_dir).filePath(class_data.class_dir_name);
        const QString image_dir = QDir(class_dir).filePath(QStringLiteral("images"));
        const QString mask_dir  = QDir(class_dir).filePath(QStringLiteral("masks"));
        if (!ensureDir(image_dir, err_msg) || !ensureDir(mask_dir, err_msg))
            return false;

        QStringList entries;

        if (is_detection)
        {
            QJsonObject boxes_json;
            for (const auto &[image_id, boxes] : class_data.boxes_by_image_id)
            {
                const QString alias = QStringLiteral("img_%1").arg(image_id);
                QString       copied_path;
                if (!copyImageToAlias(train_images[image_id], image_dir, alias, copied_path, err_msg))
                    return false;
                boxes_json[alias] = boxes;
                entries.push_back(QString("%1,%1").arg(alias));
            }

            QFile boxes_file(QDir(class_dir).filePath(QStringLiteral("boxes.json")));
            if (!boxes_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            {
                err_msg = QString("无法写入 boxes.json: %1, %2").arg(boxes_file.fileName(), boxes_file.errorString());
                return false;
            }
            boxes_file.write(QJsonDocument(boxes_json).toJson(QJsonDocument::Indented));
        }
        else
        {
            for (const auto &[image_id, mask] : class_data.masks_by_image_id)
            {
                const QString alias = QStringLiteral("img_%1").arg(image_id);
                QString       copied_path;
                if (!copyImageToAlias(train_images[image_id], image_dir, alias, copied_path, err_msg))
                    return false;
                if (!mask.save(QDir(mask_dir).filePath(alias + QStringLiteral(".png"))))
                {
                    err_msg = QString("写入训练 Mask 失败: %1").arg(alias);
                    return false;
                }
                entries.push_back(QString("%1,%1").arg(alias));
            }
        }

        const int   support_count   = std::clamp(static_cast<int>(std::round(entries.size() * context.support_ratio)),
                                                 context.kshot, static_cast<int>(entries.size()) - 1);
        QStringList support_entries = entries.mid(0, support_count);
        QStringList query_entries   = entries.mid(support_count);
        if (query_entries.empty())
            query_entries.push_back(entries.back());

        if (!writeTextFile(QDir(class_dir).filePath(QStringLiteral("support.txt")), support_entries, err_msg)
            || !writeTextFile(QDir(class_dir).filePath(QStringLiteral("query.txt")), query_entries, err_msg))
        {
            return false;
        }

        QJsonObject class_object;
        class_object[fewShotClassFieldName(FewShotClassField::Id)]   = static_cast<qint64>(class_data.label_class_id);
        class_object[fewShotClassFieldName(FewShotClassField::Name)] = class_data.label_class_name;
        class_object[fewShotClassFieldName(FewShotClassField::Dir)]  = class_data.class_dir_name;
        context.classes.append(class_object);
    }

    QStringList                    query_lines;
    std::map<int64_t, QStringList> manifest_lines_by_dataset;
    for (const auto &[image_id, image_path] : test_images)
    {
        const QString alias = QStringLiteral("img_%1").arg(image_id);
        QString       copied_path;
        if (!copyImageToAlias(image_path, context.query_dir, alias, copied_path, err_msg))
            return false;
        const QString line = QString("%1,%2").arg(alias, image_path);
        manifest_lines_by_dataset[test_image_dataset_ids[image_id]].push_back(line);
        query_lines.push_back(QString("%1,%1").arg(alias));
    }

    if (!writeTextFile(context.query_txt_path, query_lines, err_msg)
        || !writeTextFile(QDir(context.output_dir).filePath(QStringLiteral("query.txt")), query_lines, err_msg))
    {
        return false;
    }

    for (int64_t dataset_id : test_dataset_ids)
    {
        const QStringList lines = manifest_lines_by_dataset[dataset_id];
        if (lines.empty())
        {
            err_msg = QString("测试数据集 %1 没有图像").arg(data_provider_->datasetName(dataset_id));
            return false;
        }

        const QString manifest_path
            = QDir(context.run_dir).filePath(QStringLiteral("test_images_%1.txt").arg(dataset_id));
        if (!writeTextFile(manifest_path, lines, err_msg))
            return false;
        context.import_targets.push_back(PredictionImportTarget{dataset_id, manifest_path});
    }

    QString server_err;
    if (!task_manager_->ensureTaskServer(&server_err))
    {
        err_msg = QString("任务通信服务启动失败: %1").arg(server_err);
        return false;
    }

    const QString task_uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    context.train_task_id   = task_manager_->addExternalTask(task_uuid, fewShotTaskName(FewShotTaskKind::Train),
                                                             fewShotTaskType(FewShotTaskKind::Train));
    context.predict_task_id = task_manager_->addExternalTask(task_uuid, fewShotTaskName(FewShotTaskKind::Predict),
                                                             fewShotTaskType(FewShotTaskKind::Predict));
    if (is_detection)
    {
        context.box_to_mask_task_id = task_manager_->addExternalTask(
            task_uuid, fewShotTaskName(FewShotTaskKind::BoxToMask), fewShotTaskType(FewShotTaskKind::BoxToMask));
    }
    if (context.train_task_id < 0 || context.predict_task_id < 0 || (is_detection && context.box_to_mask_task_id < 0))
    {
        err_msg = QString("创建任务中心任务失败");
        return false;
    }
    context.task_host = task_manager_->taskServerHost();
    context.task_port = task_manager_->taskServerPort();
    return true;
}

bool FewShotLearningController::startTraining(const RunContext &context, QString &err_msg)
{
    QStringList arguments = {
        context.train_script,
        QStringLiteral("--datapath"),
        context.custom_dataset_dir,
        QStringLiteral("--benchmark"),
        QStringLiteral("custom"),
        QStringLiteral("--kshot"),
        QString::number(context.kshot),
        QStringLiteral("--epochs"),
        QString::number(context.epochs),
        QStringLiteral("--lr"),
        QString::number(context.lr, 'g', 16),
        QStringLiteral("--weight_decay"),
        QString::number(context.weight_decay, 'g', 16),
        QStringLiteral("--bsz"),
        QString::number(context.batch_size),
        QStringLiteral("--nworker"),
        QString::number(context.num_workers),
        QStringLiteral("--img_size"),
        QString::number(context.image_size),
        QStringLiteral("--exp_id"),
        context.exp_id,
        QStringLiteral("--fold"),
        QStringLiteral("0"),
        QStringLiteral("--logpath"),
        context.logpath,
        QStringLiteral("--sam2_cfg"),
        context.sam2_cfg,
        QStringLiteral("--dltool_task_host"),
        context.task_host,
        QStringLiteral("--dltool_task_port"),
        QString::number(context.task_port),
        QStringLiteral("--dltool_task_id"),
        QString::number(context.train_task_id),
    };
    if (!context.sam2_checkpoint.isEmpty())
        arguments << QStringLiteral("--sam2_checkpoint") << context.sam2_checkpoint;

    return startProcess(context, arguments, err_msg);
}

bool FewShotLearningController::startPrediction(const RunContext &context, int class_index, QString &err_msg)
{
    const int class_count = static_cast<int>(context.classes.size());
    if (class_index < 0 || class_index >= class_count)
    {
        err_msg = QString("推理类别索引无效: %1").arg(class_index);
        return false;
    }

    const QJsonObject class_object = context.classes.at(class_index).toObject();
    const QString     class_dir    = class_object.value(fewShotClassFieldName(FewShotClassField::Dir)).toString();
    if (class_dir.isEmpty())
    {
        err_msg = QString("推理类别目录为空");
        return false;
    }

    const QString support_dir = QDir(context.custom_dataset_dir).filePath(class_dir);
    const QString output_dir  = QDir(context.output_dir).filePath(class_dir);
    if (!ensureDir(output_dir, err_msg))
        return false;

    const int safe_class_count = std::max(1, class_count);
    const int base             = class_index * 100 / safe_class_count;
    const int end              = (class_index + 1) * 100 / safe_class_count;
    const int span             = std::max(1, end - base);

    QStringList arguments = {
        context.predict_script,
        QStringLiteral("--support_dir"),
        support_dir,
        QStringLiteral("--query_dir"),
        context.query_dir,
        QStringLiteral("--output_dir"),
        output_dir,
        QStringLiteral("--checkpoint"),
        context.checkpoint_path,
        QStringLiteral("--sam2_cfg"),
        context.sam2_cfg,
        QStringLiteral("--kshot"),
        QString::number(context.kshot),
        QStringLiteral("--img_size"),
        QString::number(context.image_size),
        QStringLiteral("--dltool_task_host"),
        context.task_host,
        QStringLiteral("--dltool_task_port"),
        QString::number(context.task_port),
        QStringLiteral("--dltool_task_id"),
        QString::number(context.predict_task_id),
        QStringLiteral("--dltool_progress_base"),
        QString::number(base),
        QStringLiteral("--dltool_progress_span"),
        QString::number(span),
    };
    if (!context.sam2_checkpoint.isEmpty())
        arguments << QStringLiteral("--sam2_checkpoint") << context.sam2_checkpoint;
    if (class_index + 1 == class_count)
        arguments << QStringLiteral("--dltool_finish_on_complete");

    return startProcess(context, arguments, err_msg);
}

bool FewShotLearningController::startBoxToMask(const RunContext &context, int class_index, QString &err_msg)
{
    const int class_count = static_cast<int>(context.classes.size());
    if (class_index < 0 || class_index >= class_count)
    {
        err_msg = QString("预处理类别索引无效: %1").arg(class_index);
        return false;
    }

    const QJsonObject class_object   = context.classes.at(class_index).toObject();
    const QString     class_dir_name = class_object.value(fewShotClassFieldName(FewShotClassField::Dir)).toString();
    if (class_dir_name.isEmpty())
    {
        err_msg = QString("预处理类别目录为空");
        return false;
    }

    const QString support_dir      = QDir(context.custom_dataset_dir).filePath(class_dir_name);
    const int     safe_class_count = std::max(1, class_count);
    const int     base             = class_index * 100 / safe_class_count;
    const int     end              = (class_index + 1) * 100 / safe_class_count;
    const int     span             = std::max(1, end - base);

    QStringList arguments = {
        context.box_to_mask_script,
        QStringLiteral("--support_dir"),
        support_dir,
        QStringLiteral("--sam2_checkpoint"),
        context.sam2_checkpoint,
        QStringLiteral("--sam2_cfg"),
        context.sam2_cfg,
        QStringLiteral("--img_size"),
        QString::number(context.image_size),
        QStringLiteral("--dltool_task_host"),
        context.task_host,
        QStringLiteral("--dltool_task_port"),
        QString::number(context.task_port),
        QStringLiteral("--dltool_task_id"),
        QString::number(context.box_to_mask_task_id),
        QStringLiteral("--dltool_progress_base"),
        QString::number(base),
        QStringLiteral("--dltool_progress_span"),
        QString::number(span),
    };

    return startProcess(context, arguments, err_msg);
}

bool FewShotLearningController::startProcess(const RunContext &context, const QStringList &arguments, QString &err_msg)
{
    if (process_ != nullptr && process_->state() != QProcess::NotRunning)
    {
        err_msg = QString("已有小样本学习进程正在运行");
        return false;
    }

    auto *process = new QProcess(this);
    process_      = process;
    process->setProgram(context.python_executable);
    process->setArguments(arguments);
    process->setWorkingDirectory(context.fs_sam2_root);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    env.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    process->setProcessEnvironment(env);
    process->setProcessChannelMode(QProcess::MergedChannels);

    connect(process, &QProcess::readyReadStandardOutput, this,
            [process]()
            {
                const QString output = QString::fromUtf8(process->readAllStandardOutput()).trimmed();
                if (!output.isEmpty())
                    spdlog::info("FS-SAM2: {}", output.toUtf8().constData());
            });
    connect(process, &QProcess::finished, this, &FewShotLearningController::handleProcessFinished);

    process->start();
    if (!process->waitForStarted(5000))
    {
        err_msg = QString("启动小样本学习进程失败: %1").arg(process->errorString());
        process->deleteLater();
        process_ = nullptr;
        return false;
    }
    return true;
}

void FewShotLearningController::finishRun()
{
    if (data_provider_ != nullptr && import_finished_connection_)
        data_provider_->disconnectImportFinished(import_finished_connection_);
    import_finished_connection_ = {};
    active_context_.reset();
    stage_                       = RunStage::Idle;
    current_prepare_class_index_ = 0;
    current_predict_class_index_ = 0;
    current_import_index_        = 0;
    importing_predictions_       = false;
    train_task_id_               = -1;
    predict_task_id_             = -1;
    box_to_mask_task_id_         = -1;
    prediction_output_dir_.clear();
    checkpoint_path_.clear();
    setRunning(false);
}

void FewShotLearningController::handleProcessFinished(int exit_code, QProcess::ExitStatus exit_status)
{
    auto *finished_process = qobject_cast<QProcess *>(sender());
    if (finished_process != nullptr)
    {
        if (process_ == finished_process)
            process_ = nullptr;
        finished_process->deleteLater();
    }

    const bool success = exit_status == QProcess::NormalExit && exit_code == 0 && !stop_requested_;
    if (!success)
    {
        const QString message
            = stop_requested_ ? QString("小样本学习任务已停止") : QString("小样本学习进程异常退出: %1").arg(exit_code);
        setLastError(message);
        if (task_manager_ && !stop_requested_)
        {
            if (stage_ == RunStage::PreparingMask)
                task_manager_->failTask(box_to_mask_task_id_);
            if (stage_ == RunStage::Training)
                task_manager_->failTask(train_task_id_);
            if (stage_ == RunStage::PreparingMask || stage_ == RunStage::Training || stage_ == RunStage::Predicting)
                task_manager_->failTask(predict_task_id_);
        }
        finishRun();
        return;
    }

    if (active_context_ == nullptr)
    {
        finishRun();
        return;
    }

    if (stage_ == RunStage::PreparingMask)
    {
        ++current_prepare_class_index_;
        if (current_prepare_class_index_ < static_cast<int>(active_context_->classes.size()))
        {
            QString err_msg;
            if (!startBoxToMask(*active_context_, current_prepare_class_index_, err_msg))
            {
                setLastError(err_msg);
                if (task_manager_)
                {
                    task_manager_->failTask(box_to_mask_task_id_);
                    task_manager_->failTask(train_task_id_);
                    task_manager_->failTask(predict_task_id_);
                }
                finishRun();
            }
            return;
        }

        if (task_manager_)
            task_manager_->finishTask(box_to_mask_task_id_);

        stage_                       = RunStage::Training;
        current_predict_class_index_ = 0;

        QString err_msg;
        if (!startTraining(*active_context_, err_msg))
        {
            setLastError(err_msg);
            if (task_manager_)
            {
                task_manager_->failTask(train_task_id_);
                task_manager_->failTask(predict_task_id_);
            }
            finishRun();
        }
        return;
    }

    if (stage_ == RunStage::Training)
    {
        if (!QFileInfo::exists(active_context_->checkpoint_path))
        {
            const QString message = QString("未找到训练模型: %1").arg(active_context_->checkpoint_path);
            setLastError(message);
            if (task_manager_)
            {
                task_manager_->failTask(train_task_id_);
                task_manager_->failTask(predict_task_id_);
            }
            finishRun();
            return;
        }

        if (task_manager_)
            task_manager_->finishTask(train_task_id_);

        stage_                       = RunStage::Predicting;
        current_predict_class_index_ = 0;

        QString err_msg;
        if (!startPrediction(*active_context_, current_predict_class_index_, err_msg))
        {
            setLastError(err_msg);
            if (task_manager_)
                task_manager_->failTask(predict_task_id_);
            finishRun();
        }
        return;
    }

    if (stage_ == RunStage::Predicting)
    {
        ++current_predict_class_index_;
        if (current_predict_class_index_ < static_cast<int>(active_context_->classes.size()))
        {
            QString err_msg;
            if (!startPrediction(*active_context_, current_predict_class_index_, err_msg))
            {
                setLastError(err_msg);
                if (task_manager_)
                    task_manager_->failTask(predict_task_id_);
                finishRun();
            }
            return;
        }

        if (task_manager_)
            task_manager_->finishTask(predict_task_id_);
        startPredictionImports();
        return;
    }

    finishRun();
}

void FewShotLearningController::startPredictionImports()
{
    if (data_provider_ == nullptr || active_context_ == nullptr || active_context_->import_targets.empty())
    {
        finishRun();
        return;
    }

    importing_predictions_      = true;
    current_import_index_       = 0;
    import_finished_connection_ = data_provider_->connectImportFinished(
        this, [this](bool success, const QString &message) { handlePredictionImportFinished(success, message); });
    startNextPredictionImport();
}

void FewShotLearningController::startNextPredictionImport()
{
    if (!importing_predictions_ || data_provider_ == nullptr || active_context_ == nullptr)
    {
        finishRun();
        return;
    }

    if (current_import_index_ >= static_cast<int>(active_context_->import_targets.size()))
    {
        if (import_finished_connection_)
            data_provider_->disconnectImportFinished(import_finished_connection_);
        import_finished_connection_ = {};
        importing_predictions_      = false;
        finishRun();
        return;
    }

    const PredictionImportTarget &target
        = active_context_->import_targets.at(static_cast<size_t>(current_import_index_));
    data_provider_->importMaskData(target.dataset_id, target.manifest_path, prediction_output_dir_);
}

void FewShotLearningController::handlePredictionImportFinished(bool success, const QString &message)
{
    if (!importing_predictions_)
        return;

    if (!success)
    {
        setLastError(message);
        if (data_provider_ != nullptr && import_finished_connection_)
            data_provider_->disconnectImportFinished(import_finished_connection_);
        import_finished_connection_ = {};
        importing_predictions_      = false;
        finishRun();
        return;
    }

    ++current_import_index_;
    startNextPredictionImport();
}

void FewShotLearningController::handleTaskStopRequested(int task_id)
{
    if (task_id != train_task_id_ && task_id != predict_task_id_ && task_id != box_to_mask_task_id_)
        return;

    stop_requested_ = true;
    if (process_ == nullptr || process_->state() == QProcess::NotRunning)
        return;

    QTimer::singleShot(3000, this,
                       [this]()
                       {
                           if (process_ != nullptr && process_->state() != QProcess::NotRunning)
                               process_->terminate();
                       });
    QTimer::singleShot(8000, this,
                       [this]()
                       {
                           if (process_ != nullptr && process_->state() != QProcess::NotRunning)
                               process_->kill();
                       });
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
