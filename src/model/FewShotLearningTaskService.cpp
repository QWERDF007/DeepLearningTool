#include "model/FewShotLearningTaskService.h"

#include "common/Utils.h"
#include "common/YamlUtils.h"
#include "core/CoreDef.h"
#include "data/DataManager.h"
#include "model/ModelRegistry.h"
#include "model/ModelStorageService.h"
#include "settings/GlobalSettings.h"
#include "settings/SettingsKeys.h"
#include "settings/SettingsValue.h"

#include <spdlog/spdlog.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>
#include <QPointF>
#include <QPolygonF>
#include <QRegularExpression>
#include <QTextStream>
#include <QUuid>
#include <QVariantList>
#include <QVariantMap>
#include <algorithm>
#include <map>
#include <set>
#include <utility>

using dltool::common::yaml::setMapValue;

namespace dltool::model {
using common::setError;

namespace {

constexpr const char *kFsSam2FrameworkName = "FS-SAM2";

struct ClassBuildData
{
    int64_t                       label_class_id{-1};
    QString                       label_class_name;
    QString                       class_dir_name;
    std::map<int64_t, QImage>     masks_by_image_id;
    std::map<int64_t, QJsonArray> boxes_by_image_id;
};

using ClassBuildMap = std::map<int64_t, ClassBuildData>;

struct FewShotImageSelection
{
    std::map<int64_t, QString> train_images;
    std::map<int64_t, QString> validation_images;
    std::map<int64_t, QString> test_images;
    std::map<int64_t, int64_t> test_image_dataset_ids;
};

struct Sam2ConfigNameParts
{
    QString prefix;
    QString size_token;
};

QString fixedSam2ConfigRoot()
{
    return dltool::common::runtimePath(QStringLiteral("python/facebookresearch/sam2/sam2/configs"));
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

bool writeTextFile(const QString &path, const QStringList &lines, QString &err_msg)
{
    QFileInfo info(path);
    if (!dltool::common::ensureDirectory(info.dir().absolutePath(), err_msg))
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

QJsonObject boxFromLabelData(const QVariantMap &label_data)
{
    QJsonObject box;
    box[QStringLiteral("x")]      = label_data.value(QStringLiteral("x")).toDouble();
    box[QStringLiteral("y")]      = label_data.value(QStringLiteral("y")).toDouble();
    box[QStringLiteral("width")]  = label_data.value(QStringLiteral("width")).toDouble();
    box[QStringLiteral("height")] = label_data.value(QStringLiteral("height")).toDouble();
    return box;
}

bool appendMaskLabel(ClassBuildData &class_data, int64_t image_id, const QString &image_path,
                     const QVariantMap &label_data)
{
    int width  = 0;
    int height = 0;
    if (!getImageDimensions(image_path, width, height))
        return false;

    QImage &mask = class_data.masks_by_image_id[image_id];
    if (mask.isNull())
    {
        mask = QImage(width, height, QImage::Format_Grayscale8);
        mask.fill(0);
    }
    paintLabelToMask(mask, label_data);
    return true;
}

bool buildClassTemplates(dltool::data::DataManager *data_manager, const std::vector<int64_t> &label_class_ids,
                         ClassBuildMap &classes, QString &err_msg)
{
    for (int64_t label_class_id : label_class_ids)
    {
        const QString class_name = data_manager->labelClassName(label_class_id);
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
    return true;
}

FewShotImageSelection collectImagesByDataset(dltool::data::DataManager *data_manager,
                                             const std::set<int64_t>   &selected_train_datasets,
                                             const std::set<int64_t>   &selected_validation_datasets,
                                             const std::set<int64_t>   &selected_test_datasets)
{
    FewShotImageSelection selection;
    for (int64_t image_id : data_manager->allImageIds())
    {
        const int64_t dataset_id = data_manager->imageDatasetId(image_id);
        const QString image_path = data_manager->imagePath(image_id);

        if (selected_train_datasets.find(dataset_id) != selected_train_datasets.end())
            selection.train_images[image_id] = image_path;
        if (!selected_validation_datasets.empty()
            && selected_validation_datasets.find(dataset_id) != selected_validation_datasets.end())
        {
            selection.validation_images[image_id] = image_path;
        }
        if (selected_test_datasets.find(dataset_id) != selected_test_datasets.end())
        {
            selection.test_images[image_id]            = image_path;
            selection.test_image_dataset_ids[image_id] = dataset_id;
        }
    }
    return selection;
}

void collectClassLabels(dltool::data::DataManager *data_manager, const std::set<int64_t> &selected_classes,
                        const FewShotImageSelection &image_selection, bool is_detection, ClassBuildMap &train_classes,
                        ClassBuildMap &validation_classes)
{
    for (int64_t label_id : data_manager->allLabelIds())
    {
        const int64_t image_id            = data_manager->labelImageId(label_id);
        const auto    train_image_it      = image_selection.train_images.find(image_id);
        const auto    validation_image_it = image_selection.validation_images.find(image_id);
        const bool    in_train            = train_image_it != image_selection.train_images.end();
        const bool    in_validation       = validation_image_it != image_selection.validation_images.end();
        if (!in_train && !in_validation)
            continue;

        const int64_t label_class_id = data_manager->labelClassId(label_id);
        if (selected_classes.find(label_class_id) == selected_classes.end())
            continue;

        const QVariantMap label_data = data_manager->labelData(label_id);
        if (is_detection)
        {
            const QJsonObject box = boxFromLabelData(label_data);
            if (in_train)
                train_classes[label_class_id].boxes_by_image_id[image_id].append(box);
            if (in_validation)
                validation_classes[label_class_id].boxes_by_image_id[image_id].append(box);
            continue;
        }

        if (in_train)
            appendMaskLabel(train_classes[label_class_id], image_id, train_image_it->second, label_data);
        if (in_validation)
            appendMaskLabel(validation_classes[label_class_id], image_id, validation_image_it->second, label_data);
    }
}

bool validateClassBuildData(const ClassBuildMap &classes, bool is_detection, int kshot, const QString &dataset_label,
                            QString &err_msg)
{
    for (const auto &[label_class_id, class_data] : classes)
    {
        Q_UNUSED(label_class_id)
        const size_t image_count
            = is_detection ? class_data.boxes_by_image_id.size() : class_data.masks_by_image_id.size();
        if (image_count < static_cast<size_t>(kshot + 1))
        {
            err_msg = QString("%1中类别 %2 至少需要 %3 张带标注图像")
                          .arg(dataset_label, class_data.label_class_name)
                          .arg(kshot + 1);
            return false;
        }
    }
    return true;
}

QString fewShotImageAlias(int64_t image_id)
{
    return QStringLiteral("img_%1").arg(image_id);
}

QString manifestPath(const QString &dataset_dir)
{
    return QDir(dataset_dir).filePath(QStringLiteral("manifest.yaml"));
}

QString maskPath(const QString &dataset_dir, int64_t image_id, int label_index)
{
    return QDir(QDir(dataset_dir).filePath(QStringLiteral("masks")))
        .filePath(QStringLiteral("image_%1_label_%2.png").arg(image_id).arg(label_index));
}

QVariantMap boxVariantMap(const QJsonValue &box)
{
    return box.toObject().toVariantMap();
}

bool appendManifestImage(std::map<int64_t, YAML::Node> &images_by_id, int64_t image_id, const QString &image_path,
                         dltool::data::DataManager *data_manager, QString &err_msg)
{
    if (images_by_id.find(image_id) != images_by_id.end())
        return true;

    int width  = 0;
    int height = 0;
    if (!getImageDimensions(image_path, width, height))
    {
        err_msg = QString("无法读取图像尺寸: %1").arg(image_path);
        return false;
    }

    YAML::Node labels(YAML::NodeType::Sequence);
    YAML::Node image(YAML::NodeType::Map);
    setMapValue(image, QStringLiteral("id"), image_id);
    setMapValue(image, QStringLiteral("path"), dltool::common::cleanPath(image_path));
    setMapValue(image, QStringLiteral("dataset_id"), data_manager->imageDatasetId(image_id));
    setMapValue(image, QStringLiteral("dataset_name"),
                data_manager->datasetName(data_manager->imageDatasetId(image_id)));
    setMapValue(image, QStringLiteral("width"), width);
    setMapValue(image, QStringLiteral("height"), height);
    setMapValue(image, QStringLiteral("labels"), labels);
    images_by_id.emplace(image_id, image);
    return true;
}

bool writeCustomManifest(const QString &dataset_dir, const QString &split_name, const ClassBuildMap &classes,
                         const std::map<int64_t, QString> &source_images, bool is_detection,
                         dltool::data::DataManager *data_manager, QString &err_msg)
{
    if (!dltool::common::ensureDirectory(dataset_dir, err_msg))
        return false;

    std::map<int64_t, YAML::Node> images_by_id;
    int                           label_index = 0;
    for (const auto &[label_class_id, class_data] : classes)
    {
        if (is_detection)
        {
            for (const auto &[image_id, boxes] : class_data.boxes_by_image_id)
            {
                const auto image_it = source_images.find(image_id);
                if (image_it == source_images.end())
                    continue;
                if (!appendManifestImage(images_by_id, image_id, image_it->second, data_manager, err_msg))
                    return false;

                YAML::Node labels = images_by_id[image_id][QStringLiteral("labels").toStdString()];
                for (const QJsonValue &box : boxes)
                {
                    ++label_index;
                    YAML::Node label(YAML::NodeType::Map);
                    setMapValue(label, QStringLiteral("label_id"), label_index);
                    setMapValue(label, QStringLiteral("label_class_id"), label_class_id);
                    setMapValue(label, QStringLiteral("label_class_name"), class_data.class_dir_name);
                    setMapValue(label, QStringLiteral("label_class_group"), QString());
                    setMapValue(label, QStringLiteral("data"), dltool::common::yaml::variantToYaml(boxVariantMap(box)));
                    setMapValue(label, QStringLiteral("mask_path"),
                                dltool::common::cleanPath(maskPath(dataset_dir, image_id, label_index)));
                    labels.push_back(label);
                }
            }
            continue;
        }

        for (const auto &[image_id, mask] : class_data.masks_by_image_id)
        {
            const auto image_it = source_images.find(image_id);
            if (image_it == source_images.end())
                continue;
            if (!appendManifestImage(images_by_id, image_id, image_it->second, data_manager, err_msg))
                return false;

            ++label_index;
            const QString saved_mask_path = maskPath(dataset_dir, image_id, label_index);
            if (!dltool::common::ensureDirectory(QFileInfo(saved_mask_path).dir().absolutePath(), err_msg)
                || !mask.save(saved_mask_path))
            {
                err_msg = QString("写入训练 Mask 失败: %1").arg(saved_mask_path);
                return false;
            }

            YAML::Node label(YAML::NodeType::Map);
            setMapValue(label, QStringLiteral("label_id"), label_index);
            setMapValue(label, QStringLiteral("label_class_id"), label_class_id);
            setMapValue(label, QStringLiteral("label_class_name"), class_data.class_dir_name);
            setMapValue(label, QStringLiteral("label_class_group"), QString());
            setMapValue(label, QStringLiteral("mask_path"), dltool::common::cleanPath(saved_mask_path));
            images_by_id[image_id][QStringLiteral("labels").toStdString()].push_back(label);
        }
    }

    YAML::Node manifest(YAML::NodeType::Map);
    YAML::Node images(YAML::NodeType::Sequence);
    setMapValue(manifest, QStringLiteral("version"), 1);
    setMapValue(manifest, QStringLiteral("framework"), QStringLiteral("FS-SAM2"));
    setMapValue(manifest, QStringLiteral("split"), split_name);
    for (const auto &[image_id, image] : images_by_id)
    {
        Q_UNUSED(image_id)
        images.push_back(image);
    }
    setMapValue(manifest, QStringLiteral("images"), images);

    return dltool::common::yaml::writeFile(manifestPath(dataset_dir), manifest, &err_msg,
                                           QString("写入 FS-SAM2 manifest 失败"),
                                           QString("生成 FS-SAM2 manifest 失败"));
}

bool writeQueryManifest(dltool::data::DataManager *data_manager, const QString &manifest_path,
                        const QString &output_dir, const std::vector<int64_t> &test_dataset_ids,
                        const std::map<int64_t, QString>           &test_images,
                        const std::map<int64_t, int64_t>           &test_image_dataset_ids,
                        std::vector<FewShotPredictionImportTarget> &import_targets, QString &err_msg)
{
    YAML::Node                     manifest(YAML::NodeType::Map);
    YAML::Node                     images(YAML::NodeType::Sequence);
    QStringList                    query_lines;
    std::map<int64_t, QStringList> manifest_lines_by_dataset;

    for (const auto &[image_id, image_path] : test_images)
    {
        const QString alias  = fewShotImageAlias(image_id);
        int           width  = 0;
        int           height = 0;
        if (!getImageDimensions(image_path, width, height))
        {
            err_msg = QString("无法读取测试图像尺寸: %1").arg(image_path);
            return false;
        }

        YAML::Node labels(YAML::NodeType::Sequence);
        YAML::Node image(YAML::NodeType::Map);
        setMapValue(image, QStringLiteral("id"), alias);
        setMapValue(image, QStringLiteral("path"), dltool::common::cleanPath(image_path));
        setMapValue(image, QStringLiteral("dataset_id"), test_image_dataset_ids.at(image_id));
        setMapValue(image, QStringLiteral("dataset_name"),
                    data_manager->datasetName(test_image_dataset_ids.at(image_id)));
        setMapValue(image, QStringLiteral("width"), width);
        setMapValue(image, QStringLiteral("height"), height);
        setMapValue(image, QStringLiteral("labels"), labels);
        images.push_back(image);

        const QString line = QString("%1,%2").arg(alias, image_path);
        manifest_lines_by_dataset[test_image_dataset_ids.at(image_id)].push_back(line);
        query_lines.push_back(line);
    }

    setMapValue(manifest, QStringLiteral("version"), 1);
    setMapValue(manifest, QStringLiteral("framework"), QStringLiteral("FS-SAM2"));
    setMapValue(manifest, QStringLiteral("split"), QStringLiteral("test"));
    setMapValue(manifest, QStringLiteral("images"), images);
    if (!dltool::common::yaml::writeFile(manifest_path, manifest, &err_msg, QString("写入 FS-SAM2 测试 manifest 失败"),
                                         QString("生成 FS-SAM2 测试 manifest 失败")))
    {
        return false;
    }
    if (!writeTextFile(QDir(output_dir).filePath(QStringLiteral("query.txt")), query_lines, err_msg))
        return false;

    for (int64_t dataset_id : test_dataset_ids)
    {
        const QStringList lines = manifest_lines_by_dataset[dataset_id];
        if (lines.empty())
        {
            err_msg = QString("测试数据集 %1 没有图像").arg(data_manager->datasetName(dataset_id));
            return false;
        }

        const QString import_manifest_path
            = QFileInfo(manifest_path).absoluteDir().filePath(QStringLiteral("test_images_%1.txt").arg(dataset_id));
        if (!writeTextFile(import_manifest_path, lines, err_msg))
            return false;
        import_targets.push_back(FewShotPredictionImportTarget{dataset_id, import_manifest_path});
    }
    return true;
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

QString sam2ConfigPathFromArchitecture(const QString &architecture_name, QString &err_msg)
{
    const Sam2ConfigNameParts parts = sam2ConfigNameParts(architecture_name);
    if (parts.prefix.isEmpty() || parts.size_token.isEmpty())
    {
        err_msg = QString("不支持的 SAM2 架构: %1").arg(architecture_name);
        return {};
    }

    const QString path = dltool::common::cleanPath(
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
    return dltool::common::cleanPath(QDir(fs_sam2_root).filePath(QString("logs/%1.log/best_model.pt").arg(logpath)));
}

QString projectDirFor(dltool::data::DataManager *data_manager, const QString &fallback_project_dir)
{
    const QString cleaned = dltool::common::cleanPath(fallback_project_dir);
    if (!cleaned.isEmpty())
        return cleaned;
    return data_manager != nullptr ? QFileInfo(data_manager->databasePath()).absoluteDir().absolutePath() : QString();
}

QString processLogPath(const FewShotLearningRunContext &context, const QString &stem)
{
    return dltool::common::cleanPath(QDir(context.run_dir).filePath(QStringLiteral("logs/%1.log").arg(stem)));
}

} // namespace

FewShotLearningTaskService::FewShotLearningTaskService(int method, QString project_dir,
                                                       dltool::data::DataManager *data_manager)
    : method_(method)
    , project_dir_(dltool::common::cleanPath(std::move(project_dir)))
    , data_manager_(data_manager)
{
}

bool FewShotLearningTaskService::prepare(const FewShotLearningRequest &request, FewShotLearningRunContext &context,
                                         QString *err_msg) const
{
    context = {};

    if (data_manager_ == nullptr)
        return setError(err_msg, QString("数据管理器未初始化"));
    if (method_ != dltool::core::DeepLearningMethod::Detection
        && method_ != dltool::core::DeepLearningMethod::Segmentation
        && method_ != dltool::core::DeepLearningMethod::AnomalyDetection)
    {
        return setError(err_msg, QString("小样本学习仅支持检测、分割和异常检测项目"));
    }
    if (request.train_dataset_ids.empty())
        return setError(err_msg, QString("请至少选择一个训练数据集"));
    if (request.test_dataset_ids.empty())
        return setError(err_msg, QString("请至少选择一个测试数据集"));

    namespace generated_field = dltool::settings::generated::field;
    auto *global_settings     = dltool::settings::GlobalSettings::getInstance();

    context.python_executable = dltool::common::pythonExecutableFromEnvPath(
        dltool::settings::settingString(global_settings, generated_field::Software::PythonEnvPath));
    if (context.python_executable.isEmpty())
        return setError(err_msg, QString("请先在软件设置中配置 Python 环境目录"));

    const FrameworkDefinition fs_sam2_framework = registeredFramework(method_, QString::fromUtf8(kFsSam2FrameworkName));
    if (fs_sam2_framework.name.isEmpty())
        return setError(err_msg, QString("FS-SAM2 框架未注册"));

    context.fs_sam2_root    = fs_sam2_framework.root;
    context.sam2_checkpoint = dltool::common::runtimePath(
        dltool::settings::settingString(global_settings, generated_field::FewShotLearning::Sam2Checkpoint));
    const QString sam2_architecture
        = dltool::settings::settingString(global_settings, generated_field::FewShotLearning::Sam2Architecture);
    if (sam2_architecture.isEmpty())
        return setError(err_msg, QString("请先配置 SAM2 架构"));
    QString sam2_err;
    context.sam2_cfg = sam2ConfigPathFromArchitecture(sam2_architecture, sam2_err);
    if (context.sam2_cfg.isEmpty())
        return setError(err_msg, sam2_err);
    if (!QDir(context.fs_sam2_root).exists())
        return setError(err_msg, QString("FS-SAM2 目录不存在: %1").arg(context.fs_sam2_root));

    context.train_script       = fs_sam2_framework.scriptFor(ModelTaskType::Train);
    context.predict_script     = fs_sam2_framework.scriptFor(ModelTaskType::Test);
    context.box_to_mask_script = fs_sam2_framework.scripts.value(QStringLiteral("box_to_mask"));
    context.python_paths       = fs_sam2_framework.python_paths;
    if (!QFileInfo::exists(context.train_script) || !QFileInfo::exists(context.predict_script))
        return setError(err_msg, QString("FS-SAM2 目录缺少 train.py 或 predict.py: %1").arg(context.fs_sam2_root));
    if (!context.sam2_checkpoint.isEmpty() && !QFileInfo::exists(context.sam2_checkpoint))
        return setError(err_msg, QString("SAM2 checkpoint 不存在: %1").arg(context.sam2_checkpoint));

    context.kshot
        = std::clamp(dltool::settings::settingInt(global_settings, generated_field::FewShotLearning::Kshot, 1), 1, 16);
    context.epochs = std::clamp(
        dltool::settings::settingInt(global_settings, generated_field::FewShotLearning::Epochs, 50), 1, 10000);
    context.batch_size = std::clamp(
        dltool::settings::settingInt(global_settings, generated_field::FewShotLearning::BatchSize, 2), 1, 128);
    context.num_workers = std::clamp(
        dltool::settings::settingInt(global_settings, generated_field::FewShotLearning::NumWorkers, 0), 0, 128);
    context.image_size = std::clamp(
        dltool::settings::settingInt(global_settings, generated_field::FewShotLearning::ImageSize, 1024), 64, 8192);
    context.lr = dltool::settings::settingDouble(global_settings, generated_field::FewShotLearning::LearningRate, 1e-4);
    context.weight_decay
        = dltool::settings::settingDouble(global_settings, generated_field::FewShotLearning::WeightDecay, 1e-6);

    const QString project_dir = projectDirFor(data_manager_, project_dir_);
    if (project_dir.isEmpty())
        return setError(err_msg, QString("项目目录为空"));

    context.task_uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    ModelStorageService storage(project_dir);
    QString             storage_err;
    if (!storage.ensureModelStorage(context.task_uuid, &storage_err))
        return setError(err_msg, storage_err);

    const QString run_id             = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss_zzz"));
    const QString datasets_dir       = storage.path(context.task_uuid, ModelStorageLocation::Datasets);
    const QString results_dir        = storage.path(context.task_uuid, ModelStorageLocation::Results);
    const QString weights_dir        = storage.path(context.task_uuid, ModelStorageLocation::Weights);
    context.exp_id                   = QStringLiteral("dltool_%1").arg(run_id);
    context.logpath                  = QStringLiteral("dltool/%1/fold0").arg(context.exp_id);
    context.run_dir                  = storage.path(context.task_uuid, ModelStorageLocation::ModelRoot);
    context.train_dataset_dir        = QDir(datasets_dir).filePath(QStringLiteral("train"));
    context.validation_dataset_dir   = request.validation_dataset_ids.empty()
                                         ? context.train_dataset_dir
                                         : QDir(datasets_dir).filePath(QStringLiteral("validation"));
    context.test_dataset_dir         = QDir(datasets_dir).filePath(QStringLiteral("test"));
    context.output_dir               = QDir(results_dir).filePath(QStringLiteral("predictions"));
    context.train_manifest_path      = manifestPath(context.train_dataset_dir);
    context.validation_manifest_path = request.validation_dataset_ids.empty()
                                         ? context.train_manifest_path
                                         : manifestPath(context.validation_dataset_dir);
    context.test_manifest_path       = manifestPath(context.test_dataset_dir);
    context.training_checkpoint_path = checkpointPath(context.fs_sam2_root, context.logpath);
    context.checkpoint_path = dltool::common::cleanPath(QDir(weights_dir).filePath(QStringLiteral("best_model.pt")));
    context.requires_box_to_mask = method_ == dltool::core::DeepLearningMethod::Detection;

    const std::set<int64_t> selected_classes(request.label_class_ids.begin(), request.label_class_ids.end());
    const std::set<int64_t> selected_train_datasets(request.train_dataset_ids.begin(), request.train_dataset_ids.end());
    const std::set<int64_t> selected_validation_datasets(request.validation_dataset_ids.begin(),
                                                         request.validation_dataset_ids.end());
    const std::set<int64_t> selected_test_datasets(request.test_dataset_ids.begin(), request.test_dataset_ids.end());

    ClassBuildMap classes;
    QString       prepare_err;
    if (!buildClassTemplates(data_manager_, request.label_class_ids, classes, prepare_err))
        return setError(err_msg, prepare_err);

    ClassBuildMap         validation_classes = classes;
    FewShotImageSelection image_selection    = collectImagesByDataset(
        data_manager_, selected_train_datasets, selected_validation_datasets, selected_test_datasets);
    if (image_selection.train_images.empty() || image_selection.test_images.empty())
        return setError(err_msg, QString("训练数据集或测试数据集没有图像"));
    if (!selected_validation_datasets.empty() && image_selection.validation_images.empty())
        return setError(err_msg, QString("验证数据集没有图像"));

    if (context.requires_box_to_mask && !QFileInfo::exists(context.box_to_mask_script))
        return setError(err_msg, QString("FS-SAM2 目录缺少 box_to_mask.py: %1").arg(context.fs_sam2_root));

    collectClassLabels(data_manager_, selected_classes, image_selection, context.requires_box_to_mask, classes,
                       validation_classes);

    if (!validateClassBuildData(classes, context.requires_box_to_mask, context.kshot, QString("训练数据集"),
                                prepare_err))
        return setError(err_msg, prepare_err);
    if (!selected_validation_datasets.empty()
        && !validateClassBuildData(validation_classes, context.requires_box_to_mask, context.kshot,
                                   QString("验证数据集"), prepare_err))
    {
        return setError(err_msg, prepare_err);
    }

    if (!dltool::common::ensureDirectory(context.train_dataset_dir, prepare_err)
        || !dltool::common::ensureDirectory(context.test_dataset_dir, prepare_err)
        || !dltool::common::ensureDirectory(context.output_dir, prepare_err)
        || !dltool::common::ensureDirectory(QDir(context.run_dir).filePath(QStringLiteral("logs")), prepare_err))
    {
        return setError(err_msg, prepare_err);
    }

    if (!writeCustomManifest(context.train_dataset_dir, QStringLiteral("train"), classes, image_selection.train_images,
                             context.requires_box_to_mask, data_manager_, prepare_err))
    {
        return setError(err_msg, prepare_err);
    }
    if (!selected_validation_datasets.empty()
        && !writeCustomManifest(context.validation_dataset_dir, QStringLiteral("validation"), validation_classes,
                                image_selection.validation_images, context.requires_box_to_mask, data_manager_,
                                prepare_err))
    {
        return setError(err_msg, prepare_err);
    }
    if (!writeQueryManifest(data_manager_, context.test_manifest_path, context.output_dir, request.test_dataset_ids,
                            image_selection.test_images, image_selection.test_image_dataset_ids, context.import_targets,
                            prepare_err))
    {
        return setError(err_msg, prepare_err);
    }

    return true;
}

bool FewShotLearningTaskService::buildTrainingSpec(const FewShotLearningRunContext &context,
                                                   ExternalProcessSpec &process_spec, QString *err_msg) const
{
    process_spec = {};
    if (context.train_task_id < 0)
        return setError(err_msg, QString("训练任务 ID 无效"));
    process_spec.task_id   = context.train_task_id;
    process_spec.program   = context.python_executable;
    process_spec.arguments = {
        context.train_script,
        QStringLiteral("--datapath"),
        context.train_manifest_path,
        QStringLiteral("--val_datapath"),
        context.validation_manifest_path.isEmpty() ? context.train_manifest_path : context.validation_manifest_path,
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
        process_spec.arguments << QStringLiteral("--sam2_checkpoint") << context.sam2_checkpoint;
    process_spec.working_directory = context.fs_sam2_root;
    process_spec.python_paths      = context.python_paths;
    process_spec.log_path          = processLogPath(context, QStringLiteral("train"));
    return true;
}

bool FewShotLearningTaskService::buildPredictionSpec(const FewShotLearningRunContext &context,
                                                     ExternalProcessSpec &process_spec, QString *err_msg) const
{
    process_spec = {};
    if (context.predict_task_id < 0)
        return setError(err_msg, QString("推理任务 ID 无效"));
    process_spec.task_id   = context.predict_task_id;
    process_spec.program   = context.python_executable;
    process_spec.arguments = {
        context.predict_script,
        QStringLiteral("--support_manifest"),
        context.train_manifest_path,
        QStringLiteral("--query_manifest"),
        context.test_manifest_path,
        QStringLiteral("--output_dir"),
        context.output_dir,
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
        QStringLiteral("0"),
        QStringLiteral("--dltool_progress_span"),
        QStringLiteral("100"),
        QStringLiteral("--dltool_finish_on_complete"),
    };
    if (!context.sam2_checkpoint.isEmpty())
        process_spec.arguments << QStringLiteral("--sam2_checkpoint") << context.sam2_checkpoint;
    process_spec.working_directory = context.fs_sam2_root;
    process_spec.python_paths      = context.python_paths;
    process_spec.log_path          = processLogPath(context, QStringLiteral("predict"));
    return true;
}

bool FewShotLearningTaskService::buildBoxToMaskSpec(const FewShotLearningRunContext &context, int split_index,
                                                    ExternalProcessSpec &process_spec, QString *err_msg) const
{
    process_spec = {};
    if (context.box_to_mask_task_id < 0)
        return setError(err_msg, QString("框转 Mask 任务 ID 无效"));

    const int dataset_count = boxToMaskSplitCount(context);
    if (split_index < 0 || split_index >= dataset_count)
        return setError(err_msg, QString("预处理数据集拆分索引无效: %1").arg(split_index));

    const QString manifest_path = split_index == 0 ? context.train_manifest_path : context.validation_manifest_path;
    const int     base          = split_index * 100 / dataset_count;
    const int     end           = (split_index + 1) * 100 / dataset_count;
    const int     span          = std::max(1, end - base);

    process_spec.task_id   = context.box_to_mask_task_id;
    process_spec.program   = context.python_executable;
    process_spec.arguments = {
        context.box_to_mask_script,
        QStringLiteral("--manifest"),
        manifest_path,
        QStringLiteral("--output_manifest"),
        manifest_path,
        QStringLiteral("--sam2_checkpoint"),
        context.sam2_checkpoint,
        QStringLiteral("--sam2_cfg"),
        context.sam2_cfg,
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
    process_spec.working_directory = context.fs_sam2_root;
    process_spec.python_paths      = context.python_paths;
    process_spec.log_path          = processLogPath(context, QStringLiteral("box_to_mask"));
    return true;
}

int FewShotLearningTaskService::boxToMaskSplitCount(const FewShotLearningRunContext &context) const
{
    return context.validation_dataset_dir == context.train_dataset_dir ? 1 : 2;
}

QString FewShotLearningTaskService::trainTaskName()
{
    return QString("小样本学习 训练");
}

QString FewShotLearningTaskService::predictTaskName()
{
    return QString("小样本学习 推理");
}

QString FewShotLearningTaskService::boxToMaskTaskName()
{
    return QString("小样本学习 框转Mask");
}

} // namespace dltool::model
