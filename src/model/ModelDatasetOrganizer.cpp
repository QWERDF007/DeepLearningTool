#include "model/ModelDatasetOrganizer.h"

#include "common/MaskPolygonUtils.h"
#include "common/Utils.h"
#include "common/YamlUtils.h"
#include "core/CoreDef.h"
#include "data/DatasetExportSource.h"
#include "data/DatasetIO.h"
#include "model/ModelTaskTypes.h"

#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QList>
#include <QPair>
#include <QPointF>
#include <QSaveFile>
#include <QStringConverter>
#include <QStringList>
#include <QTextStream>
#include <QMap>
#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

using dltool::common::cleanPath;
using dltool::common::ensureDirectory;
using dltool::common::yaml::setMapValue;
using dltool::common::yaml::variantToYaml;

namespace dltool::model {

namespace {

enum class FrameworkDatasetLayout
{
    Generic,
    Anomalib,
    Dinomaly2,
    Ultralytics,
    FsSam2,
};

enum class DatasetSplit
{
    Train,
    Validation,
    Test,
};

enum class DatasetConfigField
{
    Train,
    Validation,
    Test,
    FileList,
    MasksDir,
    ImageCount,
    LabelCount,
    GoodValues,
    AnomalyValues,
    IgnoreValues,
};

enum class DatasetFileName
{
    Mask,
    ImageMask,
    SplitFileList,
};

enum class LabelDataField
{
    Points,
    X,
    Y,
    Width,
    Height,
};

enum class GenericImageField
{
    Id,
    Path,
    DatasetId,
    DatasetName,
    Width,
    Height,
    ImageLabelClassId,
    ImageLabelClassName,
    ImageLabelGroup,
    LabelIndex,
    Anomaly,
    Labels,
};

enum class GenericLabelField
{
    LabelId,
    LabelClassId,
    LabelClassName,
    LabelClassGroup,
    ClassIndex,
    Data,
};

enum class YoloLabelField
{
    Yolo,
    Cx,
    Cy,
    Width,
    Height,
};

enum class FsSam2LabelField
{
    MaskPath,
};

enum class ImageLevelLabelField
{
    LabelClassId,
    LabelClassName,
    Group,
};

enum class LabelGroupName
{
    Good,
    Anomaly,
    Unlabeled,
};

const std::map<QString, FrameworkDatasetLayout> &frameworkDatasetLayouts()
{
    static const std::map<QString, FrameworkDatasetLayout> layouts = {
        {   QStringLiteral("anomalib"),    FrameworkDatasetLayout::Anomalib},
        {   QStringLiteral("dinomaly2"),    FrameworkDatasetLayout::Dinomaly2},
        {QStringLiteral("ultralytics"), FrameworkDatasetLayout::Ultralytics},
        {    QStringLiteral("fs-sam2"),      FrameworkDatasetLayout::FsSam2},
    };
    return layouts;
}

const std::map<DatasetSplit, QString> &datasetSplitNames()
{
    static const std::map<DatasetSplit, QString> names = {
        {     DatasetSplit::Train,      QStringLiteral("train")},
        {DatasetSplit::Validation, QStringLiteral("validation")},
        {      DatasetSplit::Test,       QStringLiteral("test")},
    };
    return names;
}

const std::map<DatasetConfigField, QString> &datasetConfigFieldNames()
{
    static const std::map<DatasetConfigField, QString> names = {
        {     DatasetConfigField::Train,       QStringLiteral("train")},
        {DatasetConfigField::Validation,  QStringLiteral("validation")},
        {      DatasetConfigField::Test,        QStringLiteral("test")},
        {  DatasetConfigField::FileList,   QStringLiteral("file_list")},
        {  DatasetConfigField::MasksDir,   QStringLiteral("masks_dir")},
        {DatasetConfigField::ImageCount, QStringLiteral("image_count")},
        {DatasetConfigField::LabelCount, QStringLiteral("label_count")},
        { DatasetConfigField::GoodValues,  QStringLiteral("good_values")},
        {DatasetConfigField::AnomalyValues, QStringLiteral("anomaly_values")},
        {DatasetConfigField::IgnoreValues, QStringLiteral("ignore_values")},
    };
    return names;
}

const std::map<DatasetFileName, QString> &datasetFileNames()
{
    static const std::map<DatasetFileName, QString> names = {
        {         DatasetFileName::Mask, QStringLiteral("image_%1_label_%2.png")},
        {    DatasetFileName::ImageMask,                QStringLiteral("%1.png")},
        {DatasetFileName::SplitFileList,                QStringLiteral("%1.txt")},
    };
    return names;
}

const std::map<LabelDataField, QString> &labelDataFieldNames()
{
    static const std::map<LabelDataField, QString> names = {
        {LabelDataField::Points, QStringLiteral("points")},
        {     LabelDataField::X,      QStringLiteral("x")},
        {     LabelDataField::Y,      QStringLiteral("y")},
        { LabelDataField::Width,  QStringLiteral("width")},
        {LabelDataField::Height, QStringLiteral("height")},
    };
    return names;
}

const std::map<GenericImageField, QString> &genericImageFieldNames()
{
    static const std::map<GenericImageField, QString> names = {
        {                 GenericImageField::Id,                     QStringLiteral("id")},
        {               GenericImageField::Path,                   QStringLiteral("path")},
        {          GenericImageField::DatasetId,             QStringLiteral("dataset_id")},
        {        GenericImageField::DatasetName,           QStringLiteral("dataset_name")},
        {              GenericImageField::Width,                  QStringLiteral("width")},
        {             GenericImageField::Height,                 QStringLiteral("height")},
        {  GenericImageField::ImageLabelClassId,   QStringLiteral("image_label_class_id")},
        {GenericImageField::ImageLabelClassName, QStringLiteral("image_label_class_name")},
        {    GenericImageField::ImageLabelGroup,      QStringLiteral("image_label_group")},
        {         GenericImageField::LabelIndex,            QStringLiteral("label_index")},
        {            GenericImageField::Anomaly,                QStringLiteral("anomaly")},
        {             GenericImageField::Labels,                 QStringLiteral("labels")},
    };
    return names;
}

const std::map<GenericLabelField, QString> &genericLabelFieldNames()
{
    static const std::map<GenericLabelField, QString> names = {
        {        GenericLabelField::LabelId,          QStringLiteral("label_id")},
        {   GenericLabelField::LabelClassId,    QStringLiteral("label_class_id")},
        { GenericLabelField::LabelClassName,  QStringLiteral("label_class_name")},
        {GenericLabelField::LabelClassGroup, QStringLiteral("label_class_group")},
        {     GenericLabelField::ClassIndex,       QStringLiteral("class_index")},
        {           GenericLabelField::Data,              QStringLiteral("data")},
    };
    return names;
}

const std::map<YoloLabelField, QString> &yoloLabelFieldNames()
{
    static const std::map<YoloLabelField, QString> names = {
        {  YoloLabelField::Yolo,   QStringLiteral("yolo")},
        {    YoloLabelField::Cx,     QStringLiteral("cx")},
        {    YoloLabelField::Cy,     QStringLiteral("cy")},
        { YoloLabelField::Width,  QStringLiteral("width")},
        {YoloLabelField::Height, QStringLiteral("height")},
    };
    return names;
}

const std::map<FsSam2LabelField, QString> &fsSam2LabelFieldNames()
{
    static const std::map<FsSam2LabelField, QString> names = {
        {FsSam2LabelField::MaskPath, QStringLiteral("mask_path")},
    };
    return names;
}

const std::map<ImageLevelLabelField, QString> &imageLevelLabelFieldNames()
{
    static const std::map<ImageLevelLabelField, QString> names = {
        {  ImageLevelLabelField::LabelClassId,   QStringLiteral("label_class_id")},
        {ImageLevelLabelField::LabelClassName, QStringLiteral("label_class_name")},
        {         ImageLevelLabelField::Group,            QStringLiteral("group")},
    };
    return names;
}

const std::map<LabelGroupName, QString> &labelGroupNames()
{
    static const std::map<LabelGroupName, QString> names = {
        {   LabelGroupName::Good, QStringLiteral("good")},
        {LabelGroupName::Anomaly, QStringLiteral("anomaly")},
        {LabelGroupName::Unlabeled, QStringLiteral("unlabeled")},
    };
    return names;
}

template<typename Key, typename Value>
Value mappedValue(const std::map<Key, Value> &values, const Key &key, const Value &fallback = Value())
{
    const auto found = values.find(key);
    return found != values.end() ? found->second : fallback;
}

FrameworkDatasetLayout datasetLayout(const QString &framework_name)
{
    const QString key = framework_name.trimmed().toLower();
    return mappedValue(frameworkDatasetLayouts(), key, FrameworkDatasetLayout::Generic);
}

bool isAnomalyGroup(const QString &group)
{
    return group.compare(mappedValue(labelGroupNames(), LabelGroupName::Anomaly), Qt::CaseInsensitive) == 0;
}

int classIndex(qint64 label_class_id, std::map<qint64, int> &class_indices)
{
    if (label_class_id < 0)
        return -1;
    const auto found = class_indices.find(label_class_id);
    if (found != class_indices.end())
        return found->second;
    const int next = static_cast<int>(class_indices.size());
    class_indices.insert({label_class_id, next});
    return next;
}

QVariantMap labelYoloData(const QVariantMap &label_data, int image_width, int image_height)
{
    if (image_width <= 0 || image_height <= 0)
        return {};

    const double bbox_x = label_data.value(mappedValue(labelDataFieldNames(), LabelDataField::X)).toDouble();
    const double bbox_y = label_data.value(mappedValue(labelDataFieldNames(), LabelDataField::Y)).toDouble();
    const double width  = label_data.value(mappedValue(labelDataFieldNames(), LabelDataField::Width)).toDouble();
    const double height = label_data.value(mappedValue(labelDataFieldNames(), LabelDataField::Height)).toDouble();
    if (width <= 0 || height <= 0)
        return {};

    return {
        {mappedValue(yoloLabelFieldNames(),     YoloLabelField::Cx),   (bbox_x + width / 2.0) / image_width},
        {mappedValue(yoloLabelFieldNames(),     YoloLabelField::Cy), (bbox_y + height / 2.0) / image_height},
        {mappedValue(yoloLabelFieldNames(),  YoloLabelField::Width),                    width / image_width},
        {mappedValue(yoloLabelFieldNames(), YoloLabelField::Height),                  height / image_height},
    };
}

std::vector<QPointF> labelPolygon(const QVariantMap &label_data)
{
    std::vector<QPointF> points = dltool::data::DatasetIO::variantListToPoints(
        label_data.value(mappedValue(labelDataFieldNames(), LabelDataField::Points)));
    if (points.size() >= 3)
        return points;

    const double x = label_data.value(mappedValue(labelDataFieldNames(), LabelDataField::X)).toDouble();
    const double y = label_data.value(mappedValue(labelDataFieldNames(), LabelDataField::Y)).toDouble();
    const double w = label_data.value(mappedValue(labelDataFieldNames(), LabelDataField::Width)).toDouble();
    const double h = label_data.value(mappedValue(labelDataFieldNames(), LabelDataField::Height)).toDouble();
    if (w <= 0.0 || h <= 0.0)
        return {};

    return {
        QPointF(x, y),
        QPointF(x + w, y),
        QPointF(x + w, y + h),
        QPointF(x, y + h),
    };
}

QString fsSam2MaskPath(const QString &masks_dir, qint64 image_id, qint64 label_id)
{
    return QDir(masks_dir).filePath(mappedValue(datasetFileNames(), DatasetFileName::Mask).arg(image_id).arg(label_id));
}

bool hasFsSam2BoxPrompt(const QVariantMap &label_data)
{
    const double width  = label_data.value(mappedValue(labelDataFieldNames(), LabelDataField::Width)).toDouble();
    const double height = label_data.value(mappedValue(labelDataFieldNames(), LabelDataField::Height)).toDouble();
    if (width > 0.0 && height > 0.0)
        return true;

    const std::vector<QPointF> points = dltool::data::DatasetIO::variantListToPoints(
        label_data.value(mappedValue(labelDataFieldNames(), LabelDataField::Points)));
    return points.size() >= 2;
}

QString anomalibMaskFileName(qint64 image_id)
{
    return mappedValue(datasetFileNames(), DatasetFileName::ImageMask).arg(image_id);
}

QString anomalibFileListPath(const QString &dataset_dir, const QString &split_name)
{
    return QDir(dataset_dir).filePath(mappedValue(datasetFileNames(), DatasetFileName::SplitFileList).arg(split_name));
}

QString splitLabelsPath(const QString &dataset_dir, const QString &split_name)
{
    return QDir(dataset_dir).filePath(QString("%1_labels.json").arg(split_name));
}

bool writeImageFileList(const QString &path, const QList<QPair<qint64, QString>> &images, QString *err_msg)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if (err_msg != nullptr)
            *err_msg = QString("打开数据集文件列表失败: %1").arg(file.errorString());
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << QString("image_id,image_path\n");
    for (const auto &[image_id, raw_path] : images)
    {
        QString image_path = raw_path;
        image_path.replace(QString("\""), QString("\"\""));
        if (image_path.contains(QChar(',')) || image_path.contains(QChar('"')) || image_path.contains(QChar('\n'))
            || image_path.contains(QChar('\r')))
            image_path = QString("\"") + image_path + QString("\"");
        stream << image_id << QChar(',') << image_path << QChar('\n');
    }
    if (!file.commit())
    {
        if (err_msg != nullptr)
            *err_msg = QString("提交数据集文件列表失败: %1").arg(file.errorString());
        return false;
    }
    return true;
}

QString numberText(const double value)
{
    return QString::number(value, 'g', 12);
}

QString escapedYamlString(QString value)
{
    value.replace(QString("'"), QString("''"));
    return QString("'%1'").arg(value);
}

bool writeUltralyticsDatasetConfig(const QString &path, const QString &dataset_dir,
                                   const QMap<int, QString> &class_names,
                                   const QMap<int, qint64> &class_ids, QString *err_msg)
{
    if (class_names.isEmpty() && class_ids.isEmpty())
        return true;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if (err_msg != nullptr)
            *err_msg = QString("打开 Ultralytics 数据集配置失败: %1").arg(file.errorString());
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << QStringLiteral("path: ") << escapedYamlString(cleanPath(dataset_dir)) << QChar('\n');
    stream << QStringLiteral("train: ") << escapedYamlString(QStringLiteral("train.txt")) << QChar('\n');
    stream << QStringLiteral("val: ") << escapedYamlString(QStringLiteral("validation.txt")) << QChar('\n');
    stream << QStringLiteral("test: ") << escapedYamlString(QStringLiteral("test.txt")) << QChar('\n');
    stream << QStringLiteral("names:\n");
    for (auto it = class_names.cbegin(); it != class_names.cend(); ++it)
        stream << QStringLiteral("  ") << it.key() << QStringLiteral(": ") << escapedYamlString(it.value())
               << QChar('\n');
    stream << QStringLiteral("class_ids:\n");
    for (auto it = class_ids.cbegin(); it != class_ids.cend(); ++it)
        stream << QStringLiteral("  ") << it.key() << QStringLiteral(": ") << it.value() << QChar('\n');

    if (!file.commit())
    {
        if (err_msg != nullptr)
            *err_msg = QString("提交 Ultralytics 数据集配置失败: %1").arg(file.errorString());
        return false;
    }
    return true;
}

void removeLegacyAnomalibSplitFiles(const QString &dataset_dir, const QString &train_dir)
{
    const auto remove_if_inside = [](const QString &root, const QString &name)
    {
        const QString clean_root = cleanPath(QFileInfo(root).absoluteFilePath());
        if (clean_root.isEmpty())
            return;
        const QString path = cleanPath(QFileInfo(QDir(clean_root).filePath(name)).absoluteFilePath());
        if (path.startsWith(clean_root + QStringLiteral("/"), Qt::CaseInsensitive) && QFileInfo::exists(path))
            QFile::remove(path);
    };

    const QStringList dataset_names = {QStringLiteral("train.txt"),         QStringLiteral("validation.txt"),
                                       QStringLiteral("train_labels.json"), QStringLiteral("validation_labels.json"),
                                       QStringLiteral("train.yaml"),        QStringLiteral("validation.yaml")};
    const QStringList train_names   = {QStringLiteral("train.yaml"), QStringLiteral("validation.yaml")};
    for (const QString &name : dataset_names) remove_if_inside(dataset_dir, name);
    for (const QString &name : train_names) remove_if_inside(train_dir, name);
}

bool writeJsonFile(const QString &path, const QVariant &value, QString *err_msg, const QString &message)
{
    const QByteArray encoded = QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact);
    if (encoded.isEmpty())
    {
        if (err_msg != nullptr)
            *err_msg = QString("%1: JSON 编码失败").arg(message);
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if (err_msg != nullptr)
            *err_msg = QString("%1: %2").arg(message, file.errorString());
        return false;
    }
    if (file.write(encoded) != encoded.size() || !file.commit())
    {
        if (err_msg != nullptr)
            *err_msg = QString("%1: %2").arg(message, file.errorString());
        return false;
    }
    return true;
}

QString writeFsSam2LabelMask(const QVariantMap &label_data, int image_width, int image_height, const QString &masks_dir,
                             qint64 image_id, qint64 label_id, QString *err_msg)
{
    const std::vector<QPointF> polygon = labelPolygon(label_data);
    if (polygon.size() < 3)
        return {};

    const std::vector<uint8_t> mask = dltool::common::polygons2Mask({polygon}, image_width, image_height);
    if (mask.empty())
        return {};

    const QString path = fsSam2MaskPath(masks_dir, image_id, label_id);
    QImage        image(mask.data(), image_width, image_height, image_width, QImage::Format_Grayscale8);
    if (image.isNull() || !image.save(path))
    {
        if (err_msg != nullptr)
            *err_msg = QString("写入 FS-SAM2 mask 失败: %1").arg(path);
        return {};
    }
    return cleanPath(path);
}

QString writeAnomalibImageMask(const std::vector<std::vector<QPointF>> &polygons, int image_width, int image_height,
                               const QString &masks_dir, qint64 image_id, QString *err_msg)
{
    if (image_width <= 0 || image_height <= 0)
    {
        if (err_msg != nullptr)
            *err_msg = QString("写入 anomalib mask 失败: 图像尺寸无效, image_id=%1").arg(image_id);
        return {};
    }

    const std::vector<uint8_t> mask = dltool::common::polygons2Mask(polygons, image_width, image_height);
    if (mask.empty())
    {
        if (err_msg != nullptr)
            *err_msg = QString("写入 anomalib mask 失败: mask 为空, image_id=%1").arg(image_id);
        return {};
    }

    const QString file_name = anomalibMaskFileName(image_id);
    const QString path      = QDir(masks_dir).filePath(file_name);
    QImage        image(mask.data(), image_width, image_height, image_width, QImage::Format_Grayscale8);
    if (image.isNull() || !image.save(path))
    {
        if (err_msg != nullptr)
            *err_msg = QString("写入 anomalib mask 失败: %1").arg(path);
        return {};
    }
    return file_name;
}

QString writeDinomaly2ImageMask(const std::map<qint64, std::vector<std::vector<QPointF>>> &class_polygons,
                                int image_width, int image_height, const QString &masks_dir, qint64 image_id,
                                QString *err_msg)
{
    if (image_width <= 0 || image_height <= 0)
    {
        if (err_msg != nullptr)
            *err_msg = QString("写入 Dinomaly2 mask 失败: 图像尺寸无效, image_id=%1").arg(image_id);
        return {};
    }

    const int64_t total = static_cast<int64_t>(image_width) * static_cast<int64_t>(image_height);
    std::vector<uint8_t> result(static_cast<size_t>(total), 0);
    for (const auto &[class_id, polygons] : class_polygons)
    {
        // Mask 为 8 位灰度图，类别值必须落在 1..255 内（0 为背景）。
        if (class_id <= 0 || class_id > 255)
        {
            if (err_msg != nullptr)
                *err_msg = QString("Dinomaly2 mask 类别值超出 1..255 范围: class_id=%1").arg(class_id);
            return {};
        }

        const std::vector<uint8_t> layer
            = dltool::common::polygons2Mask(polygons, image_width, image_height, static_cast<uint8_t>(class_id));
        if (layer.size() != result.size())
        {
            if (err_msg != nullptr)
                *err_msg = QString("写入 Dinomaly2 mask 失败: 多边形栅格化尺寸不匹配, class_id=%1").arg(class_id);
            return {};
        }
        for (size_t index = 0; index < result.size(); ++index)
        {
            if (layer[index] != 0)
                result[index] = layer[index];
        }
    }

    if (std::none_of(result.cbegin(), result.cend(), [](uint8_t value) { return value != 0; }))
        return {};

    const QString file_name = anomalibMaskFileName(image_id);
    const QString path      = QDir(masks_dir).filePath(file_name);
    QImage        image(result.data(), image_width, image_height, image_width, QImage::Format_Grayscale8);
    if (image.isNull() || !image.save(path))
    {
        if (err_msg != nullptr)
            *err_msg = QString("写入 Dinomaly2 mask 失败: %1").arg(path);
        return {};
    }
    return file_name;
}

enum class LabelExportDecision
{
    Keep,
    Skip,
    Error,
};

struct SplitExportContext
{
    const ModelDatasetExportRequest         *request{};
    const dltool::data::DatasetExportSource *source{};
    const ModelDatasetSelection             *selection{};
    DatasetSplit                             split{DatasetSplit::Train};
    QString                                  split_name;
    QString                                  split_dir;
    QString                                  masks_dir;
    YAML::Node                               images{YAML::NodeType::Sequence};
    std::map<qint64, int>                    class_indices;
    QMap<int, QString>                        class_names;
    std::set<qint64>                         good_classes;
    std::set<qint64>                         anomaly_classes;
    std::set<qint64>                         ignore_classes;
    QList<QPair<qint64, QString>>            image_files;
    int                                      image_count{0};
    int                                      label_count{0};
};

QString maskClassValueList(const SplitExportContext &ctx, DatasetConfigField field)
{
    QStringList values;
    const std::set<qint64> *classes = field == DatasetConfigField::GoodValues
                                          ? &ctx.good_classes
                                          : (field == DatasetConfigField::AnomalyValues ? &ctx.anomaly_classes
                                                                                         : &ctx.ignore_classes);
    for (const qint64 class_id : *classes)
        values.push_back(QString::number(class_id));
    return values.join(QStringLiteral(","));
}

QString splitFileListPath(const SplitExportContext &ctx, QString *err_msg)
{
    if (ctx.split == DatasetSplit::Test)
    {
        if (ctx.request == nullptr || ctx.request->test_file_list_path.trimmed().isEmpty())
        {
            if (err_msg != nullptr)
                *err_msg = QString("测试任务文件列表路径为空");
            return {};
        }
        return cleanPath(ctx.request->test_file_list_path);
    }
    return anomalibFileListPath(ctx.request->dataset_dir, ctx.split_name);
}

struct ImageExportContext
{
    qint64                            image_id{-1};
    qint64                            dataset_id{-1};
    QString                           image_path;
    int                               width{0};
    int                               height{0};
    qint64                            image_label_class_id{-1};
    QString                           image_label_class_name;
    QString                           image_label_group;
    bool                              has_anomaly_label{false};
    bool                              selected_by_image_label{false};
    std::vector<std::vector<QPointF>> anomaly_polygons;
    std::map<qint64, std::vector<std::vector<QPointF>>> class_polygons;
};

const ModelDatasetSelection *selectionForSplit(const ModelDatasetExportRequest &request, DatasetSplit split)
{
    switch (split)
    {
    case DatasetSplit::Train:
        return &request.selections.train;
    case DatasetSplit::Validation:
        return &request.selections.validation;
    case DatasetSplit::Test:
        return &request.selections.test;
    }
    return nullptr;
}

class DatasetOrganizerBase
{
public:
    virtual ~DatasetOrganizerBase() = default;

    void setClassMapping(std::map<qint64, int> class_indices, QMap<int, QString> class_names)
    {
        class_indices_ = std::move(class_indices);
        class_names_   = std::move(class_names);
    }

    QVariantMap exportSplit(const ModelDatasetExportRequest &request, DatasetSplit split, bool required,
                            QString *err_msg) const
    {
        if (request.source == nullptr)
        {
            if (err_msg != nullptr)
                *err_msg = QString("模型数据源为空");
            return {};
        }

        const ModelDatasetSelection *selection = selectionForSplit(request, split);
        if (selection == nullptr || selection->isEmpty())
        {
            if (required && err_msg != nullptr)
                *err_msg = QString("未选择%1数据集").arg(mappedValue(datasetSplitNames(), split));
            return {};
        }

        SplitExportContext ctx;
        ctx.request    = &request;
        ctx.source     = request.source;
        ctx.selection  = selection;
        ctx.split      = split;
        ctx.split_name = mappedValue(datasetSplitNames(), split);
        ctx.split_dir  = splitDirectory(request, ctx.split_name);
        ctx.class_indices = class_indices_;
        ctx.class_names   = class_names_;
        if (!prepareSplit(ctx, err_msg))
            return {};

        const std::vector<int64_t> all_image_ids = request.source->allImageIds();
        for (const int64_t image_id : all_image_ids)
        {
            ImageExportContext image;
            if (!readSelectedImage(ctx, image_id, image))
                continue;

            YAML::Node labels(YAML::NodeType::Sequence);
            if (!appendSelectedLabels(ctx, image, labels, err_msg))
                return {};

            image.selected_by_image_label = selection->contains(image.dataset_id, image.image_label_class_id);
            if (!image.selected_by_image_label && labels.size() == 0)
                continue;

            if (!appendImage(ctx, image, labels, err_msg))
                return {};
            ctx.image_files.push_back({image.image_id, imageListPath(ctx, image)});
            ++ctx.image_count;
        }

        if (ctx.image_count <= 0)
        {
            if (required && err_msg != nullptr)
                *err_msg = QString("%1数据集没有可用图像").arg(ctx.split_name);
            return {};
        }

        return finishSplit(ctx, err_msg);
    }

protected:
    virtual QString splitDirectory(const ModelDatasetExportRequest &request, const QString &split_name) const
    {
        Q_UNUSED(split_name)
        return request.dataset_dir;
    }

    virtual bool prepareSplit(SplitExportContext &ctx, QString *err_msg) const
    {
        return ensureDirectory(ctx.split_dir, err_msg, QString("数据集目录为空"), QString("创建数据集目录失败: %1"));
    }

    virtual LabelExportDecision augmentLabel(SplitExportContext &ctx, ImageExportContext &image, qint64 label_id,
                                             qint64 label_class_id, const QString &class_group,
                                             const QVariantMap &label_data, YAML::Node &label, QString *err_msg) const
    {
        Q_UNUSED(ctx)
        Q_UNUSED(image)
        Q_UNUSED(label_id)
        Q_UNUSED(label_class_id)
        Q_UNUSED(class_group)
        Q_UNUSED(label_data)
        Q_UNUSED(label)
        Q_UNUSED(err_msg)
        return LabelExportDecision::Keep;
    }

    virtual bool appendImage(SplitExportContext &ctx, const ImageExportContext &image, const YAML::Node &labels,
                             QString *err_msg) const = 0;

    virtual QString imageListPath(const SplitExportContext &ctx, const ImageExportContext &image) const
    {
        Q_UNUSED(ctx)
        return cleanPath(image.image_path);
    }

    virtual QVariantMap finishSplit(SplitExportContext &ctx, QString *err_msg) const = 0;

    YAML::Node baseLabelNode(SplitExportContext &ctx, qint64 label_id, qint64 label_class_id, const QString &class_name,
                             const QString &class_group, const QVariantMap &label_data) const
    {
        YAML::Node node(YAML::NodeType::Map);
        setMapValue(node, mappedValue(genericLabelFieldNames(), GenericLabelField::LabelId), label_id);
        setMapValue(node, mappedValue(genericLabelFieldNames(), GenericLabelField::LabelClassId), label_class_id);
        setMapValue(node, mappedValue(genericLabelFieldNames(), GenericLabelField::LabelClassName), class_name);
        setMapValue(node, mappedValue(genericLabelFieldNames(), GenericLabelField::LabelClassGroup), class_group);
        setMapValue(node, mappedValue(genericLabelFieldNames(), GenericLabelField::ClassIndex),
                    classIndex(label_class_id, ctx.class_indices));
        setMapValue(node, mappedValue(genericLabelFieldNames(), GenericLabelField::Data), variantToYaml(label_data));
        return node;
    }

private:
    bool readSelectedImage(const SplitExportContext &ctx, qint64 image_id, ImageExportContext &image) const
    {
        image.image_id   = image_id;
        image.dataset_id = ctx.source->imageDatasetId(image_id);
        if (!ctx.selection->containsDataset(image.dataset_id))
            return false;

        image.image_path = ctx.source->imagePath(image_id);
        if (image.image_path.trimmed().isEmpty())
            return false;

        if (!dltool::data::DatasetIO::getImageDimensions(image.image_path, image.width, image.height))
            return false;

        const QVariantMap image_level_label = ctx.source->imageLevelLabelData(image_id);
        image.image_label_class_id
            = image_level_label.value(mappedValue(imageLevelLabelFieldNames(), ImageLevelLabelField::LabelClassId), -1)
                  .toLongLong();
        image.image_label_class_name
            = image_level_label.value(mappedValue(imageLevelLabelFieldNames(), ImageLevelLabelField::LabelClassName))
                  .toString();
        image.image_label_group
            = image_level_label.value(mappedValue(imageLevelLabelFieldNames(), ImageLevelLabelField::Group)).toString();
        image.has_anomaly_label = isAnomalyGroup(image.image_label_group);
        return true;
    }

    bool appendSelectedLabels(SplitExportContext &ctx, ImageExportContext &image, YAML::Node &labels,
                              QString *err_msg) const
    {
        for (const int64_t label_id : ctx.source->imageLabelIds(image.image_id))
        {
            const qint64 label_class_id = ctx.source->labelClassId(label_id);
            if (!ctx.selection->contains(image.dataset_id, label_class_id))
                continue;

            const QString     class_name  = ctx.source->labelClassName(label_class_id);
            const QString     class_group = ctx.source->labelClassGroup(label_class_id);
            const QVariantMap label_data  = ctx.source->labelData(label_id);
            YAML::Node        label = baseLabelNode(ctx, label_id, label_class_id, class_name, class_group, label_data);

            const LabelExportDecision decision
                = augmentLabel(ctx, image, label_id, label_class_id, class_group, label_data, label, err_msg);
            if (decision == LabelExportDecision::Error)
                return false;
            if (decision == LabelExportDecision::Skip)
                continue;

            image.has_anomaly_label = image.has_anomaly_label || isAnomalyGroup(class_group);
            const int label_class_index = classIndex(label_class_id, ctx.class_indices);
            if (label_class_index >= 0)
                ctx.class_names.insert(label_class_index, class_name);
            labels.push_back(label);
            ++ctx.label_count;
        }
        return true;
    }

    std::map<qint64, int> class_indices_;
    QMap<int, QString>    class_names_;
};

class GenericDatasetOrganizer : public DatasetOrganizerBase
{
protected:
    bool appendImage(SplitExportContext &ctx, const ImageExportContext &image, const YAML::Node &labels,
                     QString *err_msg) const override
    {
        Q_UNUSED(err_msg)

        YAML::Node image_node(YAML::NodeType::Map);
        setMapValue(image_node, mappedValue(genericImageFieldNames(), GenericImageField::Id), image.image_id);
        setMapValue(image_node, mappedValue(genericImageFieldNames(), GenericImageField::Path),
                    cleanPath(image.image_path));
        setMapValue(image_node, mappedValue(genericImageFieldNames(), GenericImageField::DatasetId), image.dataset_id);
        setMapValue(image_node, mappedValue(genericImageFieldNames(), GenericImageField::DatasetName),
                    ctx.source->datasetName(image.dataset_id));
        setMapValue(image_node, mappedValue(genericImageFieldNames(), GenericImageField::Width), image.width);
        setMapValue(image_node, mappedValue(genericImageFieldNames(), GenericImageField::Height), image.height);
        setMapValue(image_node, mappedValue(genericImageFieldNames(), GenericImageField::ImageLabelClassId),
                    image.image_label_class_id);
        setMapValue(image_node, mappedValue(genericImageFieldNames(), GenericImageField::ImageLabelClassName),
                    image.image_label_class_name);
        setMapValue(image_node, mappedValue(genericImageFieldNames(), GenericImageField::ImageLabelGroup),
                    image.image_label_group);
        setMapValue(image_node, mappedValue(genericImageFieldNames(), GenericImageField::Anomaly),
                    image.has_anomaly_label);
        setMapValue(image_node, mappedValue(genericImageFieldNames(), GenericImageField::LabelIndex),
                    image.has_anomaly_label ? 1 : 0);
        setMapValue(image_node, mappedValue(genericImageFieldNames(), GenericImageField::Labels), labels);
        ctx.images.push_back(image_node);
        return true;
    }

    QVariantMap finishSplit(SplitExportContext &ctx, QString *err_msg) const override
    {
        const QString file_list_path = splitFileListPath(ctx, err_msg);
        if (file_list_path.isEmpty())
            return {};
        if (!writeImageFileList(file_list_path, ctx.image_files, err_msg))
            return {};
        if (ctx.split != DatasetSplit::Test)
        {
            const QString  labels_path = splitLabelsPath(ctx.request->dataset_dir, ctx.split_name);
            const QVariant labels      = common::yaml::nodeVariant(ctx.images);
            if (!writeJsonFile(labels_path, labels, err_msg, QString("写入数据集标注文件失败")))
                return {};
        }

        return {
            {mappedValue(datasetConfigFieldNames(),   DatasetConfigField::FileList),  file_list_path},
            {mappedValue(datasetConfigFieldNames(), DatasetConfigField::ImageCount), ctx.image_count},
            {mappedValue(datasetConfigFieldNames(), DatasetConfigField::LabelCount), ctx.label_count},
        };
    }
};

class UltralyticsDatasetOrganizer final : public GenericDatasetOrganizer
{
protected:
    QString imageListPath(const SplitExportContext &ctx, const ImageExportContext &image) const override
    {
        const QString suffix = QFileInfo(image.image_path).suffix().trimmed();
        return cleanPath(QDir(ctx.request->dataset_dir).filePath(
            QString::number(image.image_id) + (suffix.isEmpty() ? QStringLiteral(".jpg") : QStringLiteral(".") + suffix)));
    }

    bool appendImage(SplitExportContext &ctx, const ImageExportContext &image, const YAML::Node &labels,
                     QString *err_msg) const override
    {
        if (!GenericDatasetOrganizer::appendImage(ctx, image, labels, err_msg))
            return false;

        const QString target_image = imageListPath(ctx, image);
        if (!QFileInfo::exists(target_image))
        {
            if (!QFile::copy(image.image_path, target_image))
            {
                if (err_msg != nullptr)
                    *err_msg = QString("复制 Ultralytics 图像失败: %1 -> %2").arg(image.image_path, target_image);
                return false;
            }
        }

        const QString label_path = cleanPath(QDir(ctx.request->dataset_dir).filePath(
            QString::number(image.image_id) + QStringLiteral(".txt")));
        QSaveFile label_file(label_path);
        if (!label_file.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            if (err_msg != nullptr)
                *err_msg = QString("打开 Ultralytics 标签文件失败: %1").arg(label_file.errorString());
            return false;
        }
        QTextStream stream(&label_file);
        stream.setEncoding(QStringConverter::Utf8);
        for (const YAML::Node &entry : labels)
        {
            const int class_index = entry["class_index"] ? entry["class_index"].as<int>() : -1;
            if (class_index < 0)
                continue;

            const QVariantMap data = common::yaml::nodeVariant(entry["data"]).toMap();
            QStringList      values;
            values << QString::number(image.image_id) << QString::number(class_index);
            if (ctx.request->method == dltool::core::DeepLearningMethod::Segmentation)
            {
                const std::vector<QPointF> points = labelPolygon(data);
                for (const QPointF &point : points)
                {
                    if (image.width <= 0 || image.height <= 0)
                        continue;
                    values << numberText(std::clamp(point.x() / image.width, 0.0, 1.0));
                    values << numberText(std::clamp(point.y() / image.height, 0.0, 1.0));
                }
            }
            else
            {
                const QVariantMap yolo = entry["yolo"].IsMap()
                                            ? common::yaml::nodeVariant(entry["yolo"]).toMap()
                                            : labelYoloData(data, image.width, image.height);
                const QStringList keys = {QStringLiteral("cx"), QStringLiteral("cy"), QStringLiteral("width"),
                                          QStringLiteral("height")};
                for (const QString &key : keys)
                    values << numberText(yolo.value(key).toDouble());
            }
            if (values.size() >= (ctx.request->method == dltool::core::DeepLearningMethod::Segmentation ? 8 : 6))
                stream << values.join(QChar(' ')) << QChar('\n');
        }
        if (!label_file.commit())
        {
            if (err_msg != nullptr)
                *err_msg = QString("提交 Ultralytics 标签文件失败: %1").arg(label_file.errorString());
            return false;
        }
        return true;
    }

    QVariantMap finishSplit(SplitExportContext &ctx, QString *err_msg) const override
    {
        const QString file_list_path = splitFileListPath(ctx, err_msg);
        if (file_list_path.isEmpty() || !writeImageFileList(file_list_path, ctx.image_files, err_msg))
            return {};

        const QString config_path = cleanPath(QDir(ctx.request->dataset_dir).filePath(QStringLiteral("dataset.yaml")));
        QMap<int, qint64> class_ids;
        for (auto it = ctx.class_indices.cbegin(); it != ctx.class_indices.cend(); ++it)
            class_ids.insert(it->second, it->first);
        if (!writeUltralyticsDatasetConfig(config_path, ctx.request->dataset_dir, ctx.class_names, class_ids, err_msg))
            return {};

        return {
            {mappedValue(datasetConfigFieldNames(), DatasetConfigField::FileList), file_list_path},
            {mappedValue(datasetConfigFieldNames(), DatasetConfigField::ImageCount), ctx.image_count},
            {mappedValue(datasetConfigFieldNames(), DatasetConfigField::LabelCount), ctx.label_count},
            {QStringLiteral("dataset_config"), config_path},
        };
    }

    LabelExportDecision augmentLabel(SplitExportContext &ctx, ImageExportContext &image, qint64 label_id,
                                     qint64 label_class_id, const QString &class_group, const QVariantMap &label_data,
                                     YAML::Node &label, QString *err_msg) const override
    {
        Q_UNUSED(ctx)
        Q_UNUSED(label_id)
        Q_UNUSED(label_class_id)
        Q_UNUSED(class_group)
        Q_UNUSED(err_msg)

        const QVariantMap yolo = labelYoloData(label_data, image.width, image.height);
        if (!yolo.isEmpty())
            setMapValue(label, mappedValue(yoloLabelFieldNames(), YoloLabelField::Yolo), variantToYaml(yolo));
        return LabelExportDecision::Keep;
    }
};

class FsSam2DatasetOrganizer final : public GenericDatasetOrganizer
{
protected:
    QString splitDirectory(const ModelDatasetExportRequest &request, const QString &split_name) const override
    {
        Q_UNUSED(split_name)
        return request.dataset_dir;
    }

    bool prepareSplit(SplitExportContext &ctx, QString *err_msg) const override
    {
        if (!GenericDatasetOrganizer::prepareSplit(ctx, err_msg))
            return false;
        // 掩膜直接写入共享数据集目录（不再使用 masks 子目录）。
        ctx.masks_dir = ctx.request->dataset_dir;
        return ensureDirectory(ctx.masks_dir, err_msg, QString("数据集目录为空"), QString("创建数据集目录失败: %1"));
    }

    LabelExportDecision augmentLabel(SplitExportContext &ctx, ImageExportContext &image, qint64 label_id,
                                     qint64 label_class_id, const QString &class_group, const QVariantMap &label_data,
                                     YAML::Node &label, QString *err_msg) const override
    {
        Q_UNUSED(label_class_id)
        Q_UNUSED(class_group)

        if (ctx.split == DatasetSplit::Test)
            return LabelExportDecision::Keep;

        if (ctx.request != nullptr && ctx.request->method == dltool::core::DeepLearningMethod::Detection)
        {
            if (!hasFsSam2BoxPrompt(label_data))
                return LabelExportDecision::Skip;

            const QString mask_path = cleanPath(fsSam2MaskPath(ctx.masks_dir, image.image_id, label_id));
            setMapValue(label, mappedValue(fsSam2LabelFieldNames(), FsSam2LabelField::MaskPath), mask_path);
            return LabelExportDecision::Keep;
        }

        QString       mask_err;
        const QString mask_path = writeFsSam2LabelMask(label_data, image.width, image.height, ctx.masks_dir,
                                                       image.image_id, label_id, &mask_err);
        if (!mask_err.isEmpty())
        {
            if (err_msg != nullptr)
                *err_msg = mask_err;
            return LabelExportDecision::Error;
        }
        if (mask_path.isEmpty())
            return LabelExportDecision::Skip;

        setMapValue(label, mappedValue(fsSam2LabelFieldNames(), FsSam2LabelField::MaskPath), mask_path);
        return LabelExportDecision::Keep;
    }

    QVariantMap finishSplit(SplitExportContext &ctx, QString *err_msg) const override
    {
        const QString file_list_path = splitFileListPath(ctx, err_msg);
        if (file_list_path.isEmpty())
            return {};
        QSaveFile file(file_list_path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            if (err_msg != nullptr)
                *err_msg = QString("打开 FS-SAM2 文件列表失败: %1").arg(file.errorString());
            return {};
        }

        QTextStream stream(&file);
        stream.setEncoding(QStringConverter::Utf8);
        stream << QString("image_id,image_path\n");
        for (const auto &[image_id, image_path] : ctx.image_files)
        {
            QString escaped_path = image_path;
            escaped_path.replace(QString("\""), QString("\"\""));
            if (escaped_path.contains(QChar(',')) || escaped_path.contains(QChar('"'))
                || escaped_path.contains(QChar('\n')) || escaped_path.contains(QChar('\r')))
                escaped_path = QString("\"") + escaped_path + QString("\"");
            stream << image_id << QChar(',') << escaped_path << QChar('\n');
        }
        if (!file.commit())
        {
            if (err_msg != nullptr)
                *err_msg = QString("提交 FS-SAM2 文件列表失败: %1").arg(file.errorString());
            return {};
        }

        if (ctx.split != DatasetSplit::Test)
        {
            const QString label_file_path
                = QDir(ctx.request->dataset_dir).filePath(QString("%1_labels.json").arg(ctx.split_name));
            QSaveFile label_file(label_file_path);
            if (!label_file.open(QIODevice::WriteOnly | QIODevice::Text))
            {
                if (err_msg != nullptr)
                    *err_msg = QString("打开 FS-SAM2 标注文件失败: %1").arg(label_file.errorString());
                return {};
            }
            const QByteArray labels_json
                = QJsonDocument::fromVariant(common::yaml::nodeVariant(ctx.images)).toJson(QJsonDocument::Compact);
            if (label_file.write(labels_json) != labels_json.size() || !label_file.commit())
            {
                if (err_msg != nullptr)
                    *err_msg = QString("写入 FS-SAM2 标注文件失败: %1").arg(label_file.errorString());
                return {};
            }
        }

        return {
            {mappedValue(datasetConfigFieldNames(),   DatasetConfigField::FileList), cleanPath(file_list_path)},
            {mappedValue(datasetConfigFieldNames(),   DatasetConfigField::MasksDir),  cleanPath(ctx.masks_dir)},
            {mappedValue(datasetConfigFieldNames(), DatasetConfigField::ImageCount),           ctx.image_count},
            {mappedValue(datasetConfigFieldNames(), DatasetConfigField::LabelCount),           ctx.label_count}
        };
    }
};

class AnomalibDatasetOrganizer final : public DatasetOrganizerBase
{
protected:
    QString splitDirectory(const ModelDatasetExportRequest &request, const QString &split_name) const override
    {
        Q_UNUSED(split_name)
        return request.dataset_dir;
    }

    bool prepareSplit(SplitExportContext &ctx, QString *err_msg) const override
    {
        if (!DatasetOrganizerBase::prepareSplit(ctx, err_msg))
            return false;
        // 掩膜直接写入共享数据集目录（不再使用 masks 子目录）。
        ctx.masks_dir = ctx.request->dataset_dir;
        return ensureDirectory(ctx.masks_dir, err_msg, QString("数据集目录为空"),
                               QString("创建数据集目录失败: %1"));
    }

    LabelExportDecision augmentLabel(SplitExportContext &ctx, ImageExportContext &image, qint64 label_id,
                                     qint64 label_class_id, const QString &class_group, const QVariantMap &label_data,
                                     YAML::Node &label, QString *err_msg) const override
    {
        Q_UNUSED(ctx)
        Q_UNUSED(label_id)
        Q_UNUSED(label_class_id)
        Q_UNUSED(label)
        Q_UNUSED(err_msg)

        if (isAnomalyGroup(class_group))
        {
            std::vector<QPointF> polygon = labelPolygon(label_data);
            if (polygon.size() >= 3)
                image.anomaly_polygons.push_back(std::move(polygon));
        }
        return LabelExportDecision::Keep;
    }

    bool appendImage(SplitExportContext &ctx, const ImageExportContext &image, const YAML::Node &labels,
                     QString *err_msg) const override
    {
        Q_UNUSED(labels)

        // 掩膜按 image_id 写入共享 datasets；Python 端以同名 mask 是否存在判断是否异常。
        if (image.has_anomaly_label && ctx.split != DatasetSplit::Test)
        {
            QString mask_err;
            writeAnomalibImageMask(image.anomaly_polygons, image.width, image.height, ctx.masks_dir, image.image_id,
                                   &mask_err);
            if (!mask_err.isEmpty())
            {
                if (err_msg != nullptr)
                    *err_msg = mask_err;
                return false;
            }
        }
        return true;
    }

    QVariantMap finishSplit(SplitExportContext &ctx, QString *err_msg) const override
    {
        QString file_list_path;
        if (ctx.split == DatasetSplit::Test)
        {
            file_list_path = splitFileListPath(ctx, err_msg);
            if (file_list_path.isEmpty())
                return {};
        }
        else
        {
            const QString train_dir = cleanPath(ctx.request->train_dir);
            if (train_dir.isEmpty())
            {
                if (err_msg != nullptr)
                    *err_msg = QString("训练目录为空");
                return {};
            }
            if (!ensureDirectory(train_dir, err_msg, QString("训练目录为空"), QString("创建训练目录失败: %1")))
                return {};
            file_list_path = anomalibFileListPath(train_dir, ctx.split_name);
        }
        if (!writeImageFileList(file_list_path, ctx.image_files, err_msg))
            return {};

        return {
            {mappedValue(datasetConfigFieldNames(),   DatasetConfigField::FileList),           file_list_path},
            {mappedValue(datasetConfigFieldNames(),   DatasetConfigField::MasksDir), cleanPath(ctx.masks_dir)},
            {mappedValue(datasetConfigFieldNames(), DatasetConfigField::ImageCount),          ctx.image_count},
            {mappedValue(datasetConfigFieldNames(), DatasetConfigField::LabelCount),          ctx.label_count},
        };
    }
};

class Dinomaly2DatasetOrganizer final : public DatasetOrganizerBase
{
protected:
    QString splitDirectory(const ModelDatasetExportRequest &request, const QString &split_name) const override
    {
        Q_UNUSED(split_name)
        return request.dataset_dir;
    }

    bool prepareSplit(SplitExportContext &ctx, QString *err_msg) const override
    {
        if (!DatasetOrganizerBase::prepareSplit(ctx, err_msg))
            return false;
        // 掩膜直接写入共享数据集目录，文件命名与 anomalib 布局一致（{image_id}.png），
        // 但像素值不再只是 0/255：每个类别区域用其类别 ID 填充。
        ctx.masks_dir = ctx.request->dataset_dir;
        return ensureDirectory(ctx.masks_dir, err_msg, QString("数据集目录为空"),
                               QString("创建数据集目录失败: %1"));
    }

    LabelExportDecision augmentLabel(SplitExportContext &ctx, ImageExportContext &image, qint64 label_id,
                                     qint64 label_class_id, const QString &class_group, const QVariantMap &label_data,
                                     YAML::Node &label, QString *err_msg) const override
    {
        Q_UNUSED(label_id)
        Q_UNUSED(label)
        Q_UNUSED(err_msg)

        if (label_class_id <= 0)
            return LabelExportDecision::Skip;

        // 按类别分组收集类别值：good 组 -> good_values，anomaly 组 -> anomaly_values，
        // unlabeled 组 -> ignore_values（默认组为 anomaly）。
        const QString normalized_group = class_group.trimmed().toLower();
        if (normalized_group == mappedValue(labelGroupNames(), LabelGroupName::Good))
            ctx.good_classes.insert(label_class_id);
        else if (normalized_group == mappedValue(labelGroupNames(), LabelGroupName::Unlabeled))
            ctx.ignore_classes.insert(label_class_id);
        else
            ctx.anomaly_classes.insert(label_class_id);

        std::vector<QPointF> polygon = labelPolygon(label_data);
        if (polygon.size() >= 3)
            image.class_polygons[label_class_id].push_back(std::move(polygon));
        return LabelExportDecision::Keep;
    }

    bool appendImage(SplitExportContext &ctx, const ImageExportContext &image, const YAML::Node &labels,
                     QString *err_msg) const override
    {
        Q_UNUSED(labels)

        // 掩膜按 image_id 写入共享 datasets；测试划分不导出掩膜，测试 GT 由评估端
        // 从项目数据库重建。
        if (!image.class_polygons.empty() && ctx.split != DatasetSplit::Test)
        {
            QString mask_err;
            writeDinomaly2ImageMask(image.class_polygons, image.width, image.height, ctx.masks_dir, image.image_id,
                                    &mask_err);
            if (!mask_err.isEmpty())
            {
                if (err_msg != nullptr)
                    *err_msg = mask_err;
                return false;
            }
        }
        return true;
    }

    QVariantMap finishSplit(SplitExportContext &ctx, QString *err_msg) const override
    {
        QString file_list_path;
        if (ctx.split == DatasetSplit::Test)
        {
            file_list_path = splitFileListPath(ctx, err_msg);
            if (file_list_path.isEmpty())
                return {};
        }
        else
        {
            const QString train_dir = cleanPath(ctx.request->train_dir);
            if (train_dir.isEmpty())
            {
                if (err_msg != nullptr)
                    *err_msg = QString("训练目录为空");
                return {};
            }
            if (!ensureDirectory(train_dir, err_msg, QString("训练目录为空"), QString("创建训练目录失败: %1")))
                return {};
            file_list_path = anomalibFileListPath(train_dir, ctx.split_name);
        }
        if (!writeImageFileList(file_list_path, ctx.image_files, err_msg))
            return {};

        return {
            {mappedValue(datasetConfigFieldNames(),   DatasetConfigField::FileList),           file_list_path},
            {mappedValue(datasetConfigFieldNames(),   DatasetConfigField::MasksDir), cleanPath(ctx.masks_dir)},
            {mappedValue(datasetConfigFieldNames(), DatasetConfigField::ImageCount),          ctx.image_count},
            {mappedValue(datasetConfigFieldNames(), DatasetConfigField::LabelCount),          ctx.label_count},
            {mappedValue(datasetConfigFieldNames(),   DatasetConfigField::GoodValues),
             maskClassValueList(ctx, DatasetConfigField::GoodValues)},
            {mappedValue(datasetConfigFieldNames(), DatasetConfigField::AnomalyValues),
             maskClassValueList(ctx, DatasetConfigField::AnomalyValues)},
            {mappedValue(datasetConfigFieldNames(),   DatasetConfigField::IgnoreValues),
             maskClassValueList(ctx, DatasetConfigField::IgnoreValues)},
        };
    }
};

std::unique_ptr<DatasetOrganizerBase> createDatasetOrganizer(const QString &framework_name)
{
    switch (datasetLayout(framework_name))
    {
    case FrameworkDatasetLayout::Anomalib:
        return std::make_unique<AnomalibDatasetOrganizer>();
    case FrameworkDatasetLayout::Dinomaly2:
        return std::make_unique<Dinomaly2DatasetOrganizer>();
    case FrameworkDatasetLayout::Ultralytics:
        return std::make_unique<UltralyticsDatasetOrganizer>();
    case FrameworkDatasetLayout::FsSam2:
        return std::make_unique<FsSam2DatasetOrganizer>();
    case FrameworkDatasetLayout::Generic:
    default:
        return std::make_unique<GenericDatasetOrganizer>();
    }
}

} // namespace

QVariantMap ModelDatasetOrganizer::organize(const ModelDatasetExportRequest &request, QString *err_msg)
{
    QVariantMap datasets;
    if (request.dataset_dir.trimmed().isEmpty())
    {
        if (err_msg != nullptr)
            *err_msg = QString("模型数据集目录为空");
        return {};
    }
    if (!ensureDirectory(request.dataset_dir, err_msg, QString("数据集目录为空"), QString("创建数据集目录失败: %1")))
        return {};

    // 小样本流程（框架能力位 few_shot）：测试时仍需导出训练集划分，供
    // 小样本推理脚本读取参考样本；不再按框架名字符串判断数据集布局。
    const bool fs_sam2_layout  = request.few_shot;
    const FrameworkDatasetLayout layout = datasetLayout(request.framework_name);
    // anomalib 与 dinomaly2 布局共用 {image_id}.png 掩膜与 train/validation.txt 文件列表。
    const bool file_list_layout = layout == FrameworkDatasetLayout::Anomalib
                                  || layout == FrameworkDatasetLayout::Dinomaly2;
    const std::unique_ptr<DatasetOrganizerBase> organizer = createDatasetOrganizer(request.framework_name);
    if (layout == FrameworkDatasetLayout::Ultralytics && request.source != nullptr)
    {
        // Ultralytics uses contiguous class indices; keep a deterministic mapping
        // back to the project's persistent label-class IDs for C++ evaluation.
        std::set<qint64> source_class_ids;
        for (const int64_t image_id : request.source->allImageIds())
            for (const int64_t label_id : request.source->imageLabelIds(image_id))
            {
                const qint64 class_id = request.source->labelClassId(label_id);
                if (class_id >= 0)
                    source_class_ids.insert(class_id);
            }

        std::map<qint64, int> class_indices;
        QMap<int, QString>    class_names;
        int                   class_index = 0;
        for (const qint64 class_id : source_class_ids)
        {
            class_indices.emplace(class_id, class_index);
            QString class_name = request.source->labelClassName(class_id).trimmed();
            if (class_name.isEmpty())
                class_name = QString::number(class_id);
            class_names.insert(class_index, class_name);
            ++class_index;
        }
        organizer->setClassMapping(std::move(class_indices), std::move(class_names));
    }
    if (isTrainModelTask(request.task_type) || request.task_type == ModelTaskType::BoxToMask)
    {
        const QVariantMap train = organizer->exportSplit(request, DatasetSplit::Train, true, err_msg);
        if (train.isEmpty())
            return {};
        datasets.insert(mappedValue(datasetConfigFieldNames(), DatasetConfigField::Train), train);

        QString     validation_err;
        QVariantMap validation = organizer->exportSplit(request, DatasetSplit::Validation, false, &validation_err);
        if (!validation.isEmpty())
            datasets.insert(mappedValue(datasetConfigFieldNames(), DatasetConfigField::Validation), validation);
        else if (!validation_err.isEmpty())
            spdlog::debug("跳过验证数据集导出: {}", validation_err.toUtf8().constData());
    }
    else if (isTestModelTask(request.task_type))
    {
        if (fs_sam2_layout)
        {
            const QVariantMap train = organizer->exportSplit(request, DatasetSplit::Train, true, err_msg);
            if (train.isEmpty())
                return {};
            datasets.insert(mappedValue(datasetConfigFieldNames(), DatasetConfigField::Train), train);
        }

        const QVariantMap test = organizer->exportSplit(request, DatasetSplit::Test, true, err_msg);
        if (test.isEmpty())
            return {};
        datasets.insert(mappedValue(datasetConfigFieldNames(), DatasetConfigField::Test), test);
    }

    if (file_list_layout)
    {
        removeLegacyAnomalibSplitFiles(request.dataset_dir, request.train_dir);
        if (isTrainModelTask(request.task_type) && !request.train_dir.trimmed().isEmpty()
            && !datasets.contains(mappedValue(datasetConfigFieldNames(), DatasetConfigField::Validation)))
        {
            const QString train_root = cleanPath(QFileInfo(request.train_dir).absoluteFilePath());
            const QString stale_path = cleanPath(anomalibFileListPath(request.train_dir, QStringLiteral("validation")));
            if (!train_root.isEmpty() && stale_path.startsWith(train_root + QStringLiteral("/"), Qt::CaseInsensitive)
                && QFileInfo::exists(stale_path))
            {
                QFile::remove(stale_path);
            }
        }
    }

    return datasets;
}

} // namespace dltool::model
