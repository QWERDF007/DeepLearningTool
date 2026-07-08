#include "model/ModelDatasetOrganizer.h"

#include "common/MaskPolygonUtils.h"
#include "common/Utils.h"
#include "common/YamlUtils.h"
#include "data/DatasetIO.h"
#include "model/ModelTaskTypes.h"

#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

#include <QDir>
#include <QImage>
#include <QPointF>
#include <algorithm>
#include <map>
#include <vector>

namespace dltool::model {

namespace {

enum class FrameworkDatasetLayout
{
    Generic,
    Anomalib,
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
    Manifest,
    FileList,
    MasksDir,
    ImageCount,
    LabelCount,
};

enum class DatasetSubdir
{
    Masks,
};

enum class DatasetFileName
{
    Manifest,
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

enum class ManifestField
{
    Version,
    Method,
    Framework,
    ModelArchitecture,
    ModelUuid,
    Split,
    Images,
    Samples,
    Id,
    Path,
    Mask,
    MasksDir,
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
    LabelId,
    LabelClassId,
    LabelClassName,
    LabelClassGroup,
    ClassIndex,
    Data,
    MaskPath,
    Yolo,
    X,
    Y,
    Cx,
    Cy,
    Group,
};

enum class LabelGroupName
{
    Anomaly,
};

const std::map<QString, FrameworkDatasetLayout> &frameworkDatasetLayouts()
{
    static const std::map<QString, FrameworkDatasetLayout> layouts = {
        {   QStringLiteral("anomalib"),    FrameworkDatasetLayout::Anomalib},
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
        {  DatasetConfigField::Manifest,    QStringLiteral("manifest")},
        {  DatasetConfigField::FileList,   QStringLiteral("file_list")},
        {  DatasetConfigField::MasksDir,   QStringLiteral("masks_dir")},
        {DatasetConfigField::ImageCount, QStringLiteral("image_count")},
        {DatasetConfigField::LabelCount, QStringLiteral("label_count")},
    };
    return names;
}

const std::map<DatasetSubdir, QString> &datasetSubdirNames()
{
    static const std::map<DatasetSubdir, QString> names = {
        {DatasetSubdir::Masks, QStringLiteral("masks")},
    };
    return names;
}

const std::map<DatasetFileName, QString> &datasetFileNames()
{
    static const std::map<DatasetFileName, QString> names = {
        {     DatasetFileName::Manifest,         QStringLiteral("manifest.yaml")},
        {         DatasetFileName::Mask, QStringLiteral("image_%1_label_%2.png")},
        {    DatasetFileName::ImageMask,                QStringLiteral("%1.png")},
        {DatasetFileName::SplitFileList,               QStringLiteral("%1.yaml")},
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

const std::map<ManifestField, QString> &manifestFieldNames()
{
    static const std::map<ManifestField, QString> names = {
        {            ManifestField::Version,                QStringLiteral("version")},
        {             ManifestField::Method,                 QStringLiteral("method")},
        {          ManifestField::Framework,              QStringLiteral("framework")},
        {  ManifestField::ModelArchitecture,     QStringLiteral("model_architecture")},
        {          ManifestField::ModelUuid,             QStringLiteral("model_uuid")},
        {              ManifestField::Split,                  QStringLiteral("split")},
        {             ManifestField::Images,                 QStringLiteral("images")},
        {            ManifestField::Samples,                QStringLiteral("samples")},
        {                 ManifestField::Id,                     QStringLiteral("id")},
        {               ManifestField::Path,                   QStringLiteral("path")},
        {               ManifestField::Mask,                   QStringLiteral("mask")},
        {           ManifestField::MasksDir,              QStringLiteral("masks_dir")},
        {          ManifestField::DatasetId,             QStringLiteral("dataset_id")},
        {        ManifestField::DatasetName,           QStringLiteral("dataset_name")},
        {              ManifestField::Width,                  QStringLiteral("width")},
        {             ManifestField::Height,                 QStringLiteral("height")},
        {  ManifestField::ImageLabelClassId,   QStringLiteral("image_label_class_id")},
        {ManifestField::ImageLabelClassName, QStringLiteral("image_label_class_name")},
        {    ManifestField::ImageLabelGroup,      QStringLiteral("image_label_group")},
        {         ManifestField::LabelIndex,            QStringLiteral("label_index")},
        {            ManifestField::Anomaly,                QStringLiteral("anomaly")},
        {             ManifestField::Labels,                 QStringLiteral("labels")},
        {            ManifestField::LabelId,               QStringLiteral("label_id")},
        {       ManifestField::LabelClassId,         QStringLiteral("label_class_id")},
        {     ManifestField::LabelClassName,       QStringLiteral("label_class_name")},
        {    ManifestField::LabelClassGroup,      QStringLiteral("label_class_group")},
        {         ManifestField::ClassIndex,            QStringLiteral("class_index")},
        {               ManifestField::Data,                   QStringLiteral("data")},
        {           ManifestField::MaskPath,              QStringLiteral("mask_path")},
        {               ManifestField::Yolo,                   QStringLiteral("yolo")},
        {                  ManifestField::X,                      QStringLiteral("x")},
        {                  ManifestField::Y,                      QStringLiteral("y")},
        {                 ManifestField::Cx,                     QStringLiteral("cx")},
        {                 ManifestField::Cy,                     QStringLiteral("cy")},
        {              ManifestField::Group,                  QStringLiteral("group")},
    };
    return names;
}

const std::map<LabelGroupName, QString> &labelGroupNames()
{
    static const std::map<LabelGroupName, QString> names = {
        {LabelGroupName::Anomaly, QStringLiteral("anomaly")},
    };
    return names;
}

QString datasetSplitName(DatasetSplit split)
{
    const auto &names = datasetSplitNames();
    const auto  found = names.find(split);
    return found != names.end() ? found->second : QString();
}

QString datasetConfigFieldName(DatasetConfigField field)
{
    const auto &names = datasetConfigFieldNames();
    const auto  found = names.find(field);
    return found != names.end() ? found->second : QString();
}

QString datasetSubdirName(DatasetSubdir subdir)
{
    const auto &names = datasetSubdirNames();
    const auto  found = names.find(subdir);
    return found != names.end() ? found->second : QString();
}

QString datasetFileName(DatasetFileName name)
{
    const auto &names = datasetFileNames();
    const auto  found = names.find(name);
    return found != names.end() ? found->second : QString();
}

QString labelDataFieldName(LabelDataField field)
{
    const auto &names = labelDataFieldNames();
    const auto  found = names.find(field);
    return found != names.end() ? found->second : QString();
}

QString manifestFieldName(ManifestField field)
{
    const auto &names = manifestFieldNames();
    const auto  found = names.find(field);
    return found != names.end() ? found->second : QString();
}

QString labelGroupName(LabelGroupName group)
{
    const auto &names = labelGroupNames();
    const auto  found = names.find(group);
    return found != names.end() ? found->second : QString();
}

FrameworkDatasetLayout datasetLayout(const QString &framework_name)
{
    const QString key     = framework_name.trimmed().toLower();
    const auto   &layouts = frameworkDatasetLayouts();
    const auto    found   = layouts.find(key);
    return found != layouts.end() ? found->second : FrameworkDatasetLayout::Generic;
}

bool ensureDirectory(const QString &path, QString *err_msg)
{
    const QString cleaned = dltool::common::cleanPath(path);
    if (cleaned.isEmpty())
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("数据集目录为空");
        return false;
    }

    QDir dir(cleaned);
    if (dir.exists())
        return true;
    if (!dir.mkpath(QStringLiteral(".")))
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("创建数据集目录失败: %1").arg(cleaned);
        return false;
    }
    return true;
}

void setString(YAML::Node &node, ManifestField field, const QString &value)
{
    dltool::common::yaml::setMapValue(node, manifestFieldName(field), value);
}

void setInteger(YAML::Node &node, ManifestField field, qint64 value)
{
    dltool::common::yaml::setMapValue(node, manifestFieldName(field), value);
}

void setBool(YAML::Node &node, ManifestField field, bool value)
{
    dltool::common::yaml::setMapValue(node, manifestFieldName(field), value);
}

bool isAnomalyGroup(const QString &group)
{
    return group.compare(labelGroupName(LabelGroupName::Anomaly), Qt::CaseInsensitive) == 0;
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

    const double bbox_x = label_data.value(labelDataFieldName(LabelDataField::X)).toDouble();
    const double bbox_y = label_data.value(labelDataFieldName(LabelDataField::Y)).toDouble();
    const double width  = label_data.value(labelDataFieldName(LabelDataField::Width)).toDouble();
    const double height = label_data.value(labelDataFieldName(LabelDataField::Height)).toDouble();
    if (width <= 0 || height <= 0)
        return {};

    return {
        {    manifestFieldName(ManifestField::Cx),   (bbox_x + width / 2.0) / image_width},
        {    manifestFieldName(ManifestField::Cy), (bbox_y + height / 2.0) / image_height},
        { manifestFieldName(ManifestField::Width),                    width / image_width},
        {manifestFieldName(ManifestField::Height),                  height / image_height},
    };
}

std::vector<QPointF> labelPolygon(const QVariantMap &label_data)
{
    std::vector<QPointF> points
        = dltool::data::DatasetIO::variantListToPoints(label_data.value(labelDataFieldName(LabelDataField::Points)));
    if (points.size() >= 3)
        return points;

    const double x = label_data.value(labelDataFieldName(LabelDataField::X)).toDouble();
    const double y = label_data.value(labelDataFieldName(LabelDataField::Y)).toDouble();
    const double w = label_data.value(labelDataFieldName(LabelDataField::Width)).toDouble();
    const double h = label_data.value(labelDataFieldName(LabelDataField::Height)).toDouble();
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
    return QDir(masks_dir).filePath(datasetFileName(DatasetFileName::Mask).arg(image_id).arg(label_id));
}

QString anomalibMaskFileName(qint64 image_id)
{
    return datasetFileName(DatasetFileName::ImageMask).arg(image_id);
}

QString anomalibFileListPath(const QString &dataset_dir, const QString &split_name)
{
    return QDir(dataset_dir).filePath(datasetFileName(DatasetFileName::SplitFileList).arg(split_name));
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
            *err_msg = QStringLiteral("写入 FS-SAM2 mask 失败: %1").arg(path);
        return {};
    }
    return dltool::common::cleanPath(path);
}

QString writeAnomalibImageMask(const std::vector<std::vector<QPointF>> &polygons, int image_width, int image_height,
                               const QString &masks_dir, qint64 image_id, QString *err_msg)
{
    if (image_width <= 0 || image_height <= 0)
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("写入 anomalib mask 失败: 图像尺寸无效, image_id=%1").arg(image_id);
        return {};
    }

    const std::vector<uint8_t> mask = dltool::common::polygons2Mask(polygons, image_width, image_height);
    if (mask.empty())
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("写入 anomalib mask 失败: mask 为空, image_id=%1").arg(image_id);
        return {};
    }

    const QString file_name = anomalibMaskFileName(image_id);
    const QString path      = QDir(masks_dir).filePath(file_name);
    QImage        image(mask.data(), image_width, image_height, image_width, QImage::Format_Grayscale8);
    if (image.isNull() || !image.save(path))
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("写入 anomalib mask 失败: %1").arg(path);
        return {};
    }
    return file_name;
}

YAML::Node labelNode(const IModelDatasetSource *source, FrameworkDatasetLayout layout, qint64 label_id, int image_width,
                     int image_height, std::map<qint64, int> &class_indices)
{
    YAML::Node        node(YAML::NodeType::Map);
    const qint64      label_class_id = source->labelClassId(label_id);
    const QString     class_name     = label_class_id >= 0 ? source->labelClassName(label_class_id) : QString();
    const QString     class_group    = label_class_id >= 0 ? source->labelClassGroup(label_class_id) : QString();
    const QVariantMap data           = source->labelData(label_id);

    setInteger(node, ManifestField::LabelId, label_id);
    setInteger(node, ManifestField::LabelClassId, label_class_id);
    setString(node, ManifestField::LabelClassName, class_name);
    setString(node, ManifestField::LabelClassGroup, class_group);
    setInteger(node, ManifestField::ClassIndex, classIndex(label_class_id, class_indices));
    dltool::common::yaml::setMapValue(node, manifestFieldName(ManifestField::Data),
                                      dltool::common::yaml::variantToYaml(data));

    if (layout == FrameworkDatasetLayout::Ultralytics)
    {
        const QVariantMap yolo = labelYoloData(data, image_width, image_height);
        if (!yolo.isEmpty())
            dltool::common::yaml::setMapValue(node, manifestFieldName(ManifestField::Yolo),
                                              dltool::common::yaml::variantToYaml(yolo));
    }
    return node;
}

QVariantMap exportSplit(const ModelDatasetExportRequest &request, DatasetSplit split, bool required, QString *err_msg)
{
    const IModelDatasetSource *source = request.source;
    if (source == nullptr)
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("模型数据源为空");
        return {};
    }

    const ModelDatasetSelection *selection = nullptr;
    switch (split)
    {
    case DatasetSplit::Train:
        selection = &request.selections.train;
        break;
    case DatasetSplit::Validation:
        selection = &request.selections.validation;
        break;
    case DatasetSplit::Test:
        selection = &request.selections.test;
        break;
    }

    if (selection == nullptr || selection->isEmpty())
    {
        if (required && err_msg != nullptr)
            *err_msg = QStringLiteral("未选择%1数据集").arg(datasetSplitName(split));
        return {};
    }

    const FrameworkDatasetLayout layout     = datasetLayout(request.framework_name);
    const QString                split_name = datasetSplitName(split);
    const QString                split_dir  = layout == FrameworkDatasetLayout::Anomalib
                                                ? request.dataset_dir
                                                : QDir(request.dataset_dir).filePath(split_name);
    if (!ensureDirectory(split_dir, err_msg))
        return {};

    QString masks_dir;
    if (layout == FrameworkDatasetLayout::Anomalib)
    {
        masks_dir = QDir(request.dataset_dir).filePath(datasetSubdirName(DatasetSubdir::Masks));
        if (!ensureDirectory(masks_dir, err_msg))
            return {};
    }
    else if (layout == FrameworkDatasetLayout::FsSam2)
    {
        masks_dir = QDir(split_dir).filePath(datasetSubdirName(DatasetSubdir::Masks));
        if (!ensureDirectory(masks_dir, err_msg))
            return {};
    }

    YAML::Node manifest(YAML::NodeType::Map);
    setInteger(manifest, ManifestField::Version, 1);
    setInteger(manifest, ManifestField::Method, request.method);
    setString(manifest, ManifestField::Framework, request.framework_name);
    setString(manifest, ManifestField::ModelArchitecture, request.model_architecture);
    setString(manifest, ManifestField::ModelUuid, request.model_uuid);
    setString(manifest, ManifestField::Split, split_name);

    YAML::Node            images(YAML::NodeType::Sequence);
    YAML::Node            samples(YAML::NodeType::Sequence);
    std::map<qint64, int> class_indices;
    int                   image_count = 0;
    int                   label_count = 0;

    std::vector<int64_t> all_image_ids = source->allImageIds();
    std::sort(all_image_ids.begin(), all_image_ids.end());
    for (const int64_t image_id : all_image_ids)
    {
        const qint64 dataset_id = source->imageDatasetId(image_id);
        if (!selection->containsDataset(dataset_id))
            continue;

        const QString image_path = source->imagePath(image_id);
        if (image_path.trimmed().isEmpty())
            continue;

        int image_width  = 0;
        int image_height = 0;
        dltool::data::DatasetIO::getImageDimensions(image_path, image_width, image_height);

        QVariantMap  image_level_label = source->imageLevelLabelData(image_id);
        const qint64 image_label_class_id
            = image_level_label.value(manifestFieldName(ManifestField::LabelClassId), -1).toLongLong();
        const QString image_label_class_name
            = image_level_label.value(manifestFieldName(ManifestField::LabelClassName)).toString();
        const QString image_label_group = image_level_label.value(manifestFieldName(ManifestField::Group)).toString();

        YAML::Node                        labels(YAML::NodeType::Sequence);
        bool                              has_anomaly_label = isAnomalyGroup(image_label_group);
        std::vector<std::vector<QPointF>> anomaly_polygons;
        for (const int64_t label_id : source->imageLabelIds(image_id))
        {
            const qint64 label_class_id = source->labelClassId(label_id);
            if (!selection->contains(dataset_id, label_class_id))
                continue;

            const QString     class_group = label_class_id >= 0 ? source->labelClassGroup(label_class_id) : QString();
            const QVariantMap label_data  = source->labelData(label_id);
            YAML::Node        label = labelNode(source, layout, label_id, image_width, image_height, class_indices);
            if (layout == FrameworkDatasetLayout::FsSam2)
            {
                QString       mask_err;
                const QString mask_path = writeFsSam2LabelMask(label_data, image_width, image_height, masks_dir,
                                                               image_id, label_id, &mask_err);
                if (!mask_err.isEmpty())
                {
                    if (err_msg != nullptr)
                        *err_msg = mask_err;
                    return {};
                }
                if (mask_path.isEmpty())
                    continue;
                setString(label, ManifestField::MaskPath, mask_path);
            }

            has_anomaly_label = has_anomaly_label || isAnomalyGroup(class_group);
            if (layout == FrameworkDatasetLayout::Anomalib && isAnomalyGroup(class_group))
            {
                std::vector<QPointF> polygon = labelPolygon(label_data);
                if (polygon.size() >= 3)
                    anomaly_polygons.push_back(std::move(polygon));
            }
            labels.push_back(label);
            ++label_count;
        }

        const bool image_selected_by_image_label = selection->contains(dataset_id, image_label_class_id);
        if (!image_selected_by_image_label && labels.size() == 0)
            continue;

        if (layout == FrameworkDatasetLayout::Anomalib)
        {
            YAML::Node sample(YAML::NodeType::Map);
            setInteger(sample, ManifestField::Id, image_id);
            setString(sample, ManifestField::Path, dltool::common::cleanPath(image_path));
            setInteger(sample, ManifestField::LabelIndex, has_anomaly_label ? 1 : 0);
            if (has_anomaly_label)
            {
                QString       mask_err;
                const QString mask_file = writeAnomalibImageMask(anomaly_polygons, image_width, image_height, masks_dir,
                                                                 image_id, &mask_err);
                if (!mask_err.isEmpty())
                {
                    if (err_msg != nullptr)
                        *err_msg = mask_err;
                    return {};
                }
                setString(sample, ManifestField::Mask, mask_file);
            }
            else
            {
                setString(sample, ManifestField::Mask, QString());
            }
            samples.push_back(sample);
            ++image_count;
            continue;
        }

        YAML::Node image(YAML::NodeType::Map);
        setInteger(image, ManifestField::Id, image_id);
        setString(image, ManifestField::Path, dltool::common::cleanPath(image_path));
        setInteger(image, ManifestField::DatasetId, dataset_id);
        setString(image, ManifestField::DatasetName, source->datasetName(dataset_id));
        setInteger(image, ManifestField::Width, image_width);
        setInteger(image, ManifestField::Height, image_height);
        setInteger(image, ManifestField::ImageLabelClassId, image_label_class_id);
        setString(image, ManifestField::ImageLabelClassName, image_label_class_name);
        setString(image, ManifestField::ImageLabelGroup, image_label_group);
        setBool(image, ManifestField::Anomaly, has_anomaly_label);
        setInteger(image, ManifestField::LabelIndex, has_anomaly_label ? 1 : 0);
        dltool::common::yaml::setMapValue(image, manifestFieldName(ManifestField::Labels), labels);
        images.push_back(image);
        ++image_count;
    }

    if (image_count <= 0)
    {
        if (required && err_msg != nullptr)
            *err_msg = QStringLiteral("%1数据集没有可用图像").arg(split_name);
        return {};
    }

    if (layout == FrameworkDatasetLayout::Anomalib)
    {
        YAML::Node file_list(YAML::NodeType::Map);
        setInteger(file_list, ManifestField::Version, 1);
        setInteger(file_list, ManifestField::Method, request.method);
        setString(file_list, ManifestField::Framework, request.framework_name);
        setString(file_list, ManifestField::ModelArchitecture, request.model_architecture);
        setString(file_list, ManifestField::ModelUuid, request.model_uuid);
        setString(file_list, ManifestField::Split, split_name);
        setString(file_list, ManifestField::MasksDir, dltool::common::cleanPath(masks_dir));
        dltool::common::yaml::setMapValue(file_list, manifestFieldName(ManifestField::Samples), samples);

        const QString file_list_path = anomalibFileListPath(request.dataset_dir, split_name);
        if (!dltool::common::yaml::writeFile(file_list_path, file_list, err_msg, QStringLiteral("写入数据集清单失败"),
                                             QStringLiteral("生成数据集清单 YAML 失败")))
            return {};

        return {
            {  datasetConfigFieldName(DatasetConfigField::FileList),                       file_list_path},
            {  datasetConfigFieldName(DatasetConfigField::MasksDir), dltool::common::cleanPath(masks_dir)},
            {datasetConfigFieldName(DatasetConfigField::ImageCount),                          image_count},
            {datasetConfigFieldName(DatasetConfigField::LabelCount),                          label_count},
        };
    }

    dltool::common::yaml::setMapValue(manifest, manifestFieldName(ManifestField::Images), images);
    const QString manifest_path = QDir(split_dir).filePath(datasetFileName(DatasetFileName::Manifest));
    if (!dltool::common::yaml::writeFile(manifest_path, manifest, err_msg, QStringLiteral("写入数据集清单失败"),
                                         QStringLiteral("生成数据集清单 YAML 失败")))
        return {};

    return {
        {  datasetConfigFieldName(DatasetConfigField::Manifest), manifest_path},
        {datasetConfigFieldName(DatasetConfigField::ImageCount),   image_count},
        {datasetConfigFieldName(DatasetConfigField::LabelCount),   label_count},
    };
}

} // namespace

QVariantMap ModelDatasetOrganizer::organize(const ModelDatasetExportRequest &request, QString *err_msg)
{
    QVariantMap datasets;
    if (request.dataset_dir.trimmed().isEmpty())
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("模型数据集目录为空");
        return {};
    }
    if (!ensureDirectory(request.dataset_dir, err_msg))
        return {};

    if (isTrainModelTask(request.task_type))
    {
        const QVariantMap train = exportSplit(request, DatasetSplit::Train, true, err_msg);
        if (train.isEmpty())
            return {};
        datasets.insert(datasetConfigFieldName(DatasetConfigField::Train), train);

        QString     validation_err;
        QVariantMap validation = exportSplit(request, DatasetSplit::Validation, false, &validation_err);
        if (!validation.isEmpty())
            datasets.insert(datasetConfigFieldName(DatasetConfigField::Validation), validation);
        else if (!validation_err.isEmpty())
            spdlog::debug("跳过验证数据集导出: {}", validation_err.toUtf8().constData());
    }
    else if (isTestModelTask(request.task_type))
    {
        const QVariantMap test = exportSplit(request, DatasetSplit::Test, true, err_msg);
        if (test.isEmpty())
            return {};
        datasets.insert(datasetConfigFieldName(DatasetConfigField::Test), test);
    }

    return datasets;
}

} // namespace dltool::model
