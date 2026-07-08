#include "feature/FewShotLearningController.h"

#include "common/Utils.h"
#include "common/YamlUtils.h"
#include "core/CoreDef.h"
#include "data/DataSelectionTreeModel.h"
#include "data/DatasetViewModelFactory.h"
#include "feature/FewShotLearningDataProvider.h"
#include "feature/Utils.h"
#include "model/ModelManager.h"
#include "model/ModelTaskTypes.h"
#include "model/TaskManager.h"
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

/// 类别构建数据结构
struct ClassBuildData
{
    int64_t                       label_class_id{-1}; ///< 类别 ID
    QString                       label_class_name;   ///< 类别名称
    QString                       class_dir_name;     ///< 类别目录名
    std::map<int64_t, QImage>     masks_by_image_id;  ///< 按图像 ID 映射的 Mask
    std::map<int64_t, QJsonArray> boxes_by_image_id;  ///< 按图像 ID 映射的检测框
};

using ClassBuildMap = std::map<int64_t, ClassBuildData>;

/// 按训练、验证、测试用途拆分的图像集合
struct FewShotImageSelection
{
    std::map<int64_t, QString> train_images;           ///< 训练图像 ID 到路径
    std::map<int64_t, QString> validation_images;      ///< 验证图像 ID 到路径
    std::map<int64_t, QString> test_images;            ///< 测试图像 ID 到路径
    std::map<int64_t, int64_t> test_image_dataset_ids; ///< 测试图像 ID 到数据集 ID
};

/// 预测结果导入目标
struct PredictionImportTarget
{
    int64_t dataset_id{-1}; ///< 数据集 ID
    QString manifest_path;  ///< 图像清单文件路径
};

constexpr const char *kFsSam2FrameworkName = "FS-SAM2";

/**
 * @brief 获取 SAM2 配置文件根目录
 * @return SAM2 配置根目录绝对路径
 */
QString fixedSam2ConfigRoot()
{
    return dltool::common::runtimePath(QStringLiteral("python/facebookresearch/sam2/sam2/configs"));
}

/**
 * @brief 清理文件名中的非法字符
 * @param value 原始文件名
 * @param fallback 清理后为空时使用的回退值
 * @return 安全的文件名
 */
QString sanitizeFileName(QString value, const QString &fallback)
{
    value = value.trimmed();
    if (value.isEmpty())
        value = fallback;
    value.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")), QStringLiteral("_"));
    value.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral("_"));
    return value.isEmpty() ? fallback : value;
}

using dltool::common::ensureDirectory;
using dltool::settings::settingDouble;
using dltool::settings::settingInt;
using dltool::settings::settingString;

/**
 * @brief 写入文本文件
 * @param path 文件路径
 * @param lines 要写入的文本行
 * @param err_msg 错误信息（输出）
 * @return 成功返回 true
 */
bool writeTextFile(const QString &path, const QStringList &lines, QString &err_msg)
{
    QFileInfo info(path);
    if (!ensureDirectory(info.dir().absolutePath(), err_msg))
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

/**
 * @brief 将 QVariant 转换为 PointF 向量
 * @param value 包含点数据的 QVariant
 * @return PointF 向量
 */
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

/**
 * @brief 获取图像的宽度和高度
 * @param image_path 图像文件路径
 * @param width 宽度（输出）
 * @param height 高度（输出）
 * @return 成功返回 true
 */
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

/**
 * @brief 将 QVariant 点数据转换为 QPolygonF
 * @param value 包含点数据的 QVariant
 * @return 多边形
 */
QPolygonF variantPointsToPolygon(const QVariant &value)
{
    QPolygonF polygon;
    for (const QPointF &point : variantListToPoints(value)) polygon << point;
    return polygon;
}

/**
 * @brief 将标注数据绘制到 Mask 图像上
 * @param mask Mask 图像
 * @param label_data 标注数据
 */
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

bool buildClassTemplates(FewShotLearningDataProvider *data_provider, const std::vector<int64_t> &label_class_ids,
                         ClassBuildMap &classes, QString &err_msg)
{
    for (int64_t label_class_id : label_class_ids)
    {
        const QString class_name = data_provider->labelClassName(label_class_id);
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

FewShotImageSelection collectImagesByDataset(FewShotLearningDataProvider *data_provider,
                                             const std::set<int64_t>     &selected_train_datasets,
                                             const std::set<int64_t>     &selected_validation_datasets,
                                             const std::set<int64_t>     &selected_test_datasets)
{
    FewShotImageSelection selection;
    for (int64_t image_id : data_provider->allImageIds())
    {
        const int64_t dataset_id = data_provider->imageDatasetId(image_id);
        const QString image_path = data_provider->imagePath(image_id);

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

void collectClassLabels(FewShotLearningDataProvider *data_provider, const std::set<int64_t> &selected_classes,
                        const FewShotImageSelection &image_selection, bool is_detection, ClassBuildMap &train_classes,
                        ClassBuildMap &validation_classes)
{
    for (int64_t label_id : data_provider->allLabelIds())
    {
        const int64_t image_id            = data_provider->labelImageId(label_id);
        const auto    train_image_it      = image_selection.train_images.find(image_id);
        const auto    validation_image_it = image_selection.validation_images.find(image_id);
        const bool    in_train            = train_image_it != image_selection.train_images.end();
        const bool    in_validation       = validation_image_it != image_selection.validation_images.end();
        if (!in_train && !in_validation)
            continue;

        const int64_t label_class_id = data_provider->labelClassId(label_id);
        if (selected_classes.find(label_class_id) == selected_classes.end())
            continue;

        const QVariantMap label_data = data_provider->labelData(label_id);
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
                         FewShotLearningDataProvider *data_provider, QString &err_msg)
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
    dltool::common::yaml::setMapValue(image, QStringLiteral("id"), image_id);
    dltool::common::yaml::setMapValue(image, QStringLiteral("path"), dltool::common::cleanPath(image_path));
    dltool::common::yaml::setMapValue(image, QStringLiteral("dataset_id"), data_provider->imageDatasetId(image_id));
    dltool::common::yaml::setMapValue(image, QStringLiteral("dataset_name"),
                                      data_provider->datasetName(data_provider->imageDatasetId(image_id)));
    dltool::common::yaml::setMapValue(image, QStringLiteral("width"), width);
    dltool::common::yaml::setMapValue(image, QStringLiteral("height"), height);
    dltool::common::yaml::setMapValue(image, QStringLiteral("labels"), labels);
    images_by_id.emplace(image_id, image);
    return true;
}

bool writeCustomManifest(const QString &dataset_dir, const QString &split_name, const ClassBuildMap &classes,
                         const std::map<int64_t, QString> &source_images, bool is_detection,
                         FewShotLearningDataProvider *data_provider, QString &err_msg)
{
    if (!ensureDirectory(dataset_dir, err_msg))
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
                if (!appendManifestImage(images_by_id, image_id, image_it->second, data_provider, err_msg))
                    return false;

                YAML::Node labels = images_by_id[image_id][QStringLiteral("labels").toStdString()];
                for (const QJsonValue &box : boxes)
                {
                    ++label_index;
                    YAML::Node label(YAML::NodeType::Map);
                    dltool::common::yaml::setMapValue(label, QStringLiteral("label_id"), label_index);
                    dltool::common::yaml::setMapValue(label, QStringLiteral("label_class_id"), label_class_id);
                    dltool::common::yaml::setMapValue(label, QStringLiteral("label_class_name"),
                                                      class_data.class_dir_name);
                    dltool::common::yaml::setMapValue(label, QStringLiteral("label_class_group"), QString());
                    dltool::common::yaml::setMapValue(label, QStringLiteral("data"),
                                                      dltool::common::yaml::variantToYaml(boxVariantMap(box)));
                    dltool::common::yaml::setMapValue(label, QStringLiteral("mask_path"),
                                                      dltool::common::cleanPath(maskPath(dataset_dir, image_id,
                                                                                        label_index)));
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
            if (!appendManifestImage(images_by_id, image_id, image_it->second, data_provider, err_msg))
                return false;

            ++label_index;
            const QString saved_mask_path = maskPath(dataset_dir, image_id, label_index);
            if (!ensureDirectory(QFileInfo(saved_mask_path).dir().absolutePath(), err_msg) || !mask.save(saved_mask_path))
            {
                err_msg = QString("写入训练 Mask 失败: %1").arg(saved_mask_path);
                return false;
            }

            YAML::Node label(YAML::NodeType::Map);
            dltool::common::yaml::setMapValue(label, QStringLiteral("label_id"), label_index);
            dltool::common::yaml::setMapValue(label, QStringLiteral("label_class_id"), label_class_id);
            dltool::common::yaml::setMapValue(label, QStringLiteral("label_class_name"), class_data.class_dir_name);
            dltool::common::yaml::setMapValue(label, QStringLiteral("label_class_group"), QString());
            dltool::common::yaml::setMapValue(label, QStringLiteral("mask_path"),
                                              dltool::common::cleanPath(saved_mask_path));
            images_by_id[image_id][QStringLiteral("labels").toStdString()].push_back(label);
        }
    }

    YAML::Node manifest(YAML::NodeType::Map);
    YAML::Node images(YAML::NodeType::Sequence);
    dltool::common::yaml::setMapValue(manifest, QStringLiteral("version"), 1);
    dltool::common::yaml::setMapValue(manifest, QStringLiteral("framework"), QStringLiteral("FS-SAM2"));
    dltool::common::yaml::setMapValue(manifest, QStringLiteral("split"), split_name);
    for (const auto &[image_id, image] : images_by_id)
    {
        Q_UNUSED(image_id)
        images.push_back(image);
    }
    dltool::common::yaml::setMapValue(manifest, QStringLiteral("images"), images);

    return dltool::common::yaml::writeFile(manifestPath(dataset_dir), manifest, &err_msg,
                                           QStringLiteral("写入 FS-SAM2 manifest 失败"),
                                           QStringLiteral("生成 FS-SAM2 manifest 失败"));
}

bool writeQueryManifest(FewShotLearningDataProvider *data_provider, const QString &manifest_path,
                        const QString &output_dir,
                        const std::vector<int64_t>       &test_dataset_ids,
                        const std::map<int64_t, QString> &test_images,
                        const std::map<int64_t, int64_t> &test_image_dataset_ids,
                        std::vector<PredictionImportTarget> &import_targets, QString &err_msg)
{
    YAML::Node manifest(YAML::NodeType::Map);
    YAML::Node images(YAML::NodeType::Sequence);
    QStringList                    query_lines;
    std::map<int64_t, QStringList> manifest_lines_by_dataset;

    for (const auto &[image_id, image_path] : test_images)
    {
        const QString alias = fewShotImageAlias(image_id);
        int           width = 0;
        int           height = 0;
        if (!getImageDimensions(image_path, width, height))
        {
            err_msg = QString("无法读取测试图像尺寸: %1").arg(image_path);
            return false;
        }

        YAML::Node labels(YAML::NodeType::Sequence);
        YAML::Node image(YAML::NodeType::Map);
        dltool::common::yaml::setMapValue(image, QStringLiteral("id"), alias);
        dltool::common::yaml::setMapValue(image, QStringLiteral("path"), dltool::common::cleanPath(image_path));
        dltool::common::yaml::setMapValue(image, QStringLiteral("dataset_id"), test_image_dataset_ids.at(image_id));
        dltool::common::yaml::setMapValue(image, QStringLiteral("dataset_name"),
                                          data_provider->datasetName(test_image_dataset_ids.at(image_id)));
        dltool::common::yaml::setMapValue(image, QStringLiteral("width"), width);
        dltool::common::yaml::setMapValue(image, QStringLiteral("height"), height);
        dltool::common::yaml::setMapValue(image, QStringLiteral("labels"), labels);
        images.push_back(image);

        const QString line = QString("%1,%2").arg(alias, image_path);
        manifest_lines_by_dataset[test_image_dataset_ids.at(image_id)].push_back(line);
        query_lines.push_back(line);
    }

    dltool::common::yaml::setMapValue(manifest, QStringLiteral("version"), 1);
    dltool::common::yaml::setMapValue(manifest, QStringLiteral("framework"), QStringLiteral("FS-SAM2"));
    dltool::common::yaml::setMapValue(manifest, QStringLiteral("split"), QStringLiteral("test"));
    dltool::common::yaml::setMapValue(manifest, QStringLiteral("images"), images);
    if (!dltool::common::yaml::writeFile(manifest_path, manifest, &err_msg,
                                         QStringLiteral("写入 FS-SAM2 测试 manifest 失败"),
                                         QStringLiteral("生成 FS-SAM2 测试 manifest 失败")))
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
            err_msg = QString("测试数据集 %1 没有图像").arg(data_provider->datasetName(dataset_id));
            return false;
        }

        const QString import_manifest_path =
            QFileInfo(manifest_path).absoluteDir().filePath(QStringLiteral("test_images_%1.txt").arg(dataset_id));
        if (!writeTextFile(import_manifest_path, lines, err_msg))
            return false;
        import_targets.push_back(PredictionImportTarget{dataset_id, import_manifest_path});
    }
    return true;
}

/// 小样本学习任务类型枚举
enum class FewShotTaskKind
{
    Train,     ///< 训练
    Predict,   ///< 推理
    BoxToMask, ///< 框转 Mask
};

/**
 * @brief 获取小样本学习任务的中文显示名称
 * @param kind 任务类型
 * @return 任务显示名称
 */
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

/**
 * @brief 获取小样本学习任务的强类型任务类型
 * @param kind 任务类型
 * @return 任务类型枚举
 */
dltool::model::ModelTaskType fewShotTaskType(FewShotTaskKind kind)
{
    switch (kind)
    {
    case FewShotTaskKind::Train:
        return dltool::model::ModelTaskType::Train;
    case FewShotTaskKind::Predict:
        return dltool::model::ModelTaskType::Test;
    case FewShotTaskKind::BoxToMask:
        return dltool::model::ModelTaskType::BoxToMask;
    default:
        return dltool::model::ModelTaskType::Unknown;
    }
}

/// SAM2 配置名称解析结果
struct Sam2ConfigNameParts
{
    QString prefix;     ///< 版本前缀（sam2 或 sam2.1）
    QString size_token; ///< 模型尺寸标识（t/s/b+/l）
};

/**
 * @brief 解析 SAM2 架构名称，提取版本前缀和尺寸标识
 * @param architecture_name 架构名称
 * @return 配置名称解析结果
 */
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

/**
 * @brief 根据架构名称获取 SAM2 配置文件路径
 * @param architecture_name 架构名称
 * @param err_msg 错误信息（输出）
 * @return 配置文件路径
 */
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

/**
 * @brief 获取训练检查点文件路径
 * @param fs_sam2_root FS-SAM2 根目录
 * @param logpath 日志路径
 * @return 检查点文件路径
 */
QString checkpointPath(const QString &fs_sam2_root, const QString &logpath)
{
    return dltool::common::cleanPath(QDir(fs_sam2_root).filePath(QString("logs/%1.log/best_model.pt").arg(logpath)));
}

} // namespace

struct FewShotLearningController::RunContext
{
    QString     python_executable;       ///< Python 可执行文件路径
    QString     fs_sam2_root;            ///< FS-SAM2 根目录
    QString     sam2_checkpoint;         ///< SAM2 检查点文件路径
    QString     sam2_cfg;                ///< SAM2 配置文件路径
    QString     run_dir;                 ///< 本次运行目录
    QString     train_dataset_dir;       ///< 训练 manifest 目录
    QString     validation_dataset_dir;  ///< 验证 manifest 目录；为空时使用 train_dataset_dir
    QString     test_dataset_dir;        ///< 测试 manifest 目录
    QString     output_dir;              ///< 输出目录
    QString     train_manifest_path;     ///< 训练 manifest
    QString     validation_manifest_path;///< 验证 manifest
    QString     test_manifest_path;      ///< 测试 manifest
    QString     train_script;            ///< 训练脚本路径
    QString     predict_script;          ///< 推理脚本路径
    QString     box_to_mask_script;      ///< 框转 Mask 脚本路径
    QStringList python_paths;            ///< 额外 Python 搜索路径
    int         box_to_mask_task_id{-1}; ///< 框转 Mask 任务 ID
    QString     checkpoint_path;         ///< 训练检查点路径
    QString     exp_id;                  ///< 实验 ID
    QString     logpath;                 ///< 日志路径

    int    kshot{1};           ///< K-shot 数量
    int    epochs{50};         ///< 训练轮数
    int    batch_size{2};      ///< 批处理大小
    int    num_workers{0};     ///< 数据加载线程数
    int    image_size{1024};   ///< 图像尺寸
    double lr{1e-4};           ///< 学习率
    double weight_decay{1e-6}; ///< 权重衰减

    int     train_task_id{-1};   ///< 训练任务 ID
    int     predict_task_id{-1}; ///< 推理任务 ID
    QString task_host;           ///< 任务服务主机
    quint16 task_port{0};        ///< 任务服务端口

    std::vector<PredictionImportTarget> import_targets; ///< 预测结果导入目标列表
};

FewShotLearningController::FewShotLearningController(FewShotLearningDataProvider *data_provider,
                                                     dltool::data::DataManager *data_manager, QObject *parent)
    : QObject(parent)
    , data_provider_(data_provider)
    , task_manager_(dltool::model::TaskManager::getInstance())
{
    auto *gs = dltool::settings::GlobalSettings::getInstance();
    enabled_ = gs->valueForField(dltool::settings::generated::field::FewShotLearning::Key::Enabled, true).toBool();

    setTrainDatasetViewModel(dltool::data::DatasetViewModelFactory::createDatasetSelectionModel(data_manager, this));
    setValidationDatasetViewModel(
        dltool::data::DatasetViewModelFactory::createDatasetSelectionModel(data_manager, this));
    setTestDatasetViewModel(dltool::data::DatasetViewModelFactory::createDatasetSelectionModel(data_manager, this));

    if (task_manager_)
    {
        connect(task_manager_, &dltool::model::TaskManager::taskStopRequested, this,
                &FewShotLearningController::handleTaskStopRequested);
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
    if (process_ != nullptr)
    {
        process_->kill();
        process_->deleteLater();
        process_ = nullptr;
    }
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

/**
 * @brief 启动小样本学习流程（FS-SAM2），从控制器持有的 ViewModel 中读取选择
 * @return 启动成功返回 true
 */
bool FewShotLearningController::startFsSam2()
{
    QVariantList label_class_ids = selectedLabelClassIdsFromViewModel(train_dataset_view_model_);
    if (label_class_ids.empty())
        label_class_ids = selectedLabelClassIdsFromViewModel(label_class_view_model_);

    return startFsSam2WithIds(selectedDatasetIdsFromViewModel(train_dataset_view_model_),
                              selectedDatasetIdsFromViewModel(validation_dataset_view_model_),
                              selectedDatasetIdsFromViewModel(test_dataset_view_model_), label_class_ids);
}

bool FewShotLearningController::startFsSam2WithIds(const QVariantList &train_dataset_ids,
                                                   const QVariantList &validation_dataset_ids,
                                                   const QVariantList &test_dataset_ids,
                                                   const QVariantList &label_class_ids)
{
    if (running_)
        return false;

    setLastError({});
    stop_requested_ = false;

    RunContext context;
    QString    err_msg;
    if (!prepareRun(parseInt64Ids(train_dataset_ids, true, true), parseInt64Ids(validation_dataset_ids, true, true),
                    parseInt64Ids(test_dataset_ids, true, true), parseInt64Ids(label_class_ids, true, true), context,
                    err_msg))
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
        current_prepare_split_index_ = 0;
        started                      = startBoxToMask(*active_context_, 0, err_msg);
    }
    else
    {
        stage_  = RunStage::Training;
        started = startTraining(*active_context_, err_msg);
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

/// 清除最后一次错误信息
void FewShotLearningController::clearLastError()
{
    setLastError({});
}

/// 取消当前运行的小样本学习任务
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

/**
 * @brief 准备运行环境：验证参数、配置路径、收集数据、注册任务
 * @param train_dataset_ids 训练数据集 ID
 * @param validation_dataset_ids 验证数据集 ID；为空时复用训练数据集
 * @param test_dataset_ids 测试数据集 ID
 * @param label_class_ids 标注类别 ID
 * @param context 运行上下文（输出）
 * @param err_msg 错误信息（输出）
 * @return 准备成功返回 true
 */
bool FewShotLearningController::prepareRun(const std::vector<int64_t> &train_dataset_ids,
                                           const std::vector<int64_t> &validation_dataset_ids,
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
        && data_provider_->method() != dltool::core::DeepLearningMethod::Segmentation
        && data_provider_->method() != dltool::core::DeepLearningMethod::AnomalyDetection)
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

    context.python_executable = dltool::common::pythonExecutableFromEnvPath(
        settingString(global_settings, generated_field::Software::PythonEnvPath));
    if (context.python_executable.isEmpty())
    {
        err_msg = QString("请先在软件设置中配置 Python 环境目录");
        return false;
    }

    const auto fs_sam2_framework
        = dltool::model::registeredFramework(data_provider_->method(), QString::fromUtf8(kFsSam2FrameworkName));
    if (fs_sam2_framework.name.isEmpty())
    {
        err_msg = QString("FS-SAM2 框架未注册");
        return false;
    }

    context.fs_sam2_root = fs_sam2_framework.root;
    context.sam2_checkpoint
        = dltool::common::runtimePath(settingString(global_settings, generated_field::FewShotLearning::Sam2Checkpoint));
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
    context.train_script       = fs_sam2_framework.train_script;
    context.predict_script     = fs_sam2_framework.predict_script;
    context.box_to_mask_script = fs_sam2_framework.scripts.value(QStringLiteral("box_to_mask"));
    context.python_paths       = fs_sam2_framework.python_paths;
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
    const QString output_root_setting
        = dltool::common::cleanPath(settingString(global_settings, generated_field::FewShotLearning::OutputDir));
    const QString project_dir      = QFileInfo(data_provider_->databasePath()).absoluteDir().absolutePath();
    const QString run_id           = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss_zzz"));
    context.exp_id                 = QStringLiteral("dltool_%1").arg(run_id);
    context.logpath                = QStringLiteral("dltool/%1/fold0").arg(context.exp_id);
    context.run_dir                = output_root_setting.isEmpty()
                                       ? QDir(project_dir).filePath(QStringLiteral(".dltool/few_shot/%1").arg(run_id))
                                       : QDir(output_root_setting).filePath(run_id);
    context.train_dataset_dir      = QDir(context.run_dir).filePath(QStringLiteral("datasets/train"));
    context.validation_dataset_dir = validation_dataset_ids.empty()
                                       ? context.train_dataset_dir
                                       : QDir(context.run_dir).filePath(QStringLiteral("datasets/validation"));
    context.test_dataset_dir       = QDir(context.run_dir).filePath(QStringLiteral("datasets/test"));
    context.output_dir             = QDir(context.run_dir).filePath(QStringLiteral("predictions"));
    context.train_manifest_path    = manifestPath(context.train_dataset_dir);
    context.validation_manifest_path = validation_dataset_ids.empty()
                                         ? context.train_manifest_path
                                         : manifestPath(context.validation_dataset_dir);
    context.test_manifest_path     = manifestPath(context.test_dataset_dir);
    context.checkpoint_path        = checkpointPath(context.fs_sam2_root, context.logpath);

    const std::set<int64_t> selected_classes(label_class_ids.begin(), label_class_ids.end());
    const std::set<int64_t> selected_train_datasets(train_dataset_ids.begin(), train_dataset_ids.end());
    const std::set<int64_t> selected_validation_datasets(validation_dataset_ids.begin(), validation_dataset_ids.end());
    const std::set<int64_t> selected_test_datasets(test_dataset_ids.begin(), test_dataset_ids.end());

    ClassBuildMap classes;
    if (!buildClassTemplates(data_provider_, label_class_ids, classes, err_msg))
        return false;

    ClassBuildMap         validation_classes = classes;
    FewShotImageSelection image_selection    = collectImagesByDataset(
        data_provider_, selected_train_datasets, selected_validation_datasets, selected_test_datasets);
    if (image_selection.train_images.empty() || image_selection.test_images.empty())
    {
        err_msg = QString("训练数据集或测试数据集没有图像");
        return false;
    }
    if (!selected_validation_datasets.empty() && image_selection.validation_images.empty())
    {
        err_msg = QString("验证数据集没有图像");
        return false;
    }

    const bool is_detection = data_provider_->method() == dltool::core::DeepLearningMethod::Detection;
    if (is_detection && !QFileInfo::exists(context.box_to_mask_script))
    {
        err_msg = QString("FS-SAM2 目录缺少 box_to_mask.py: %1").arg(context.fs_sam2_root);
        return false;
    }
    collectClassLabels(data_provider_, selected_classes, image_selection, is_detection, classes, validation_classes);

    if (!validateClassBuildData(classes, is_detection, context.kshot, QStringLiteral("训练数据集"), err_msg))
        return false;
    if (!selected_validation_datasets.empty()
        && !validateClassBuildData(validation_classes, is_detection, context.kshot, QStringLiteral("验证数据集"),
                                   err_msg))
    {
        return false;
    }

    if (!ensureDirectory(context.train_dataset_dir, err_msg) || !ensureDirectory(context.test_dataset_dir, err_msg)
        || !ensureDirectory(context.output_dir, err_msg))
    {
        return false;
    }

    if (!writeCustomManifest(context.train_dataset_dir, QStringLiteral("train"), classes, image_selection.train_images,
                             is_detection, data_provider_, err_msg))
    {
        return false;
    }
    if (!selected_validation_datasets.empty()
        && !writeCustomManifest(context.validation_dataset_dir, QStringLiteral("validation"), validation_classes,
                                image_selection.validation_images, is_detection, data_provider_, err_msg))
    {
        return false;
    }
    if (!writeQueryManifest(data_provider_, context.test_manifest_path, context.output_dir, test_dataset_ids,
                            image_selection.test_images, image_selection.test_image_dataset_ids,
                            context.import_targets, err_msg))
        return false;

    QString server_err;
    if (task_manager_.isNull() || !task_manager_->ensureTaskServer(&server_err))
    {
        err_msg = QString("任务通信服务启动失败: %1").arg(server_err);
        return false;
    }

    const QString task_uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    context.train_task_id   = task_manager_->addTask(task_uuid, fewShotTaskName(FewShotTaskKind::Train),
                                                     fewShotTaskType(FewShotTaskKind::Train), false);
    context.predict_task_id = task_manager_->addTask(task_uuid, fewShotTaskName(FewShotTaskKind::Predict),
                                                     fewShotTaskType(FewShotTaskKind::Predict), false);
    if (is_detection)
    {
        context.box_to_mask_task_id = task_manager_->addTask(task_uuid, fewShotTaskName(FewShotTaskKind::BoxToMask),
                                                             fewShotTaskType(FewShotTaskKind::BoxToMask), false);
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

/**
 * @brief 启动训练子进程
 * @param context 运行上下文
 * @param err_msg 错误信息（输出）
 * @return 启动成功返回 true
 */
bool FewShotLearningController::startTraining(const RunContext &context, QString &err_msg)
{
    QStringList arguments = {
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
        arguments << QStringLiteral("--sam2_checkpoint") << context.sam2_checkpoint;

    return startProcess(context, arguments, err_msg);
}

/**
 * @brief 启动推理子进程
 * @param context 运行上下文
 * @param err_msg 错误信息（输出）
 * @return 启动成功返回 true
 */
bool FewShotLearningController::startPrediction(const RunContext &context, QString &err_msg)
{
    if (!ensureDirectory(context.output_dir, err_msg))
        return false;

    QStringList arguments = {
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
        arguments << QStringLiteral("--sam2_checkpoint") << context.sam2_checkpoint;

    return startProcess(context, arguments, err_msg);
}

/**
 * @brief 启动框转 Mask 预处理子进程
 * @param context 运行上下文
 * @param split_index 当前数据集拆分索引
 * @param err_msg 错误信息（输出）
 * @return 启动成功返回 true
 */
bool FewShotLearningController::startBoxToMask(const RunContext &context, int split_index, QString &err_msg)
{
    const int dataset_count = context.validation_dataset_dir == context.train_dataset_dir ? 1 : 2;
    if (split_index < 0 || split_index >= dataset_count)
    {
        err_msg = QString("预处理数据集拆分索引无效: %1").arg(split_index);
        return false;
    }

    const QString manifest_path = split_index == 0 ? context.train_manifest_path : context.validation_manifest_path;
    const int     base          = split_index * 100 / dataset_count;
    const int     end           = (split_index + 1) * 100 / dataset_count;
    const int     span          = std::max(1, end - base);

    QStringList arguments = {
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

    return startProcess(context, arguments, err_msg);
}

/**
 * @brief 启动 Python 子进程
 * @param context 运行上下文
 * @param arguments 命令行参数
 * @param err_msg 错误信息（输出）
 * @return 启动成功返回 true
 */
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

    QProcessEnvironment env               = QProcessEnvironment::systemEnvironment();
    const QString       old_python_path   = env.value(QStringLiteral("PYTHONPATH"));
    QStringList         python_path_parts = context.python_paths;
    if (!old_python_path.isEmpty())
        python_path_parts.append(old_python_path);
    if (!python_path_parts.isEmpty())
        env.insert(QStringLiteral("PYTHONPATH"), python_path_parts.join(QDir::listSeparator()));
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

/// 结束运行并重置所有状态
void FewShotLearningController::finishRun()
{
    if (data_provider_ != nullptr && import_finished_connection_)
        data_provider_->disconnectImportFinished(import_finished_connection_);
    import_finished_connection_ = {};
    active_context_.reset();
    stage_                       = RunStage::Idle;
    current_prepare_split_index_ = 0;
    current_import_index_        = 0;
    importing_predictions_       = false;
    train_task_id_               = -1;
    predict_task_id_             = -1;
    box_to_mask_task_id_         = -1;
    prediction_output_dir_.clear();
    checkpoint_path_.clear();
    setRunning(false);
}

/**
 * @brief 处理子进程结束事件，根据当前阶段决定下一步操作
 * @param exit_code 退出码
 * @param exit_status 退出状态
 */
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
        ++current_prepare_split_index_;
        const int dataset_count
            = active_context_->validation_dataset_dir == active_context_->train_dataset_dir ? 1 : 2;
        const int prepare_count = dataset_count;
        if (current_prepare_split_index_ < prepare_count)
        {
            QString err_msg;
            if (!startBoxToMask(*active_context_, current_prepare_split_index_, err_msg))
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

        stage_ = RunStage::Training;

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

        stage_ = RunStage::Predicting;

        QString err_msg;
        if (!startPrediction(*active_context_, err_msg))
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
        if (task_manager_)
            task_manager_->finishTask(predict_task_id_);
        startPredictionImports();
        return;
    }

    finishRun();
}

/// 开始导入预测结果
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

/// 开始导入下一个预测结果
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

/**
 * @brief 处理预测结果导入完成
 * @param success 是否成功
 * @param message 导入消息
 */
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

/**
 * @brief 处理任务管理器请求停止任务
 * @param task_id 任务 ID
 */
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
