#include "feature/FewShotLearningController.h"

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

/**
 * @brief 清理并规范化路径
 * @param path 原始路径
 * @return 规范化后的路径
 */
QString cleanPath(const QString &path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
}

/**
 * @brief 将相对路径转换为运行时绝对路径
 * @param path 输入路径
 * @return 如果输入为绝对路径则直接返回，否则相对于应用程序目录解析
 */
QString runtimePath(const QString &path)
{
    const QString cleaned = cleanPath(path);
    if (cleaned.isEmpty() || QFileInfo(cleaned).isAbsolute())
        return cleaned;
    return cleanPath(QDir(QCoreApplication::applicationDirPath()).filePath(cleaned));
}

constexpr const char *kFsSam2FrameworkName = "FS-SAM2";

/**
 * @brief 获取 SAM2 配置文件根目录
 * @return SAM2 配置根目录绝对路径
 */
QString fixedSam2ConfigRoot()
{
    return runtimePath(QStringLiteral("python/facebookresearch/sam2/sam2/configs"));
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

using dltool::settings::settingDouble;
using dltool::settings::settingInt;
using dltool::settings::settingString;

/**
 * @brief 从环境路径中查找 Python 可执行文件
 * @param env_path Python 环境目录路径
 * @return Python 可执行文件的绝对路径，未找到则返回空字符串
 */
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

/**
 * @brief 确保目录存在，不存在则创建
 * @param path 目录路径
 * @param err_msg 错误信息（输出）
 * @return 成功返回 true
 */
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

/**
 * @brief 复制文件
 * @param source_path 源文件路径
 * @param target_path 目标文件路径
 * @param err_msg 错误信息（输出）
 * @return 成功返回 true
 */
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

/**
 * @brief 复制图像文件并按别名重命名
 * @param source_path 源图像路径
 * @param target_dir 目标目录
 * @param alias 文件别名（不含后缀）
 * @param copied_path 复制后的完整路径（输出）
 * @param err_msg 错误信息（输出）
 * @return 成功返回 true
 */
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

bool writeBoxesJson(const QString &class_dir, const QJsonObject &boxes_json, QString &err_msg)
{
    QFile boxes_file(QDir(class_dir).filePath(QStringLiteral("boxes.json")));
    if (!boxes_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        err_msg = QString("无法写入 boxes.json: %1, %2").arg(boxes_file.fileName(), boxes_file.errorString());
        return false;
    }
    boxes_file.write(QJsonDocument(boxes_json).toJson(QJsonDocument::Indented));
    return true;
}

bool writeCustomClassData(const QString &class_dir, const ClassBuildData &class_data,
                          const std::map<int64_t, QString> &source_images, bool is_detection, QStringList &entries,
                          QString &err_msg)
{
    const QString image_dir = QDir(class_dir).filePath(QStringLiteral("images"));
    const QString mask_dir  = QDir(class_dir).filePath(QStringLiteral("masks"));
    if (!ensureDir(image_dir, err_msg) || !ensureDir(mask_dir, err_msg))
        return false;

    if (is_detection)
    {
        QJsonObject boxes_json;
        for (const auto &[image_id, boxes] : class_data.boxes_by_image_id)
        {
            const auto image_it = source_images.find(image_id);
            if (image_it == source_images.end())
                continue;

            const QString alias = QStringLiteral("img_%1").arg(image_id);
            QString       copied_path;
            if (!copyImageToAlias(image_it->second, image_dir, alias, copied_path, err_msg))
                return false;
            boxes_json[alias] = boxes;
            entries.push_back(QString("%1,%1").arg(alias));
        }
        return writeBoxesJson(class_dir, boxes_json, err_msg);
    }

    for (const auto &[image_id, mask] : class_data.masks_by_image_id)
    {
        const auto image_it = source_images.find(image_id);
        if (image_it == source_images.end())
            continue;

        const QString alias = QStringLiteral("img_%1").arg(image_id);
        QString       copied_path;
        if (!copyImageToAlias(image_it->second, image_dir, alias, copied_path, err_msg))
            return false;
        if (!mask.save(QDir(mask_dir).filePath(alias + QStringLiteral(".png"))))
        {
            err_msg = QString("写入训练 Mask 失败: %1").arg(alias);
            return false;
        }
        entries.push_back(QString("%1,%1").arg(alias));
    }
    return true;
}

bool writeSupportQueryFiles(const QString &class_dir, const QStringList &entries, int kshot, double support_ratio,
                            QString &err_msg)
{
    const int entry_count = static_cast<int>(entries.size());
    if (entry_count <= kshot)
    {
        err_msg = QString("类别样本数量不足，至少需要 %1 张图像").arg(kshot + 1);
        return false;
    }

    const int support_count
        = std::clamp(static_cast<int>(std::round(entry_count * support_ratio)), kshot, entry_count - 1);
    QStringList support_entries = entries.mid(0, support_count);
    QStringList query_entries   = entries.mid(support_count);
    if (query_entries.empty())
        query_entries.push_back(entries.back());

    return writeTextFile(QDir(class_dir).filePath(QStringLiteral("support.txt")), support_entries, err_msg)
        && writeTextFile(QDir(class_dir).filePath(QStringLiteral("query.txt")), query_entries, err_msg);
}

bool writeCustomDataset(const QString &dataset_dir, const ClassBuildMap &classes,
                        const std::map<int64_t, QString> &source_images, bool is_detection, int kshot,
                        double support_ratio, QString &err_msg)
{
    for (const auto &[label_class_id, class_data] : classes)
    {
        Q_UNUSED(label_class_id)
        const QString class_dir = QDir(dataset_dir).filePath(class_data.class_dir_name);
        QStringList   entries;
        if (!writeCustomClassData(class_dir, class_data, source_images, is_detection, entries, err_msg)
            || !writeSupportQueryFiles(class_dir, entries, kshot, support_ratio, err_msg))
        {
            return false;
        }
    }
    return true;
}

bool writePredictionQueryInputs(FewShotLearningDataProvider *data_provider, const QString &run_dir,
                                const QString &query_dir, const QString &output_dir, const QString &query_txt_path,
                                const std::vector<int64_t>          &test_dataset_ids,
                                const std::map<int64_t, QString>    &test_images,
                                const std::map<int64_t, int64_t>    &test_image_dataset_ids,
                                std::vector<PredictionImportTarget> &import_targets, QString &err_msg)
{
    QStringList                    query_lines;
    std::map<int64_t, QStringList> manifest_lines_by_dataset;
    for (const auto &[image_id, image_path] : test_images)
    {
        const QString alias = QStringLiteral("img_%1").arg(image_id);
        QString       copied_path;
        if (!copyImageToAlias(image_path, query_dir, alias, copied_path, err_msg))
            return false;

        const QString line = QString("%1,%2").arg(alias, image_path);
        manifest_lines_by_dataset[test_image_dataset_ids.at(image_id)].push_back(line);
        query_lines.push_back(QString("%1,%1").arg(alias));
    }

    if (!writeTextFile(query_txt_path, query_lines, err_msg)
        || !writeTextFile(QDir(output_dir).filePath(QStringLiteral("query.txt")), query_lines, err_msg))
    {
        return false;
    }

    for (int64_t dataset_id : test_dataset_ids)
    {
        const QStringList lines = manifest_lines_by_dataset[dataset_id];
        if (lines.empty())
        {
            err_msg = QString("测试数据集 %1 没有图像").arg(data_provider->datasetName(dataset_id));
            return false;
        }

        const QString manifest_path = QDir(run_dir).filePath(QStringLiteral("test_images_%1.txt").arg(dataset_id));
        if (!writeTextFile(manifest_path, lines, err_msg))
            return false;
        import_targets.push_back(PredictionImportTarget{dataset_id, manifest_path});
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

/// 小样本学习类别字段枚举
enum class FewShotClassField
{
    Id,   ///< 类别 ID
    Name, ///< 类别名称
    Dir,  ///< 类别目录
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

/**
 * @brief 获取类别字段的 JSON 键名
 * @param field 字段类型
 * @return JSON 键名
 */
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

QJsonArray classObjectsFromBuildData(const ClassBuildMap &classes)
{
    QJsonArray objects;
    for (const auto &[label_class_id, class_data] : classes)
    {
        Q_UNUSED(label_class_id)
        QJsonObject class_object;
        class_object[fewShotClassFieldName(FewShotClassField::Id)]   = static_cast<qint64>(class_data.label_class_id);
        class_object[fewShotClassFieldName(FewShotClassField::Name)] = class_data.label_class_name;
        class_object[fewShotClassFieldName(FewShotClassField::Dir)]  = class_data.class_dir_name;
        objects.append(class_object);
    }
    return objects;
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

    const QString path = cleanPath(
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
    return cleanPath(QDir(fs_sam2_root).filePath(QString("logs/%1.log/best_model.pt").arg(logpath)));
}

} // namespace

struct FewShotLearningController::RunContext
{
    QString     python_executable;       ///< Python 可执行文件路径
    QString     fs_sam2_root;            ///< FS-SAM2 根目录
    QString     sam2_checkpoint;         ///< SAM2 检查点文件路径
    QString     sam2_cfg;                ///< SAM2 配置文件路径
    QString     run_dir;                 ///< 本次运行目录
    QString     custom_dataset_dir;      ///< 自定义数据集目录
    QString     validation_dataset_dir;  ///< 验证数据集目录；为空时使用 custom_dataset_dir
    QString     query_dir;               ///< 查询图像目录
    QString     output_dir;              ///< 输出目录
    QString     query_txt_path;          ///< 查询清单文件路径
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
    double support_ratio{0.5}; ///< 支持集比例

    int     train_task_id{-1};   ///< 训练任务 ID
    int     predict_task_id{-1}; ///< 推理任务 ID
    QString task_host;           ///< 任务服务主机
    quint16 task_port{0};        ///< 任务服务端口

    QJsonArray                          classes;        ///< 类别 JSON 数组
    std::vector<PredictionImportTarget> import_targets; ///< 预测结果导入目标列表
};

FewShotLearningController::FewShotLearningController(FewShotLearningDataProvider *data_provider,
                                                     dltool::data::DataManager   *data_manager,
                                                     QObject *parent)
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

    context.python_executable
        = pythonExecutableFromEnvPath(settingString(global_settings, generated_field::Software::PythonEnvPath));
    if (context.python_executable.isEmpty())
    {
        err_msg = QString("请先在软件设置中配置 Python 环境目录");
        return false;
    }

    const auto fs_sam2_framework = dltool::model::ModelManager::registeredFramework(
        data_provider_->method(), QString::fromUtf8(kFsSam2FrameworkName));
    if (fs_sam2_framework.name.isEmpty())
    {
        err_msg = QString("FS-SAM2 框架未注册");
        return false;
    }

    context.fs_sam2_root = fs_sam2_framework.root;
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
    context.support_ratio
        = std::clamp(settingDouble(global_settings, generated_field::FewShotLearning::SupportRatio, 0.5), 0.1, 0.9);

    const QString output_root_setting
        = cleanPath(settingString(global_settings, generated_field::FewShotLearning::OutputDir));
    const QString project_dir      = QFileInfo(data_provider_->databasePath()).absoluteDir().absolutePath();
    const QString run_id           = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss_zzz"));
    context.exp_id                 = QStringLiteral("dltool_%1").arg(run_id);
    context.logpath                = QStringLiteral("dltool/%1/fold0").arg(context.exp_id);
    context.run_dir                = output_root_setting.isEmpty()
                                       ? QDir(project_dir).filePath(QStringLiteral(".dltool/few_shot/%1").arg(run_id))
                                       : QDir(output_root_setting).filePath(run_id);
    context.custom_dataset_dir     = QDir(context.run_dir).filePath(QStringLiteral("custom/train"));
    context.validation_dataset_dir = validation_dataset_ids.empty()
                                       ? context.custom_dataset_dir
                                       : QDir(context.run_dir).filePath(QStringLiteral("custom/val"));
    context.query_dir              = QDir(context.run_dir).filePath(QStringLiteral("query"));
    context.output_dir             = QDir(context.run_dir).filePath(QStringLiteral("predictions"));
    context.query_txt_path         = QDir(context.query_dir).filePath(QStringLiteral("query.txt"));
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

    if (!ensureDir(context.custom_dataset_dir, err_msg) || !ensureDir(context.query_dir, err_msg)
        || !ensureDir(context.output_dir, err_msg))
    {
        return false;
    }

    if (!writeCustomDataset(context.custom_dataset_dir, classes, image_selection.train_images, is_detection,
                            context.kshot, context.support_ratio, err_msg))
    {
        return false;
    }
    if (!selected_validation_datasets.empty()
        && !writeCustomDataset(context.validation_dataset_dir, validation_classes, image_selection.validation_images,
                               is_detection, context.kshot, context.support_ratio, err_msg))
    {
        return false;
    }
    context.classes = classObjectsFromBuildData(classes);

    if (!writePredictionQueryInputs(data_provider_, context.run_dir, context.query_dir, context.output_dir,
                                    context.query_txt_path, test_dataset_ids, image_selection.test_images,
                                    image_selection.test_image_dataset_ids, context.import_targets, err_msg))
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
        context.custom_dataset_dir,
        QStringLiteral("--val_datapath"),
        context.validation_dataset_dir.isEmpty() ? context.custom_dataset_dir : context.validation_dataset_dir,
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
 * @param class_index 当前类别索引
 * @param err_msg 错误信息（输出）
 * @return 启动成功返回 true
 */
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

/**
 * @brief 启动框转 Mask 预处理子进程
 * @param context 运行上下文
 * @param class_index 当前类别索引
 * @param err_msg 错误信息（输出）
 * @return 启动成功返回 true
 */
bool FewShotLearningController::startBoxToMask(const RunContext &context, int class_index, QString &err_msg)
{
    const int class_count   = static_cast<int>(context.classes.size());
    const int dataset_count = context.validation_dataset_dir == context.custom_dataset_dir ? 1 : 2;
    const int prepare_count = class_count * dataset_count;
    if (class_index < 0 || class_index >= prepare_count)
    {
        err_msg = QString("预处理类别索引无效: %1").arg(class_index);
        return false;
    }

    const int         actual_class_index = class_index % class_count;
    const int         dataset_index      = class_index / class_count;
    const QJsonObject class_object       = context.classes.at(actual_class_index).toObject();
    const QString     class_dir_name     = class_object.value(fewShotClassFieldName(FewShotClassField::Dir)).toString();
    if (class_dir_name.isEmpty())
    {
        err_msg = QString("预处理类别目录为空");
        return false;
    }

    const QString dataset_dir        = dataset_index == 0 ? context.custom_dataset_dir : context.validation_dataset_dir;
    const QString support_dir        = QDir(dataset_dir).filePath(class_dir_name);
    const int     safe_prepare_count = std::max(1, prepare_count);
    const int     base               = class_index * 100 / safe_prepare_count;
    const int     end                = (class_index + 1) * 100 / safe_prepare_count;
    const int     span               = std::max(1, end - base);

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
        ++current_prepare_class_index_;
        const int class_count = static_cast<int>(active_context_->classes.size());
        const int dataset_count
            = active_context_->validation_dataset_dir == active_context_->custom_dataset_dir ? 1 : 2;
        const int prepare_count = class_count * dataset_count;
        if (current_prepare_class_index_ < prepare_count)
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
