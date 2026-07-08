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
#include <memory>
#include <vector>

using dltool::common::cleanPath;
using dltool::common::yaml::setMapValue;
using dltool::common::yaml::variantToYaml;

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

enum class ManifestMetaField
{
    Version,
    Method,
    Framework,
    ModelArchitecture,
    ModelUuid,
    Split,
};

enum class GenericManifestField
{
    Images,
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

enum class AnomalibFileListField
{
    MasksDir,
    Samples,
};

enum class AnomalibSampleField
{
    Id,
    Path,
    LabelIndex,
    Mask,
};

enum class ImageLevelLabelField
{
    LabelClassId,
    LabelClassName,
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

const std::map<ManifestMetaField, QString> &manifestMetaFieldNames()
{
    static const std::map<ManifestMetaField, QString> names = {
        {          ManifestMetaField::Version,            QStringLiteral("version")},
        {           ManifestMetaField::Method,             QStringLiteral("method")},
        {        ManifestMetaField::Framework,          QStringLiteral("framework")},
        {ManifestMetaField::ModelArchitecture, QStringLiteral("model_architecture")},
        {        ManifestMetaField::ModelUuid,         QStringLiteral("model_uuid")},
        {            ManifestMetaField::Split,              QStringLiteral("split")},
    };
    return names;
}

const std::map<GenericManifestField, QString> &genericManifestFieldNames()
{
    static const std::map<GenericManifestField, QString> names = {
        {GenericManifestField::Images, QStringLiteral("images")},
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

const std::map<AnomalibFileListField, QString> &anomalibFileListFieldNames()
{
    static const std::map<AnomalibFileListField, QString> names = {
        {AnomalibFileListField::MasksDir, QStringLiteral("masks_dir")},
        { AnomalibFileListField::Samples,   QStringLiteral("samples")},
    };
    return names;
}

const std::map<AnomalibSampleField, QString> &anomalibSampleFieldNames()
{
    static const std::map<AnomalibSampleField, QString> names = {
        {        AnomalibSampleField::Id,          QStringLiteral("id")},
        {      AnomalibSampleField::Path,        QStringLiteral("path")},
        {AnomalibSampleField::LabelIndex, QStringLiteral("label_index")},
        {      AnomalibSampleField::Mask,        QStringLiteral("mask")},
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
        {LabelGroupName::Anomaly, QStringLiteral("anomaly")},
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

bool ensureDirectory(const QString &path, QString *err_msg)
{
    const QString cleaned = cleanPath(path);
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

QString anomalibMaskFileName(qint64 image_id)
{
    return mappedValue(datasetFileNames(), DatasetFileName::ImageMask).arg(image_id);
}

QString anomalibFileListPath(const QString &dataset_dir, const QString &split_name)
{
    return QDir(dataset_dir).filePath(mappedValue(datasetFileNames(), DatasetFileName::SplitFileList).arg(split_name));
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
    return cleanPath(path);
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

enum class LabelExportDecision
{
    Keep,
    Skip,
    Error,
};

struct SplitExportContext
{
    const ModelDatasetExportRequest *request{};
    const IModelDatasetSource       *source{};
    const ModelDatasetSelection     *selection{};
    DatasetSplit                     split{DatasetSplit::Train};
    QString                          split_name;
    QString                          split_dir;
    QString                          masks_dir;
    YAML::Node                       manifest{YAML::NodeType::Map};
    YAML::Node                       images{YAML::NodeType::Sequence};
    YAML::Node                       samples{YAML::NodeType::Sequence};
    std::map<qint64, int>            class_indices;
    int                              image_count{0};
    int                              label_count{0};
};

struct ImageExportContext
{
    qint64                            image_id{-1};
    qint64                            dataset_id{-1};
    QString                           image_path;
    int                               width{0};
    int                               height{0};
    QVariantMap                       image_level_label;
    qint64                            image_label_class_id{-1};
    QString                           image_label_class_name;
    QString                           image_label_group;
    bool                              has_anomaly_label{false};
    bool                              selected_by_image_label{false};
    std::vector<std::vector<QPointF>> anomaly_polygons;
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

    QVariantMap exportSplit(const ModelDatasetExportRequest &request, DatasetSplit split, bool required,
                            QString *err_msg) const
    {
        if (request.source == nullptr)
        {
            if (err_msg != nullptr)
                *err_msg = QStringLiteral("模型数据源为空");
            return {};
        }

        const ModelDatasetSelection *selection = selectionForSplit(request, split);
        if (selection == nullptr || selection->isEmpty())
        {
            if (required && err_msg != nullptr)
                *err_msg = QStringLiteral("未选择%1数据集").arg(mappedValue(datasetSplitNames(), split));
            return {};
        }

        SplitExportContext ctx;
        ctx.request    = &request;
        ctx.source     = request.source;
        ctx.selection  = selection;
        ctx.split      = split;
        ctx.split_name = mappedValue(datasetSplitNames(), split);
        ctx.split_dir  = splitDirectory(request, ctx.split_name);
        initializeManifest(ctx);

        if (!prepareSplit(ctx, err_msg))
            return {};

        std::vector<int64_t> all_image_ids = request.source->allImageIds();
        std::sort(all_image_ids.begin(), all_image_ids.end());
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
            ++ctx.image_count;
        }

        if (ctx.image_count <= 0)
        {
            if (required && err_msg != nullptr)
                *err_msg = QStringLiteral("%1数据集没有可用图像").arg(ctx.split_name);
            return {};
        }

        return finishSplit(ctx, err_msg);
    }

protected:
    virtual QString splitDirectory(const ModelDatasetExportRequest &request, const QString &split_name) const
    {
        return QDir(request.dataset_dir).filePath(split_name);
    }

    virtual bool prepareSplit(SplitExportContext &ctx, QString *err_msg) const
    {
        return ensureDirectory(ctx.split_dir, err_msg);
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
                             QString *err_msg) const
        = 0;

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
    void initializeManifest(SplitExportContext &ctx) const
    {
        ctx.manifest = YAML::Node(YAML::NodeType::Map);
        setMapValue(ctx.manifest, mappedValue(manifestMetaFieldNames(), ManifestMetaField::Version), 1);
        setMapValue(ctx.manifest, mappedValue(manifestMetaFieldNames(), ManifestMetaField::Method),
                    ctx.request->method);
        setMapValue(ctx.manifest, mappedValue(manifestMetaFieldNames(), ManifestMetaField::Framework),
                    ctx.request->framework_name);
        setMapValue(ctx.manifest, mappedValue(manifestMetaFieldNames(), ManifestMetaField::ModelArchitecture),
                    ctx.request->model_architecture);
        setMapValue(ctx.manifest, mappedValue(manifestMetaFieldNames(), ManifestMetaField::ModelUuid),
                    ctx.request->model_uuid);
        setMapValue(ctx.manifest, mappedValue(manifestMetaFieldNames(), ManifestMetaField::Split), ctx.split_name);
    }

    bool readSelectedImage(const SplitExportContext &ctx, qint64 image_id, ImageExportContext &image) const
    {
        image.image_id   = image_id;
        image.dataset_id = ctx.source->imageDatasetId(image_id);
        if (!ctx.selection->containsDataset(image.dataset_id))
            return false;

        image.image_path = ctx.source->imagePath(image_id);
        if (image.image_path.trimmed().isEmpty())
            return false;

        dltool::data::DatasetIO::getImageDimensions(image.image_path, image.width, image.height);

        image.image_level_label = ctx.source->imageLevelLabelData(image_id);
        image.image_label_class_id
            = image.image_level_label
                  .value(mappedValue(imageLevelLabelFieldNames(), ImageLevelLabelField::LabelClassId), -1)
                  .toLongLong();
        image.image_label_class_name
            = image.image_level_label
                  .value(mappedValue(imageLevelLabelFieldNames(), ImageLevelLabelField::LabelClassName))
                  .toString();
        image.image_label_group
            = image.image_level_label.value(mappedValue(imageLevelLabelFieldNames(), ImageLevelLabelField::Group))
                  .toString();
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

            const QString class_name  = label_class_id >= 0 ? ctx.source->labelClassName(label_class_id) : QString();
            const QString class_group = label_class_id >= 0 ? ctx.source->labelClassGroup(label_class_id) : QString();
            const QVariantMap label_data = ctx.source->labelData(label_id);
            YAML::Node        label = baseLabelNode(ctx, label_id, label_class_id, class_name, class_group, label_data);

            const LabelExportDecision decision
                = augmentLabel(ctx, image, label_id, label_class_id, class_group, label_data, label, err_msg);
            if (decision == LabelExportDecision::Error)
                return false;
            if (decision == LabelExportDecision::Skip)
                continue;

            image.has_anomaly_label = image.has_anomaly_label || isAnomalyGroup(class_group);
            labels.push_back(label);
            ++ctx.label_count;
        }
        return true;
    }
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
        setMapValue(ctx.manifest, mappedValue(genericManifestFieldNames(), GenericManifestField::Images), ctx.images);
        const QString manifest_path
            = QDir(ctx.split_dir).filePath(mappedValue(datasetFileNames(), DatasetFileName::Manifest));
        if (!dltool::common::yaml::writeFile(manifest_path, ctx.manifest, err_msg, QStringLiteral("写入数据集清单失败"),
                                             QStringLiteral("生成数据集清单 YAML 失败")))
            return {};

        return {
            {mappedValue(datasetConfigFieldNames(),   DatasetConfigField::Manifest),   manifest_path},
            {mappedValue(datasetConfigFieldNames(), DatasetConfigField::ImageCount), ctx.image_count},
            {mappedValue(datasetConfigFieldNames(), DatasetConfigField::LabelCount), ctx.label_count},
        };
    }
};

class UltralyticsDatasetOrganizer final : public GenericDatasetOrganizer
{
protected:
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
    bool prepareSplit(SplitExportContext &ctx, QString *err_msg) const override
    {
        if (!GenericDatasetOrganizer::prepareSplit(ctx, err_msg))
            return false;
        ctx.masks_dir = QDir(ctx.split_dir).filePath(mappedValue(datasetSubdirNames(), DatasetSubdir::Masks));
        return ensureDirectory(ctx.masks_dir, err_msg);
    }

    LabelExportDecision augmentLabel(SplitExportContext &ctx, ImageExportContext &image, qint64 label_id,
                                     qint64 label_class_id, const QString &class_group, const QVariantMap &label_data,
                                     YAML::Node &label, QString *err_msg) const override
    {
        Q_UNUSED(label_class_id)
        Q_UNUSED(class_group)

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
        ctx.masks_dir
            = QDir(ctx.request->dataset_dir).filePath(mappedValue(datasetSubdirNames(), DatasetSubdir::Masks));
        return ensureDirectory(ctx.masks_dir, err_msg);
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

        YAML::Node sample(YAML::NodeType::Map);
        setMapValue(sample, mappedValue(anomalibSampleFieldNames(), AnomalibSampleField::Id), image.image_id);
        setMapValue(sample, mappedValue(anomalibSampleFieldNames(), AnomalibSampleField::Path),
                    cleanPath(image.image_path));
        setMapValue(sample, mappedValue(anomalibSampleFieldNames(), AnomalibSampleField::LabelIndex),
                    image.has_anomaly_label ? 1 : 0);
        if (image.has_anomaly_label)
        {
            QString       mask_err;
            const QString mask_file = writeAnomalibImageMask(image.anomaly_polygons, image.width, image.height,
                                                             ctx.masks_dir, image.image_id, &mask_err);
            if (!mask_err.isEmpty())
            {
                if (err_msg != nullptr)
                    *err_msg = mask_err;
                return false;
            }
            setMapValue(sample, mappedValue(anomalibSampleFieldNames(), AnomalibSampleField::Mask), mask_file);
        }
        else
        {
            setMapValue(sample, mappedValue(anomalibSampleFieldNames(), AnomalibSampleField::Mask), QString());
        }
        ctx.samples.push_back(sample);
        return true;
    }

    QVariantMap finishSplit(SplitExportContext &ctx, QString *err_msg) const override
    {
        YAML::Node file_list = ctx.manifest;
        setMapValue(file_list, mappedValue(anomalibFileListFieldNames(), AnomalibFileListField::MasksDir),
                    cleanPath(ctx.masks_dir));
        setMapValue(file_list, mappedValue(anomalibFileListFieldNames(), AnomalibFileListField::Samples), ctx.samples);

        const QString file_list_path = anomalibFileListPath(ctx.request->dataset_dir, ctx.split_name);
        if (!dltool::common::yaml::writeFile(file_list_path, file_list, err_msg, QStringLiteral("写入数据集清单失败"),
                                             QStringLiteral("生成数据集清单 YAML 失败")))
            return {};

        return {
            {mappedValue(datasetConfigFieldNames(),   DatasetConfigField::FileList),           file_list_path},
            {mappedValue(datasetConfigFieldNames(),   DatasetConfigField::MasksDir), cleanPath(ctx.masks_dir)},
            {mappedValue(datasetConfigFieldNames(), DatasetConfigField::ImageCount),          ctx.image_count},
            {mappedValue(datasetConfigFieldNames(), DatasetConfigField::LabelCount),          ctx.label_count},
        };
    }
};

std::unique_ptr<DatasetOrganizerBase> createDatasetOrganizer(const QString &framework_name)
{
    switch (datasetLayout(framework_name))
    {
    case FrameworkDatasetLayout::Anomalib:
        return std::make_unique<AnomalibDatasetOrganizer>();
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
            *err_msg = QStringLiteral("模型数据集目录为空");
        return {};
    }
    if (!ensureDirectory(request.dataset_dir, err_msg))
        return {};

    const std::unique_ptr<DatasetOrganizerBase> organizer = createDatasetOrganizer(request.framework_name);
    if (isTrainModelTask(request.task_type))
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
        const QVariantMap test = organizer->exportSplit(request, DatasetSplit::Test, true, err_msg);
        if (test.isEmpty())
            return {};
        datasets.insert(mappedValue(datasetConfigFieldNames(), DatasetConfigField::Test), test);
    }

    return datasets;
}

} // namespace dltool::model
