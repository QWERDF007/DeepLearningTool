#include "data/DataIO.h"

#include "common/MaskPolygonUtils.h"
#include "common/Utils.h"
#include "core/CoreDef.h"
#include "data/DataFormat.h"
#include "data/DataNameUtils.h"
#include "data/DataOperationWorkflow.h"
#include "settings/GlobalSettings.h"
#include "settings/SettingsValue.h"
#include "ui/ProgressManager.h"

#include <json.hpp>
#include <spdlog/spdlog.h>

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPolygonF>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <utility>

using dltool::common::ensureDirectory;
using dltool::core::DeepLearningMethod;

namespace dltool::data {

// ============================================================================
// Anonymous namespace: shared helpers
// ============================================================================

namespace {

constexpr double kDefaultMaskImportPolygonApproxRatio = 0.01;

double normalizedMaskImportPolygonApproxRatio(const double ratio)
{
    return std::isfinite(ratio) ? std::max(0.0, ratio) : kDefaultMaskImportPolygonApproxRatio;
}

double maskImportPolygonApproxRatio()
{
    namespace generated_field = dltool::settings::generated::field;

    return normalizedMaskImportPolygonApproxRatio(dltool::settings::settingDouble(
        dltool::settings::GlobalSettings::getInstance(), generated_field::Data::PolygonApproxEpsilonRatio,
        kDefaultMaskImportPolygonApproxRatio));
}

// ponytail: 统一 LabelMe / Mask 导出的唯一文件名生成
QString uniqueImageName(const QString &source_path, int64_t stable_id, const std::map<QString, int> &used_names,
                        const std::map<QString, int> &used_stems)
{
    const QFileInfo file_info(source_path);
    const QString   suffix = file_info.suffix().isEmpty() ? QString() : QString(".%1").arg(file_info.suffix());
    const QString   stem
        = file_info.completeBaseName().isEmpty() ? QString::number(stable_id) : file_info.completeBaseName();
    QString candidate = QString("%1%2").arg(stem, suffix);
    if (used_names.find(candidate) == used_names.end() && used_stems.find(stem) == used_stems.end())
        return candidate;

    QString candidate_stem = QString("%1_%2").arg(stem).arg(stable_id);
    candidate              = QString("%1%2").arg(candidate_stem, suffix);
    int index              = 1;
    while (used_names.find(candidate) != used_names.end() || used_stems.find(candidate_stem) != used_stems.end())
    {
        candidate_stem = QString("%1_%2_%3").arg(stem).arg(stable_id).arg(index++);
        candidate      = QString("%1%2").arg(candidate_stem, suffix);
    }
    return candidate;
}

void addScannedLabelClass(std::map<QString, QString> &label_class_info, const QString &raw_name, int &color_index)
{
    const QString name = sanitizeName(raw_name);
    if (name.isEmpty() || label_class_info.find(name) != label_class_info.end())
        return;

    label_class_info[name] = DatasetIO::generateDefaultColor(color_index++);
}

QStringList imageNameFilters()
{
    return {
        "*.jpg", "*.jpeg", "*.png", "*.bmp", "*.gif", "*.tiff", "*.tif", "*.webp",
        "*.JPG", "*.JPEG", "*.PNG", "*.BMP", "*.GIF", "*.TIFF", "*.TIF", "*.WEBP",
    };
}

std::vector<QString> scanImmediateImageFiles(const QString &image_dir)
{
    std::vector<QString> image_files;
    const QDir           dir(image_dir);
    if (!dir.exists())
        return image_files;

    const QFileInfoList files = dir.entryInfoList(imageNameFilters(), QDir::Files, QDir::Name);
    image_files.reserve(static_cast<size_t>(files.size()));
    for (const QFileInfo &file : files)
    {
        image_files.push_back(file.absoluteFilePath());
    }
    return image_files;
}

struct FolderClassInfo
{
    QString              name;
    std::vector<QString> image_paths;
};

std::vector<FolderClassInfo> collectFolderClasses(const QDir &root_dir)
{
    std::vector<FolderClassInfo> classes;
    std::map<QString, size_t>    class_index_by_name;

    auto append_class = [&](QString raw_name, std::vector<QString> images)
    {
        if (images.empty())
            return;

        QString class_name = sanitizeName(raw_name);
        if (class_name.isEmpty())
            class_name = QStringLiteral("root");

        const auto found = class_index_by_name.find(class_name);
        if (found != class_index_by_name.end())
        {
            auto &existing_images = classes[found->second].image_paths;
            existing_images.insert(existing_images.end(), images.begin(), images.end());
            return;
        }

        class_index_by_name[class_name] = classes.size();
        classes.push_back({class_name, std::move(images)});
    };

    append_class(root_dir.dirName(), scanImmediateImageFiles(root_dir.absolutePath()));

    const QStringList class_dirs = root_dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &dir_name : class_dirs)
    {
        const QDir class_dir(root_dir.filePath(dir_name));
        append_class(dir_name, DatasetIO::scanImageFiles(class_dir.absolutePath()));
    }

    return classes;
}

// ============================================================================
// COCO helpers
// ============================================================================

struct ImportCancelled : public std::exception
{
    const char *what() const noexcept override
    {
        return "import cancelled";
    }
};

std::filesystem::path toFilesystemPath(const QString &path)
{
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}

bool parseJsonFile(const QString &json_path, nlohmann::json::parser_callback_t callback)
{
    try
    {
        std::ifstream input(toFilesystemPath(json_path), std::ios::binary);
        if (!input.is_open())
        {
            spdlog::error("无法打开 JSON 文件: {}", json_path.toStdString());
            return false;
        }
        const nlohmann::json parsed = nlohmann::json::parse(input, callback, true, true);
        (void)parsed;
        return true;
    }
    catch (const ImportCancelled &)
    {
        return false;
    }
    catch (const std::exception &e)
    {
        spdlog::error("解析 JSON 文件失败: {}, 错误: {}", json_path.toStdString(), e.what());
        return false;
    }
}

bool parseTopLevelArrayObjects(const QString                                                      &json_path,
                               const std::function<bool(const QString &, const nlohmann::json &)> &on_object)
{
    QString pending_top_key;
    QString active_array_key;

    nlohmann::json::parser_callback_t callback
        = [&](int depth, nlohmann::json::parse_event_t event, nlohmann::json &parsed) -> bool
    {
        if (event == nlohmann::json::parse_event_t::key && depth == 1 && parsed.is_string())
        {
            pending_top_key = QString::fromStdString(parsed.get<std::string>());
            return true;
        }
        if (event == nlohmann::json::parse_event_t::array_start && depth == 1)
        {
            active_array_key = pending_top_key;
            return true;
        }
        if (event == nlohmann::json::parse_event_t::object_end && depth == 2 && !active_array_key.isEmpty())
        {
            if (!on_object(active_array_key, parsed))
                throw ImportCancelled();
            return false;
        }
        if (event == nlohmann::json::parse_event_t::array_end && depth == 1)
        {
            active_array_key.clear();
            return false;
        }
        if (event == nlohmann::json::parse_event_t::object_end && depth == 1)
            return false;
        return true;
    };

    return parseJsonFile(json_path, callback);
}

bool parseTopLevelKeys(const QString &json_path, bool &has_images, bool &has_annotations, bool &has_categories)
{
    has_images      = false;
    has_annotations = false;
    has_categories  = false;

    QString                           pending_top_key;
    QString                           active_array_key;
    nlohmann::json::parser_callback_t callback
        = [&](int depth, nlohmann::json::parse_event_t event, nlohmann::json &parsed) -> bool
    {
        if (event == nlohmann::json::parse_event_t::key && depth == 1 && parsed.is_string())
        {
            pending_top_key = QString::fromStdString(parsed.get<std::string>());
            return true;
        }
        if (event == nlohmann::json::parse_event_t::array_start && depth == 1)
        {
            has_images       = has_images || pending_top_key == QStringLiteral("images");
            has_annotations  = has_annotations || pending_top_key == QStringLiteral("annotations");
            has_categories   = has_categories || pending_top_key == QStringLiteral("categories");
            active_array_key = pending_top_key;
            return true;
        }
        if (event == nlohmann::json::parse_event_t::object_end && depth == 2 && !active_array_key.isEmpty())
            return false;
        if (event == nlohmann::json::parse_event_t::array_end && depth == 1)
        {
            active_array_key.clear();
            return false;
        }
        if (event == nlohmann::json::parse_event_t::object_end && depth == 1)
            return false;
        return true;
    };

    return parseJsonFile(json_path, callback);
}

bool jsonToInt64(const nlohmann::json &value, int64_t &out)
{
    if (value.is_number_integer() || value.is_number_unsigned())
    {
        out = value.get<int64_t>();
        return true;
    }
    return false;
}

double jsonToDouble(const nlohmann::json &value, double fallback = 0.0)
{
    return value.is_number() ? value.get<double>() : fallback;
}

std::vector<QPointF> jsonArrayToPolygon(const nlohmann::json &polygon_json)
{
    std::vector<QPointF> points;
    if (!polygon_json.is_array() || polygon_json.size() < 6)
        return points;

    points.reserve(polygon_json.size() / 2);
    for (size_t i = 0; i + 1 < polygon_json.size(); i += 2)
    {
        if (polygon_json[i].is_number() && polygon_json[i + 1].is_number())
            points.emplace_back(polygon_json[i].get<double>(), polygon_json[i + 1].get<double>());
    }
    return points.size() >= 3 ? points : std::vector<QPointF>();
}

// ponytail: COCO import/export 共用，消除重复
double polygonArea(const std::vector<QPointF> &points)
{
    if (points.size() < 3)
        return 0.0;

    double area = 0.0;
    for (size_t i = 0; i < points.size(); ++i)
    {
        const QPointF &a = points[i];
        const QPointF &b = points[(i + 1) % points.size()];
        area += a.x() * b.y() - b.x() * a.y();
    }
    return std::abs(area) / 2.0;
}

bool readRleSize(const nlohmann::json &segmentation, int &height, int &width)
{
    if (!segmentation.contains("size") || !segmentation["size"].is_array() || segmentation["size"].size() < 2)
        return false;
    height = static_cast<int>(jsonToDouble(segmentation["size"][0], 0.0));
    width  = static_cast<int>(jsonToDouble(segmentation["size"][1], 0.0));
    return height > 0 && width > 0;
}

std::vector<int64_t> decodeCompressedRleCounts(const std::string &counts)
{
    std::vector<int64_t> decoded;
    decoded.reserve(counts.size());

    size_t pos = 0;
    while (pos < counts.size())
    {
        int64_t value = 0;
        int     shift = 0;
        int     c     = 0;
        bool    more  = false;
        do
        {
            c = static_cast<unsigned char>(counts[pos++]) - 48;
            value |= static_cast<int64_t>(c & 0x1F) << (5 * shift);
            more = (c & 0x20) != 0;
            ++shift;
            if (!more && (c & 0x10))
                value |= -1LL << (5 * shift);
        }
        while (more && pos < counts.size());

        if (decoded.size() > 2)
            value += decoded[decoded.size() - 2];
        if (value < 0)
            return {};
        decoded.push_back(value);
    }
    return decoded;
}

bool decodeRleMask(const nlohmann::json &segmentation, std::vector<uint8_t> &mask, int &width, int &height)
{
    if (!segmentation.is_object() || !readRleSize(segmentation, height, width) || !segmentation.contains("counts"))
        return false;

    std::vector<int64_t> counts;
    const auto          &counts_json = segmentation["counts"];
    if (counts_json.is_array())
    {
        counts.reserve(counts_json.size());
        for (const auto &count_json : counts_json)
        {
            if (!count_json.is_number_integer() && !count_json.is_number_unsigned())
                return false;
            const int64_t count = count_json.get<int64_t>();
            if (count < 0)
                return false;
            counts.push_back(count);
        }
    }
    else if (counts_json.is_string())
    {
        counts = decodeCompressedRleCounts(counts_json.get<std::string>());
        if (counts.empty())
            return false;
    }
    else
    {
        return false;
    }

    const int64_t total = static_cast<int64_t>(width) * static_cast<int64_t>(height);
    if (total <= 0)
        return false;

    mask.assign(static_cast<size_t>(total), 0);
    int64_t offset         = 0;
    bool    fill           = false;
    bool    has_foreground = false;
    for (const int64_t count : counts)
    {
        const int64_t end = std::min(total, offset + count);
        if (fill)
        {
            has_foreground = has_foreground || end > offset;
            for (int64_t index = offset; index < end; ++index)
            {
                const int y                              = static_cast<int>(index % height);
                const int x                              = static_cast<int>(index / height);
                mask[static_cast<size_t>(y * width + x)] = 1;
            }
        }
        offset = end;
        fill   = !fill;
        if (offset >= total)
            break;
    }
    return has_foreground;
}

bool hasSegmentationData(const nlohmann::json &annotation_json)
{
    if (!annotation_json.contains("segmentation"))
        return false;
    const auto &segmentation = annotation_json["segmentation"];
    if (segmentation.is_array())
        return !segmentation.empty();
    if (segmentation.is_object())
        return segmentation.contains("counts") && segmentation.contains("size");
    return false;
}

std::vector<std::vector<QPointF>> parseSegmentationPolygons(const nlohmann::json &annotation_json,
                                                            const double          polygon_approx_epsilon_ratio)
{
    if (!hasSegmentationData(annotation_json))
        return {};

    const auto                       &segmentation = annotation_json["segmentation"];
    std::vector<std::vector<QPointF>> polygons;
    if (segmentation.is_array())
    {
        if (!segmentation.empty() && segmentation.front().is_array())
        {
            for (const auto &polygon_json : segmentation)
            {
                std::vector<QPointF> points = jsonArrayToPolygon(polygon_json);
                if (!points.empty())
                    polygons.push_back(std::move(points));
            }
        }
        else
        {
            std::vector<QPointF> points = jsonArrayToPolygon(segmentation);
            if (!points.empty())
                polygons.push_back(std::move(points));
        }
    }
    else if (segmentation.is_object())
    {
        std::vector<uint8_t> mask;
        int                  width  = 0;
        int                  height = 0;
        if (decodeRleMask(segmentation, mask, width, height))
            polygons = dltool::common::maskToPolygons(mask, width, height, false, polygon_approx_epsilon_ratio);
    }

    std::sort(polygons.begin(), polygons.end(), [](const std::vector<QPointF> &left, const std::vector<QPointF> &right)
              { return polygonArea(left) > polygonArea(right); });
    return polygons;
}

// ============================================================================
// LabelMe helpers
// ============================================================================

std::vector<QPointF> rectangleToPolygon(const QPointF &p1, const QPointF &p2)
{
    const double x_min = std::min(p1.x(), p2.x());
    const double y_min = std::min(p1.y(), p2.y());
    const double x_max = std::max(p1.x(), p2.x());
    const double y_max = std::max(p1.y(), p2.y());
    return {QPointF(x_min, y_min), QPointF(x_max, y_min), QPointF(x_max, y_max), QPointF(x_min, y_max)};
}

bool containsOnlyRectangleShapes(const LabelMeIO::LabelMeData &data)
{
    return !data.shapes.empty()
        && std::all_of(data.shapes.begin(), data.shapes.end(), [](const LabelMeIO::LabelMeShape &shape)
                       { return shape.shape_type == QStringLiteral("rectangle"); });
}

// ============================================================================
// Mask helpers
// ============================================================================

constexpr int kMaskThreshold = 128;

bool isForeground(const QImage &image, int x, int y)
{
    if (x < 0 || y < 0 || x >= image.width() || y >= image.height())
        return false;
    return qGray(image.pixel(x, y)) >= kMaskThreshold;
}

QRect foregroundBoundingBox(const QImage &mask)
{
    int x_min = mask.width();
    int y_min = mask.height();
    int x_max = -1;
    int y_max = -1;

    for (int y = 0; y < mask.height(); ++y)
    {
        for (int x = 0; x < mask.width(); ++x)
        {
            if (!isForeground(mask, x, y))
                continue;
            x_min = std::min(x_min, x);
            y_min = std::min(y_min, y);
            x_max = std::max(x_max, x);
            y_max = std::max(y_max, y);
        }
    }
    if (x_max < x_min || y_max < y_min)
        return {};
    return QRect(QPoint(x_min, y_min), QPoint(x_max + 1, y_max + 1));
}

void addImageMapEntry(std::map<QString, QString> &image_by_stem, const QString &key, const QString &image_path)
{
    const QString trimmed_key  = key.trimmed();
    const QString trimmed_path = image_path.trimmed();
    if (trimmed_key.isEmpty() || trimmed_path.isEmpty())
        return;

    image_by_stem[trimmed_key] = trimmed_path;

    const QString cleaned_key = dltool::common::cleanPath(trimmed_key);
    if (!cleaned_key.isEmpty())
        image_by_stem[cleaned_key] = trimmed_path;
}

void addImagePathAliases(std::map<QString, QString> &image_by_stem, const QString &image_path)
{
    const QString trimmed_path = image_path.trimmed();
    if (trimmed_path.isEmpty())
        return;

    addImageMapEntry(image_by_stem, QFileInfo(trimmed_path).completeBaseName(), trimmed_path);
    addImageMapEntry(image_by_stem, trimmed_path, trimmed_path);
    addImageMapEntry(image_by_stem, QFileInfo(trimmed_path).absoluteFilePath(), trimmed_path);
}

std::map<QString, QString> loadImageMap(const QString &image_dir)
{
    std::map<QString, QString> image_by_stem;
    const QFileInfo            image_info(image_dir);
    if (image_info.isFile())
    {
        QFile file(image_info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return image_by_stem;

        QTextStream stream(&file);
        while (!stream.atEnd())
        {
            const QString line = stream.readLine().trimmed();
            if (line.isEmpty())
                continue;

            const int comma = line.indexOf(QLatin1Char(','));
            if (comma >= 0)
            {
                const QString alias = line.left(comma).trimmed();
                const QString path  = line.mid(comma + 1).trimmed();
                if (!alias.isEmpty() && !path.isEmpty())
                {
                    addImageMapEntry(image_by_stem, alias, path);
                    addImagePathAliases(image_by_stem, path);
                }
                continue;
            }
            addImagePathAliases(image_by_stem, line);
        }
        return image_by_stem;
    }

    const std::vector<QString> image_files = DatasetIO::scanImageFiles(image_dir);
    for (const QString &image_path : image_files)
    {
        addImagePathAliases(image_by_stem, image_path);
    }
    return image_by_stem;
}

struct ImportCancelled; // forward decl (already defined above in COCO section)

// ============================================================================
// Mask export helpers
// ============================================================================

constexpr const char *kMaskOutputModeOption = "mask_output_mode";

enum class MaskOutputMode
{
    All255     = 0,
    ClassIndex = 1,
};

MaskOutputMode maskOutputModeFromOptions(const QVariantMap &options)
{
    const int mode = options.value(QString::fromUtf8(kMaskOutputModeOption), 0).toInt();
    return mode == static_cast<int>(MaskOutputMode::ClassIndex) ? MaskOutputMode::ClassIndex : MaskOutputMode::All255;
}

QString maskOutputModeName(MaskOutputMode mode)
{
    return mode == MaskOutputMode::ClassIndex ? QStringLiteral("class_index") : QStringLiteral("all_255");
}

QPolygonF variantPointsToPolygon(const QVariant &value)
{
    QPolygonF polygon;
    for (const QPointF &point : DatasetIO::variantListToPoints(value)) polygon << point;
    return polygon;
}

bool paintLabelToMask(QImage &mask, const QVariantMap &label_data, int value)
{
    if (mask.isNull() || value <= 0)
        return false;

    QPainter painter(&mask);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(value, value, value));

    const QPolygonF polygon = variantPointsToPolygon(label_data.value(QStringLiteral("points")));
    if (polygon.size() >= 3)
    {
        painter.drawPolygon(polygon);
        return true;
    }

    const QRectF rect(
        label_data.value(QStringLiteral("x")).toDouble(), label_data.value(QStringLiteral("y")).toDouble(),
        label_data.value(QStringLiteral("width")).toDouble(), label_data.value(QStringLiteral("height")).toDouble());
    if (rect.width() <= 0 || rect.height() <= 0)
        return false;

    painter.fillRect(rect, QColor(value, value, value));
    return true;
}

bool writeClassMetadata(const ExportDataset &dataset, const QString &output_dir, MaskOutputMode mode,
                        const std::map<int64_t, int> &class_values, QString &err_msg)
{
    nlohmann::json json_data;
    json_data["mode"]       = maskOutputModeName(mode).toStdString();
    json_data["background"] = 0;
    json_data["classes"]    = nlohmann::json::array();

    for (const ExportLabelClass &label_class : dataset.label_classes)
    {
        const auto value_it = class_values.find(label_class.id);
        if (value_it == class_values.end())
            continue;
        json_data["classes"].push_back({
            {"value",                value_it->second},
            {   "id",                  label_class.id},
            { "name",  label_class.name.toStdString()},
            {"color", label_class.color.toStdString()},
        });
    }

    QFile metadata_file(QDir(output_dir).filePath(QStringLiteral("classes.json")));
    if (!metadata_file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        err_msg = QString("无法写入 Mask 类别映射文件: %1").arg(metadata_file.fileName());
        return false;
    }
    metadata_file.write(QByteArray::fromStdString(json_data.dump(2)));
    return true;
}

} // namespace

// ============================================================================
// DataIO base class
// ============================================================================

DataIO::DataIO(QObject *parent)
    : QObject(parent)
{
}

DataIO::~DataIO() = default;

void DataIO::requestCancel()
{
    cancel_requested_.store(true, std::memory_order_relaxed);
}

bool DataIO::isCancelRequested() const
{
    return cancel_requested_.load(std::memory_order_relaxed);
}

DataIO *DataIO::createIO(int data_format, QObject *parent)
{
    if (!DataFormat::isDataFormatSupported(data_format))
    {
        spdlog::error("不支持的数据格式: {}", data_format);
        return nullptr;
    }

    switch (data_format)
    {
    case DataFormat::LabelMe:
        return new LabelMeIO(parent);
    case DataFormat::COCO:
        return new COCOIO(parent);
    case DataFormat::Mask:
        return new MaskIO(parent);
    case DataFormat::Folder:
        return new FolderIO(parent);
    default:
        spdlog::error("未实现的数据格式: {}", data_format);
        return nullptr;
    }
}

void DataIO::startImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir)
{
    Q_UNUSED(dataset_id)
    Q_UNUSED(image_dir)
    Q_UNUSED(data_dir)
}

void DataIO::startScanLabelClasses(const QString &image_dir, const QString &data_dir)
{
    Q_UNUSED(image_dir)
    Q_UNUSED(data_dir)
    emit labelClassesScanned(true, {}, QString());
}

void DataIO::startExport(ExportDataset dataset, const QString &output_dir, const QVariantMap &options)
{
    Q_UNUSED(dataset)
    Q_UNUSED(output_dir)
    Q_UNUSED(options)
}

void DataIO::updateProgress(int progress, const QString &message)
{
    QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "updateProgress", Qt::QueuedConnection,
                              Q_ARG(int, progress));
    QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection,
                              Q_ARG(int, spdlog::level::info), Q_ARG(QString, message));
}

void DataIO::runInThread(std::function<void()> work)
{
    DataOperationWorkflow::Options options;
    options.manage_progress = false;

    DataOperationWorkflow::start(
        this, std::move(options),
        [work = std::move(work)](DataOperationWorkflow::Result &result) mutable
        {
            work();
            result.success = true;
        },
        [](const DataOperationWorkflow::Result &result)
        {
            if (!result.success)
            {
                spdlog::error("DataIO 后台任务失败: {}", result.error.toUtf8().constData());
            }
        });
}

bool DataIO::importImagesOnly(int64_t dataset_id, const QString &image_dir, const QString &format_name)
{
    updateProgress(0, QString("正在扫描图像文件..."));
    const std::vector<QString> image_files = DatasetIO::scanImageFiles(image_dir);
    if (image_files.empty())
    {
        updateProgress(100, QString("未找到任何图像文件"));
        emit importFinished(false, {}, {});
        return false;
    }

    std::vector<QString> batch_image_paths;
    std::vector<int64_t> batch_image_widths;
    std::vector<int64_t> batch_image_heights;
    batch_image_paths.reserve(DataIO::ImportBatchImageCount);
    batch_image_widths.reserve(DataIO::ImportBatchImageCount);
    batch_image_heights.reserve(DataIO::ImportBatchImageCount);

    const int total_images = static_cast<int>(image_files.size());
    int       processed    = 0;
    int       valid_images = 0;
    int       skipped      = 0;

    auto flush_batch = [&]() -> bool
    {
        if (batch_image_paths.empty())
            return true;

        emit dataBatchReady(dataset_id, std::move(batch_image_paths), std::move(batch_image_widths),
                            std::move(batch_image_heights), {}, {}, processed, total_images);

        batch_image_paths.clear();
        batch_image_widths.clear();
        batch_image_heights.clear();
        batch_image_paths.reserve(DataIO::ImportBatchImageCount);
        batch_image_widths.reserve(DataIO::ImportBatchImageCount);
        batch_image_heights.reserve(DataIO::ImportBatchImageCount);
        return !isCancelRequested();
    };

    for (const QString &image_path : image_files)
    {
        if (isCancelRequested())
        {
            emit importFinished(false, {}, {});
            return false;
        }

        ++processed;
        int width  = 0;
        int height = 0;
        if (!DatasetIO::getImageDimensions(image_path, width, height))
        {
            ++skipped;
            continue;
        }

        ++valid_images;
        batch_image_paths.push_back(image_path);
        batch_image_widths.push_back(width);
        batch_image_heights.push_back(height);

        if (batch_image_paths.size() >= DataIO::ImportBatchImageCount && !flush_batch())
        {
            emit importFinished(false, {}, {});
            return false;
        }

        if (processed % std::max(1, total_images / 10) == 0 || processed == total_images)
        {
            const int progress = 10 + processed * 80 / std::max(1, total_images);
            updateProgress(progress, QString("已处理图像 %1/%2").arg(processed).arg(total_images));
        }
    }

    if (!flush_batch())
    {
        emit importFinished(false, {}, {});
        return false;
    }

    if (valid_images == 0)
    {
        updateProgress(100, QString("没有有效的图像可导入"));
        emit importFinished(false, {}, {});
        return false;
    }

    updateProgress(
        100, QString("%1 导入完成: %2 个图像，无标注，跳过图像 %3 个").arg(format_name).arg(valid_images).arg(skipped));
    emit importFinished(true, {}, {});
    return true;
}

// ============================================================================
// COCOIO
// ============================================================================

void COCOIO::startImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir)
{
    const double polygon_approx_epsilon_ratio = maskImportPolygonApproxRatio();
    runInThread([this, dataset_id, image_dir, data_dir, polygon_approx_epsilon_ratio]()
                { doImport(dataset_id, image_dir, data_dir, polygon_approx_epsilon_ratio); });
}

void COCOIO::startScanLabelClasses(const QString &image_dir, const QString &data_dir)
{
    Q_UNUSED(image_dir)
    runInThread([this, data_dir]() { doScanLabelClasses(data_dir); });
}

void COCOIO::startExport(ExportDataset dataset, const QString &output_dir, const QVariantMap &options)
{
    Q_UNUSED(options)
    runInThread([this, dataset = std::move(dataset), output_dir]() { doExport(std::move(dataset), output_dir); });
}

QString COCOIO::findCocoJsonFile(const QString &data_path) const
{
    const std::vector<QString> json_files = DatasetIO::scanJsonFiles(data_path);
    for (const QString &json_path : json_files)
    {
        if (looksLikeCocoJson(json_path))
            return json_path;
    }
    return QString();
}

bool COCOIO::looksLikeCocoJson(const QString &json_path) const
{
    bool has_images      = false;
    bool has_annotations = false;
    bool has_categories  = false;
    if (!parseTopLevelKeys(json_path, has_images, has_annotations, has_categories))
        return false;
    return has_images && has_annotations && has_categories;
}

QString COCOIO::resolveImagePath(const QString &image_dir, const QString &file_name,
                                 const std::map<QString, QString> &image_file_index) const
{
    QDir    root_dir(image_dir);
    QString direct_path = root_dir.filePath(file_name);
    direct_path         = dltool::common::cleanPath(direct_path);
    if (QFileInfo::exists(direct_path))
        return QFileInfo(direct_path).absoluteFilePath();

    const QString basename = QFileInfo(file_name).fileName();
    auto          found    = image_file_index.find(basename);
    if (found != image_file_index.end())
        return found->second;

    return QString();
}

void COCOIO::doScanLabelClasses(const QString &data_dir)
{
    try
    {
        const QString annotation_dir = data_dir.trimmed();
        if (annotation_dir.isEmpty())
        {
            emit labelClassesScanned(true, {}, QStringLiteral("未提供 COCO 标注目录"));
            return;
        }

        updateProgress(0, QStringLiteral("正在扫描 COCO 类别..."));
        const QString coco_json_path = findCocoJsonFile(annotation_dir);
        if (coco_json_path.isEmpty())
        {
            emit labelClassesScanned(false, {}, QStringLiteral("未找到有效的 COCO 标注文件"));
            return;
        }

        std::map<QString, QString> label_class_info;
        int                        color_index = 0;
        const bool                 ok          = parseTopLevelArrayObjects(
            coco_json_path,
            [&](const QString &array_key, const nlohmann::json &object_json) -> bool
            {
                if (isCancelRequested())
                    return false;

                if (array_key != QStringLiteral("categories"))
                    return true;

                if (object_json.contains("name") && object_json["name"].is_string())
                    addScannedLabelClass(label_class_info,
                                                                  QString::fromStdString(object_json["name"].get<std::string>()), color_index);
                return true;
            });

        if (!ok || isCancelRequested())
        {
            emit labelClassesScanned(false, {}, QStringLiteral("COCO 类别扫描已取消或失败"));
            return;
        }

        updateProgress(100, QStringLiteral("COCO 类别扫描完成: %1 个类别").arg(label_class_info.size()));
        emit labelClassesScanned(true, label_class_info, QString());
    }
    catch (const std::exception &e)
    {
        spdlog::error("COCO 类别扫描失败: {}", e.what());
        emit labelClassesScanned(false, {}, QString("COCO 类别扫描失败: %1").arg(e.what()));
    }
}

void COCOIO::doImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir,
                      const double polygon_approx_epsilon_ratio)
{
    spdlog::info("开始解析 COCO 数据: dataset_id={}", dataset_id);

    try
    {
        const QString annotation_dir = data_dir.trimmed();
        if (annotation_dir.isEmpty())
        {
            importImagesOnly(dataset_id, image_dir, QStringLiteral("COCO"));
            return;
        }

        updateProgress(0, QString("正在查找 COCO 标注文件..."));
        const QString coco_json_path = findCocoJsonFile(annotation_dir);
        if (coco_json_path.isEmpty())
        {
            updateProgress(100, QString("未找到有效的 COCO 标注文件"));
            emit importFinished(false, {}, {});
            return;
        }

        updateProgress(10, QString("正在索引图像文件..."));
        std::map<QString, QString> image_file_index;
        for (const QString &image_path : DatasetIO::scanImageFiles(image_dir))
            image_file_index[QFileInfo(image_path).fileName()] = image_path;

        std::map<int64_t, CocoCategory> categories_by_id;
        std::map<QString, QString>      label_class_info;
        std::vector<CocoImage>          images;
        std::map<int64_t, CocoImage>    images_by_coco_id;
        int                             color_index             = 0;
        int                             processed_image_entries = 0;
        int                             skipped_images          = 0;

        updateProgress(20, QString("正在流式解析 COCO 类别和图像..."));
        const bool pass1_ok = parseTopLevelArrayObjects(
            coco_json_path,
            [&](const QString &array_key, const nlohmann::json &object_json) -> bool
            {
                if (isCancelRequested())
                    return false;

                if (array_key == QStringLiteral("categories"))
                {
                    if (!object_json.contains("id") || !object_json.contains("name")
                        || !object_json["name"].is_string())
                        return true;

                    int64_t category_id = 0;
                    if (!jsonToInt64(object_json["id"], category_id))
                        return true;

                    CocoCategory category;
                    category.id   = category_id;
                    category.name = sanitizeName(QString::fromStdString(object_json["name"].get<std::string>()));
                    if (category.name.isEmpty())
                        return true;

                    categories_by_id[category.id] = category;
                    if (label_class_info.find(category.name) == label_class_info.end())
                        label_class_info[category.name] = DatasetIO::generateDefaultColor(color_index++);
                    return true;
                }

                if (array_key != QStringLiteral("images"))
                    return true;

                ++processed_image_entries;
                if (!object_json.contains("id") || !object_json.contains("file_name")
                    || !object_json["file_name"].is_string())
                {
                    ++skipped_images;
                    return true;
                }

                int64_t coco_image_id = 0;
                if (!jsonToInt64(object_json["id"], coco_image_id))
                {
                    ++skipped_images;
                    return true;
                }

                CocoImage image;
                image.coco_id    = coco_image_id;
                image.file_name  = QString::fromStdString(object_json["file_name"].get<std::string>());
                image.image_path = resolveImagePath(image_dir, image.file_name, image_file_index);
                if (image.image_path.isEmpty())
                {
                    spdlog::warn("COCO 图像文件不存在，跳过: {}", image.file_name.toStdString());
                    ++skipped_images;
                    return true;
                }

                image.width = object_json.contains("width") ? static_cast<int>(jsonToDouble(object_json["width"])) : 0;
                image.height
                    = object_json.contains("height") ? static_cast<int>(jsonToDouble(object_json["height"])) : 0;
                if (image.width <= 0 || image.height <= 0)
                {
                    int real_width  = 0;
                    int real_height = 0;
                    if (!DatasetIO::getImageDimensions(image.image_path, real_width, real_height))
                    {
                        ++skipped_images;
                        return true;
                    }
                    image.width  = real_width;
                    image.height = real_height;
                }

                images_by_coco_id[image.coco_id] = image;
                images.push_back(std::move(image));

                if (processed_image_entries % 1000 == 0)
                    updateProgress(20, QString("已解析 COCO 图像 %1").arg(processed_image_entries));
                return true;
            });

        if (!pass1_ok || isCancelRequested())
        {
            emit importFinished(false, {}, {});
            return;
        }

        if (images.empty())
        {
            updateProgress(100, QString("COCO 数据中没有可导入的有效图像"));
            emit importFinished(false, {}, {});
            return;
        }

        std::vector<QString> batch_image_paths;
        std::vector<int64_t> batch_image_widths;
        std::vector<int64_t> batch_image_heights;
        batch_image_paths.reserve(DataIO::ImportBatchImageCount);
        batch_image_widths.reserve(DataIO::ImportBatchImageCount);
        batch_image_heights.reserve(DataIO::ImportBatchImageCount);

        size_t emitted_images    = 0;
        bool   sent_classes      = false;
        auto   flush_image_batch = [&]() -> bool
        {
            if (batch_image_paths.empty())
                return true;

            std::map<QString, QString> batch_label_class_info;
            if (!sent_classes)
            {
                batch_label_class_info = label_class_info;
                sent_classes           = true;
            }

            emitted_images += batch_image_paths.size();
            emit dataBatchReady(dataset_id, std::move(batch_image_paths), std::move(batch_image_widths),
                                std::move(batch_image_heights), std::move(batch_label_class_info), {},
                                static_cast<int64_t>(emitted_images), static_cast<int64_t>(images.size()));

            batch_image_paths.clear();
            batch_image_widths.clear();
            batch_image_heights.clear();
            batch_image_paths.reserve(DataIO::ImportBatchImageCount);
            batch_image_widths.reserve(DataIO::ImportBatchImageCount);
            batch_image_heights.reserve(DataIO::ImportBatchImageCount);
            return !isCancelRequested();
        };

        updateProgress(40, QString("正在分批写入 COCO 图像..."));
        for (const CocoImage &image : images)
        {
            batch_image_paths.push_back(image.image_path);
            batch_image_widths.push_back(image.width);
            batch_image_heights.push_back(image.height);

            if (batch_image_paths.size() >= DataIO::ImportBatchImageCount)
            {
                if (!flush_image_batch())
                {
                    emit importFinished(false, {}, {});
                    return;
                }
            }
        }

        if (!flush_image_batch())
        {
            emit importFinished(false, {}, {});
            return;
        }

        updateProgress(60, QString("正在流式解析 COCO 标注..."));
        std::vector<ImportedLabel> batch_labels;
        batch_labels.reserve(DataIO::ImportBatchImageCount);
        int processed_annotations = 0;
        int skipped_annotations   = 0;
        int imported_label_count  = 0;

        auto flush_label_batch = [&]() -> bool
        {
            if (batch_labels.empty())
                return true;
            emit dataBatchReady(dataset_id, {}, {}, {}, {}, std::move(batch_labels),
                                static_cast<int64_t>(images.size()), static_cast<int64_t>(images.size()));
            batch_labels.clear();
            batch_labels.reserve(DataIO::ImportBatchImageCount);
            return !isCancelRequested();
        };

        const bool import_as_segmentation = target_method_ == DeepLearningMethod::Segmentation
                                         || target_method_ == DeepLearningMethod::AnomalyDetection;
        const bool pass2_ok = parseTopLevelArrayObjects(
            coco_json_path,
            [&](const QString &array_key, const nlohmann::json &annotation_json) -> bool
            {
                if (isCancelRequested())
                    return false;

                if (array_key != QStringLiteral("annotations"))
                    return true;

                ++processed_annotations;
                if (!annotation_json.contains("image_id") || !annotation_json.contains("category_id"))
                {
                    ++skipped_annotations;
                    return true;
                }

                int64_t coco_image_id = 0;
                int64_t category_id   = 0;
                if (!jsonToInt64(annotation_json["image_id"], coco_image_id)
                    || !jsonToInt64(annotation_json["category_id"], category_id))
                {
                    ++skipped_annotations;
                    return true;
                }

                auto image_it = images_by_coco_id.find(coco_image_id);
                auto class_it = categories_by_id.find(category_id);
                if (image_it == images_by_coco_id.end() || class_it == categories_by_id.end())
                {
                    ++skipped_annotations;
                    return true;
                }

                const bool               annotation_has_seg = hasSegmentationData(annotation_json);
                std::vector<QVariantMap> label_data_list;

                if (import_as_segmentation && annotation_has_seg)
                {
                    const std::vector<std::vector<QPointF>> segmentation_polygons
                        = parseSegmentationPolygons(annotation_json, polygon_approx_epsilon_ratio);
                    for (const std::vector<QPointF> &polygon : segmentation_polygons)
                    {
                        QVariantMap label_data
                            = DatasetIO::pointsToLabelData(polygon, image_it->second.width, image_it->second.height);
                        if (!label_data.isEmpty())
                            label_data_list.push_back(label_data);
                    }
                }

                if (label_data_list.empty())
                {
                    if (import_as_segmentation && annotation_has_seg)
                    {
                        spdlog::warn("COCO segmentation 存在但无法转换为多边形，跳过标注: image_id={}, category_id={}",
                                     coco_image_id, category_id);
                        ++skipped_annotations;
                        return true;
                    }

                    if (!annotation_json.contains("bbox") || !annotation_json["bbox"].is_array()
                        || annotation_json["bbox"].size() < 4)
                    {
                        ++skipped_annotations;
                        return true;
                    }

                    const auto &bbox       = annotation_json["bbox"];
                    QVariantMap label_data = DatasetIO::bboxToLabelData(
                        jsonToDouble(bbox[0]), jsonToDouble(bbox[1]), jsonToDouble(bbox[2]), jsonToDouble(bbox[3]),
                        image_it->second.width, image_it->second.height);
                    if (!label_data.isEmpty())
                        label_data_list.push_back(label_data);
                }

                if (label_data_list.empty())
                {
                    ++skipped_annotations;
                    return true;
                }

                for (const QVariantMap &label_data : label_data_list)
                {
                    ImportedLabel label;
                    label.label_class_name = class_it->second.name;
                    label.data             = label_data;
                    label.image_path       = image_it->second.image_path;
                    batch_labels.push_back(label);
                    ++imported_label_count;
                }

                if (batch_labels.size() >= DataIO::ImportBatchImageCount)
                {
                    if (!flush_label_batch())
                        return false;
                }

                if (processed_annotations % 1000 == 0)
                    updateProgress(75, QString("已解析 COCO 标注 %1").arg(processed_annotations));
                return true;
            });

        if (!pass2_ok || isCancelRequested())
        {
            emit importFinished(false, {}, {});
            return;
        }

        if (!flush_label_batch())
        {
            emit importFinished(false, {}, {});
            return;
        }

        updateProgress(100, QString("导入完成: %1 个图像, %2 个标注，跳过图像 %3 个，跳过标注 %4 个")
                                .arg(images.size())
                                .arg(imported_label_count)
                                .arg(skipped_images)
                                .arg(skipped_annotations));
        emit importFinished(true, {}, {});
    }
    catch (const std::exception &e)
    {
        spdlog::error("COCO 导入过程中发生异常: {}", e.what());
        updateProgress(100, QString("COCO 导入失败: %1").arg(e.what()));
        emit importFinished(false, {}, {});
    }
}

void COCOIO::doExport(ExportDataset dataset, QString output_dir)
{
    try
    {
        QString       err_msg;
        const QString images_dir      = QDir(output_dir).filePath(QStringLiteral("images"));
        const QString annotations_dir = QDir(output_dir).filePath(QStringLiteral("annotations"));
        if (!ensureDirectory(images_dir, err_msg) || !ensureDirectory(annotations_dir, err_msg))
        {
            emit exportFinished(false, err_msg);
            return;
        }

        nlohmann::json json_data;
        json_data["info"] = {
            {       "year",                       QDateTime::currentDateTime().date().year()},
            {    "version",                                                            "1.0"},
            {"description",                               dataset.dataset_name.toStdString()},
            {       "date", QDateTime::currentDateTime().toString(Qt::ISODate).toStdString()},
        };
        json_data["licenses"]    = nlohmann::json::array();
        json_data["images"]      = nlohmann::json::array();
        json_data["annotations"] = nlohmann::json::array();
        json_data["categories"]  = nlohmann::json::array();

        std::map<QString, int>     used_image_names;
        std::map<int64_t, QString> image_name_by_id;
        const int                  image_count = static_cast<int>(dataset.images.size());

        for (int i = 0; i < image_count; ++i)
        {
            const ExportImage &image     = dataset.images[i];
            const QString      file_name = DatasetIO::uniqueFileName(image.path, image.image_id, used_image_names);
            used_image_names[file_name]++;
            image_name_by_id[image.image_id] = file_name;

            const QString target_path = QDir(images_dir).filePath(file_name);
            if (!DatasetIO::copyFile(image.path, target_path, err_msg))
            {
                emit exportFinished(false, err_msg);
                return;
            }

            json_data["images"].push_back({
                {       "id",          image.image_id},
                {"file_name", file_name.toStdString()},
                {    "width",             image.width},
                {   "height",            image.height},
                {  "license",                       0},
            });

            if ((i + 1) % std::max(1, image_count / 10) == 0 || i + 1 == image_count)
                updateProgress((i + 1) * 45 / std::max(1, image_count),
                               QString("已复制图像 %1/%2").arg(i + 1).arg(image_count));
        }

        for (const ExportLabelClass &label_class : dataset.label_classes)
        {
            json_data["categories"].push_back({
                {           "id",                 label_class.id},
                {         "name", label_class.name.toStdString()},
                {"supercategory",                             ""},
            });
        }

        const int label_count = static_cast<int>(dataset.labels.size());
        for (int i = 0; i < label_count; ++i)
        {
            const ExportLabel &label = dataset.labels[i];
            const double       x     = label.data.value(QStringLiteral("x")).toDouble();
            const double       y     = label.data.value(QStringLiteral("y")).toDouble();
            const double       w     = label.data.value(QStringLiteral("width")).toDouble();
            const double       h     = label.data.value(QStringLiteral("height")).toDouble();
            if (w <= 0 || h <= 0)
                continue;

            nlohmann::json             segmentation = nlohmann::json::array();
            double                     area         = w * h;
            const std::vector<QPointF> points
                = DatasetIO::variantListToPoints(label.data.value(QStringLiteral("points")));
            if (points.size() >= 3)
            {
                nlohmann::json flat_points = nlohmann::json::array();
                for (const QPointF &point : points)
                {
                    flat_points.push_back(point.x());
                    flat_points.push_back(point.y());
                }
                segmentation.push_back(flat_points);
                area = polygonArea(points);
                if (area <= 0)
                    area = w * h;
            }

            json_data["annotations"].push_back({
                {          "id",       label.label_id},
                {    "image_id",       label.image_id},
                { "category_id", label.label_class_id},
                {        "bbox",         {x, y, w, h}},
                {        "area",                 area},
                {     "iscrowd",                    0},
                {"segmentation",         segmentation},
            });

            if ((i + 1) % std::max(1, label_count / 10) == 0 || i + 1 == label_count)
                updateProgress(45 + (i + 1) * 45 / std::max(1, label_count),
                               QString("已写入 COCO 标注 %1/%2").arg(i + 1).arg(label_count));
        }

        QFile annotation_file(QDir(annotations_dir).filePath(QStringLiteral("instances.json")));
        if (!annotation_file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        {
            emit exportFinished(false, QString("无法写入标注文件: %1").arg(annotation_file.fileName()));
            return;
        }

        annotation_file.write(QByteArray::fromStdString(json_data.dump(2)));
        updateProgress(100, QString("COCO 标注文件已写入"));
        emit exportFinished(
            true, QString("COCO 导出完成: %1 个图像, %2 个标注").arg(dataset.images.size()).arg(dataset.labels.size()));
    }
    catch (const std::exception &e)
    {
        spdlog::error("COCO 导出失败: {}", e.what());
        emit exportFinished(false, QString("COCO 导出失败: %1").arg(e.what()));
    }
}

// ============================================================================
// LabelMeIO
// ============================================================================

void LabelMeIO::startImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir)
{
    runInThread([this, dataset_id, image_dir, data_dir]() { doImport(dataset_id, image_dir, data_dir); });
}

void LabelMeIO::startScanLabelClasses(const QString &image_dir, const QString &data_dir)
{
    runInThread([this, image_dir, data_dir]() { doScanLabelClasses(image_dir, data_dir); });
}

void LabelMeIO::startExport(ExportDataset dataset, const QString &output_dir, const QVariantMap &options)
{
    Q_UNUSED(options)
    runInThread([this, dataset = std::move(dataset), output_dir]() { doExport(std::move(dataset), output_dir); });
}

bool LabelMeIO::parseLabelMeJson(const QString &json_path, LabelMeData &data)
{
    try
    {
        QFile file(json_path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            spdlog::error("无法打开 LabelMe JSON 文件: {}", json_path.toUtf8().constData());
            return false;
        }

        const QByteArray json_bytes = file.readAll();
        const auto       json_data
            = nlohmann::json::parse(json_bytes.constData(), json_bytes.constData() + json_bytes.size());

        data.image_path   = QString();
        data.image_width  = json_data.value("imageWidth", 0);
        data.image_height = json_data.value("imageHeight", 0);

        if (!json_data.contains("shapes") || !json_data["shapes"].is_array())
            return true;

        for (const auto &shape_json : json_data["shapes"])
        {
            if (!shape_json.contains("label") || !shape_json["label"].is_string() || !shape_json.contains("points")
                || !shape_json["points"].is_array())
                continue;

            LabelMeShape shape;
            shape.label      = QString::fromStdString(shape_json["label"].get<std::string>());
            shape.shape_type = QString::fromStdString(shape_json.value("shape_type", "polygon"));

            for (const auto &point_json : shape_json["points"])
            {
                if (!point_json.is_array() || point_json.size() < 2 || !point_json[0].is_number()
                    || !point_json[1].is_number())
                    continue;
                shape.points.emplace_back(point_json[0].get<double>(), point_json[1].get<double>());
            }

            if (!shape.points.empty())
                data.shapes.push_back(shape);
        }

        return true;
    }
    catch (const std::exception &e)
    {
        spdlog::error("解析 LabelMe JSON 失败: {}, 错误: {}", json_path.toUtf8().constData(), e.what());
        return false;
    }
}

QVariantMap LabelMeIO::convertShapeToLabelData(const LabelMeShape &shape, int image_width, int image_height,
                                               bool convert_rectangle_to_polygon)
{
    if (shape.shape_type == QStringLiteral("rectangle"))
    {
        if (shape.points.size() < 2)
        {
            spdlog::warn("rectangle 标注点数不足: {}", shape.points.size());
            return {};
        }

        const QPointF p1 = shape.points[0];
        const QPointF p2 = shape.points[1];
        if (target_method_ == DeepLearningMethod::Segmentation
            || target_method_ == DeepLearningMethod::AnomalyDetection)
        {
            if (!convert_rectangle_to_polygon)
                return {};

            const std::vector<QPointF> polygon    = rectangleToPolygon(p1, p2);
            const QVariantMap          label_data = DatasetIO::pointsToLabelData(polygon, image_width, image_height);
            if (label_data.isEmpty())
            {
                spdlog::warn("rectangle 标注无法转换为四点多边形: label={}", shape.label.toUtf8().constData());
                return {};
            }
            return label_data;
        }

        const double x_min = std::min(p1.x(), p2.x());
        const double y_min = std::min(p1.y(), p2.y());
        const double x_max = std::max(p1.x(), p2.x());
        const double y_max = std::max(p1.y(), p2.y());
        return DatasetIO::bboxToLabelData(x_min, y_min, x_max - x_min, y_max - y_min, image_width, image_height);
    }

    if (shape.shape_type == QStringLiteral("polygon"))
    {
        if (shape.points.empty())
        {
            spdlog::warn("polygon 标注没有坐标点");
            return {};
        }

        const QVariantMap label_data = DatasetIO::pointsToLabelData(shape.points, image_width, image_height);
        if (label_data.isEmpty())
        {
            spdlog::warn("polygon 标注点数不足或超出图像范围: {}", shape.points.size());
            return {};
        }
        return label_data;
    }

    spdlog::warn("不支持的 LabelMe shape_type: {}, label: {}", shape.shape_type.toUtf8().constData(),
                 shape.label.toUtf8().constData());
    return {};
}

void LabelMeIO::doScanLabelClasses(const QString &image_dir, const QString &data_dir)
{
    try
    {
        const QString annotation_dir = data_dir.trimmed();
        if (annotation_dir.isEmpty())
        {
            emit labelClassesScanned(true, {}, QStringLiteral("未提供 LabelMe 标注目录"));
            return;
        }

        updateProgress(0, QStringLiteral("正在扫描 LabelMe 类别..."));
        const std::vector<QString> json_files = DatasetIO::scanJsonFiles(annotation_dir);
        if (json_files.empty())
        {
            emit labelClassesScanned(true, {}, QStringLiteral("未找到 LabelMe 标注文件"));
            return;
        }

        std::set<QString> image_stems;
        if (!image_dir.trimmed().isEmpty())
        {
            for (const QString &image_path : DatasetIO::scanImageFiles(image_dir))
                image_stems.insert(QFileInfo(image_path).baseName());
        }

        std::map<QString, QString> label_class_info;
        int                        color_index     = 0;
        int                        processed_files = 0;
        int                        skipped_files   = 0;
        for (const QString &json_path : json_files)
        {
            if (isCancelRequested())
            {
                emit labelClassesScanned(false, {}, QStringLiteral("LabelMe 类别扫描已取消"));
                return;
            }

            ++processed_files;
            if (!image_stems.empty() && image_stems.find(QFileInfo(json_path).baseName()) == image_stems.end())
            {
                ++skipped_files;
                continue;
            }

            LabelMeData data;
            if (!parseLabelMeJson(json_path, data))
            {
                ++skipped_files;
                continue;
            }

            for (const LabelMeShape &shape : data.shapes)
                addScannedLabelClass(label_class_info, shape.label, color_index);

            if (processed_files % 500 == 0)
                updateProgress(50, QStringLiteral("已扫描 LabelMe 标注 %1").arg(processed_files));
        }

        updateProgress(100, QStringLiteral("LabelMe 类别扫描完成: %1 个类别，跳过 %2 个文件")
                                .arg(label_class_info.size())
                                .arg(skipped_files));
        emit labelClassesScanned(true, label_class_info, QString());
    }
    catch (const std::exception &e)
    {
        spdlog::error("LabelMe 类别扫描失败: {}", e.what());
        emit labelClassesScanned(false, {}, QString("LabelMe 类别扫描失败: %1").arg(e.what()));
    }
}

void LabelMeIO::doImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir)
{
    spdlog::info("开始解析 LabelMe 数据: dataset_id={}", dataset_id);

    try
    {
        const QString annotation_dir = data_dir.trimmed();
        updateProgress(0, QString("正在扫描图像文件..."));
        const QString              image_root_dir = QFileInfo(image_dir).absoluteFilePath();
        const std::vector<QString> image_files    = DatasetIO::scanImageFiles(image_root_dir);
        if (image_files.empty())
        {
            updateProgress(100, QString("未找到任何图像文件"));
            emit importFinished(false, {}, {});
            return;
        }

        std::map<QString, QString> annotation_files_by_relative_name;
        int                        total_json_files = 0;
        const QString              annotation_root_dir
            = annotation_dir.isEmpty() ? QString() : QFileInfo(annotation_dir).absoluteFilePath();
        if (!annotation_root_dir.isEmpty() && QFileInfo(annotation_root_dir).exists())
        {
            updateProgress(5, QString("正在扫描标注文件..."));
            const std::vector<QString> json_files = DatasetIO::scanJsonFiles(annotation_root_dir);
            total_json_files                      = static_cast<int>(json_files.size());
            for (const QString &json_path : json_files)
            {
                const QString relative = dltool::common::relativePath(annotation_root_dir, json_path);
                const QString key      = QFileInfo(relative).completeBaseName();
                auto          existing = annotation_files_by_relative_name.find(key);
                if (existing != annotation_files_by_relative_name.end())
                {
                    spdlog::warn("LabelMe 标注文件名冲突，后发现的将覆盖前者: {} vs {}",
                                 existing->second.toUtf8().constData(), json_path.toUtf8().constData());
                }
                annotation_files_by_relative_name[key] = json_path;
            }
        }

        const int total_images = static_cast<int>(image_files.size());

        std::vector<QString>       batch_image_paths;
        std::vector<int64_t>       batch_image_widths;
        std::vector<int64_t>       batch_image_heights;
        std::map<QString, QString> batch_label_class_info;
        std::vector<ImportedLabel> batch_labels;
        batch_image_paths.reserve(DataIO::ImportBatchImageCount);
        batch_image_widths.reserve(DataIO::ImportBatchImageCount);
        batch_image_heights.reserve(DataIO::ImportBatchImageCount);

        std::map<QString, QString> label_class_colors;
        int                        color_index         = 0;
        int                        processed_images    = 0;
        int                        valid_images        = 0;
        int                        skipped_images      = 0;
        int                        parsed_annotations  = 0;
        int                        skipped_annotations = 0;

        auto flush_batch = [&]() -> bool
        {
            if (batch_image_paths.empty() && batch_labels.empty())
                return true;

            emit dataBatchReady(dataset_id, std::move(batch_image_paths), std::move(batch_image_widths),
                                std::move(batch_image_heights), std::move(batch_label_class_info),
                                std::move(batch_labels), processed_images, total_images);

            batch_image_paths.clear();
            batch_image_widths.clear();
            batch_image_heights.clear();
            batch_label_class_info.clear();
            batch_labels.clear();
            batch_image_paths.reserve(DataIO::ImportBatchImageCount);
            batch_image_widths.reserve(DataIO::ImportBatchImageCount);
            batch_image_heights.reserve(DataIO::ImportBatchImageCount);
            return !isCancelRequested();
        };

        for (const QString &image_path : image_files)
        {
            if (isCancelRequested())
            {
                emit importFinished(false, {}, {});
                return;
            }

            ++processed_images;

            int width  = 0;
            int height = 0;
            if (!DatasetIO::getImageDimensions(image_path, width, height))
            {
                ++skipped_images;
                continue;
            }

            ++valid_images;
            batch_image_paths.push_back(image_path);
            batch_image_widths.push_back(width);
            batch_image_heights.push_back(height);

            const QString image_relative = dltool::common::relativePath(image_root_dir, image_path);
            const QString image_key      = QFileInfo(image_relative).completeBaseName();
            auto          json_it        = annotation_files_by_relative_name.find(image_key);
            if (json_it != annotation_files_by_relative_name.end())
            {
                LabelMeData data;
                if (parseLabelMeJson(json_it->second, data))
                {
                    data.image_path   = image_path;
                    data.image_width  = width;
                    data.image_height = height;
                    ++parsed_annotations;

                    const bool convert_rectangles_to_polygons
                        = (target_method_ == DeepLearningMethod::Segmentation
                           || target_method_ == DeepLearningMethod::AnomalyDetection)
                       && containsOnlyRectangleShapes(data);

                    for (const LabelMeShape &shape : data.shapes)
                    {
                        if (shape.label.isEmpty())
                            continue;
                        const QString label_class_name = sanitizeName(shape.label);
                        if (label_class_name.isEmpty())
                            continue;

                        const QVariantMap label_data
                            = convertShapeToLabelData(shape, width, height, convert_rectangles_to_polygons);
                        if (label_data.isEmpty())
                            continue;

                        auto color_it = label_class_colors.find(label_class_name);
                        if (color_it == label_class_colors.end())
                        {
                            const QString color = DatasetIO::generateDefaultColor(color_index++);
                            color_it            = label_class_colors.emplace(label_class_name, color).first;
                            batch_label_class_info[label_class_name] = color;
                        }

                        ImportedLabel imported_label;
                        imported_label.label_class_name = label_class_name;
                        imported_label.data             = label_data;
                        imported_label.image_path       = image_path;
                        batch_labels.push_back(imported_label);
                    }
                }
                else
                {
                    ++skipped_annotations;
                }
            }

            if (processed_images % std::max(1, total_images / 10) == 0 || processed_images == total_images)
            {
                const int progress = 10 + (processed_images * 80 / std::max(1, total_images));
                updateProgress(progress, QString("已处理 LabelMe 图像 %1/%2").arg(processed_images).arg(total_images));
            }

            if (batch_image_paths.size() >= DataIO::ImportBatchImageCount)
            {
                if (!flush_batch())
                {
                    emit importFinished(false, {}, {});
                    return;
                }
            }
        }

        if (!flush_batch())
        {
            emit importFinished(false, {}, {});
            return;
        }

        if (valid_images == 0)
        {
            updateProgress(100, QString("没有有效的图像可导入"));
            emit importFinished(false, {}, {});
            return;
        }

        updateProgress(100, QString("导入完成: %1 个图像, %2 个标注文件，跳过图像 %3 个，跳过标注 %4/%5")
                                .arg(valid_images)
                                .arg(parsed_annotations)
                                .arg(skipped_images)
                                .arg(skipped_annotations)
                                .arg(total_json_files));
        emit importFinished(true, {}, {});
    }
    catch (const std::exception &e)
    {
        spdlog::error("导入过程中发生异常: {}", e.what());
        updateProgress(100, QString("导入失败: %1").arg(e.what()));
        emit importFinished(false, {}, {});
    }
}

void LabelMeIO::doExport(ExportDataset dataset, QString output_dir)
{
    try
    {
        QString       err_msg;
        const QString images_dir      = QDir(output_dir).filePath(QStringLiteral("images"));
        const QString annotations_dir = QDir(output_dir).filePath(QStringLiteral("annotations"));
        if (!ensureDirectory(images_dir, err_msg) || !ensureDirectory(annotations_dir, err_msg))
        {
            emit exportFinished(false, err_msg);
            return;
        }

        std::map<QString, int>     used_image_names;
        std::map<QString, int>     used_image_stems;
        std::map<int64_t, QString> image_name_by_id;
        const int                  image_count = static_cast<int>(dataset.images.size());

        for (int i = 0; i < image_count; ++i)
        {
            const ExportImage &image = dataset.images[i];
            const QString file_name  = uniqueImageName(image.path, image.image_id, used_image_names, used_image_stems);
            used_image_names[file_name]++;
            used_image_stems[QFileInfo(file_name).completeBaseName()]++;
            image_name_by_id[image.image_id] = file_name;

            const QString target_path = QDir(images_dir).filePath(file_name);
            if (!DatasetIO::copyFile(image.path, target_path, err_msg))
            {
                emit exportFinished(false, err_msg);
                return;
            }

            if ((i + 1) % std::max(1, image_count / 10) == 0 || i + 1 == image_count)
                updateProgress((i + 1) * 45 / std::max(1, image_count),
                               QString("已复制图像 %1/%2").arg(i + 1).arg(image_count));
        }

        std::map<int64_t, QString> class_name_by_id;
        for (const ExportLabelClass &label_class : dataset.label_classes)
            class_name_by_id[label_class.id] = label_class.name;

        std::map<int64_t, std::vector<ExportLabel>> labels_by_image_id;
        for (const ExportLabel &label : dataset.labels) labels_by_image_id[label.image_id].push_back(label);

        for (int i = 0; i < image_count; ++i)
        {
            const ExportImage &image      = dataset.images[i];
            const QString      image_name = image_name_by_id[image.image_id];

            nlohmann::json json_data;
            json_data["version"]     = "5.0.1";
            json_data["flags"]       = nlohmann::json::object();
            json_data["shapes"]      = nlohmann::json::array();
            json_data["imagePath"]   = image_name.toStdString();
            json_data["imageData"]   = nullptr;
            json_data["imageHeight"] = image.height;
            json_data["imageWidth"]  = image.width;

            for (const ExportLabel &label : labels_by_image_id[image.image_id])
            {
                const double x = label.data.value(QStringLiteral("x")).toDouble();
                const double y = label.data.value(QStringLiteral("y")).toDouble();
                const double w = label.data.value(QStringLiteral("width")).toDouble();
                const double h = label.data.value(QStringLiteral("height")).toDouble();
                if (w <= 0 || h <= 0)
                    continue;

                nlohmann::json shape;
                shape["label"]       = class_name_by_id[label.label_class_id].toStdString();
                shape["group_id"]    = nullptr;
                shape["description"] = "";
                shape["flags"]       = nlohmann::json::object();

                const std::vector<QPointF> points
                    = DatasetIO::variantListToPoints(label.data.value(QStringLiteral("points")));
                if (points.size() >= 3)
                {
                    nlohmann::json point_array = nlohmann::json::array();
                    for (const QPointF &point : points) point_array.push_back({point.x(), point.y()});
                    shape["points"]     = point_array;
                    shape["shape_type"] = "polygon";
                }
                else
                {
                    shape["points"] = {
                        {    x,     y},
                        {x + w, y + h}
                    };
                    shape["shape_type"] = "rectangle";
                }
                json_data["shapes"].push_back(shape);
            }

            const QString annotation_name = QString("%1.json").arg(QFileInfo(image_name).completeBaseName());
            QFile         annotation_file(QDir(annotations_dir).filePath(annotation_name));
            if (!annotation_file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
            {
                emit exportFinished(false, QString("无法写入标注文件: %1").arg(annotation_file.fileName()));
                return;
            }

            annotation_file.write(QByteArray::fromStdString(json_data.dump(2)));

            const int progress = 45 + (i + 1) * 55 / std::max(1, image_count);
            if ((i + 1) % std::max(1, image_count / 10) == 0 || i + 1 == image_count)
                updateProgress(progress, QString("已写入 LabelMe 标注 %1/%2").arg(i + 1).arg(image_count));
        }

        emit exportFinished(
            true,
            QString("LabelMe 导出完成: %1 个图像, %2 个标注").arg(dataset.images.size()).arg(dataset.labels.size()));
    }
    catch (const std::exception &e)
    {
        spdlog::error("LabelMe 导出失败: {}", e.what());
        emit exportFinished(false, QString("LabelMe 导出失败: %1").arg(e.what()));
    }
}

// ============================================================================
// MaskIO
// ============================================================================

void MaskIO::startImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir)
{
    const double polygon_approx_epsilon_ratio = maskImportPolygonApproxRatio();
    runInThread([this, dataset_id, image_dir, data_dir, polygon_approx_epsilon_ratio]()
                { doImport(dataset_id, image_dir, data_dir, polygon_approx_epsilon_ratio); });
}

void MaskIO::startScanLabelClasses(const QString &image_dir, const QString &data_dir)
{
    Q_UNUSED(image_dir)
    runInThread([this, data_dir]() { doScanLabelClasses(data_dir); });
}

void MaskIO::startExport(ExportDataset dataset, const QString &output_dir, const QVariantMap &options)
{
    runInThread([this, dataset = std::move(dataset), output_dir, options]()
                { doExport(std::move(dataset), output_dir, options); });
}

void MaskIO::doScanLabelClasses(const QString &data_dir)
{
    try
    {
        const QString annotation_dir = data_dir.trimmed();
        if (annotation_dir.isEmpty())
        {
            emit labelClassesScanned(true, {}, QStringLiteral("未提供 Mask 标注目录"));
            return;
        }

        updateProgress(0, QStringLiteral("正在扫描 Mask 类别..."));
        const std::vector<QString> mask_files = scanMaskFiles(annotation_dir);
        if (mask_files.empty())
        {
            emit labelClassesScanned(true, {}, QStringLiteral("未找到 Mask 文件"));
            return;
        }

        std::map<QString, QString> label_class_info;
        int                        color_index = 0;
        int                        processed   = 0;
        for (const QString &mask_path : mask_files)
        {
            if (isCancelRequested())
            {
                emit labelClassesScanned(false, {}, QStringLiteral("Mask 类别扫描已取消"));
                return;
            }

            ++processed;
            addScannedLabelClass(label_class_info, labelClassNameForMask(mask_path, annotation_dir), color_index);
            if (processed % 1000 == 0)
                updateProgress(50, QStringLiteral("已扫描 Mask %1").arg(processed));
        }

        updateProgress(100, QStringLiteral("Mask 类别扫描完成: %1 个类别").arg(label_class_info.size()));
        emit labelClassesScanned(true, label_class_info, QString());
    }
    catch (const std::exception &e)
    {
        spdlog::error("Mask 类别扫描失败: {}", e.what());
        emit labelClassesScanned(false, {}, QString("Mask 类别扫描失败: %1").arg(e.what()));
    }
}

void MaskIO::doImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir,
                      const double polygon_approx_epsilon_ratio)
{
    try
    {
        const QString annotation_dir = data_dir.trimmed();
        if (annotation_dir.isEmpty())
        {
            importImagesOnly(dataset_id, image_dir, QStringLiteral("Mask"));
            return;
        }

        updateProgress(0, QString("正在扫描图像和 Mask..."));
        const std::map<QString, QString> image_by_stem = loadImageMap(image_dir);
        const std::vector<QString>       mask_files    = scanMaskFiles(annotation_dir);
        if (image_by_stem.empty() || mask_files.empty())
        {
            updateProgress(100, QString("图像目录或 Mask 目录为空"));
            emit importFinished(false, {}, {});
            return;
        }

        std::vector<QString>       batch_image_paths;
        std::vector<int64_t>       batch_image_widths;
        std::vector<int64_t>       batch_image_heights;
        std::map<QString, QString> batch_label_class_info;
        std::vector<ImportedLabel> batch_labels;
        std::map<QString, QString> label_class_colors;

        int processed_masks = 0;
        int valid_masks     = 0;
        int skipped_masks   = 0;

        auto flush_batch = [&]() -> bool
        {
            if (batch_image_paths.empty() && batch_labels.empty())
                return true;

            emit dataBatchReady(dataset_id, std::move(batch_image_paths), std::move(batch_image_widths),
                                std::move(batch_image_heights), std::move(batch_label_class_info),
                                std::move(batch_labels), processed_masks, static_cast<int64_t>(mask_files.size()));

            batch_image_paths.clear();
            batch_image_widths.clear();
            batch_image_heights.clear();
            batch_label_class_info.clear();
            batch_labels.clear();
            return !isCancelRequested();
        };

        for (const QString &mask_path : mask_files)
        {
            if (isCancelRequested())
            {
                emit importFinished(false, {}, {});
                return;
            }

            ++processed_masks;
            const QString mask_stem  = QFileInfo(mask_path).completeBaseName();
            const QString image_stem = imageStemForMask(mask_path, data_dir, mask_stem);
            const auto    image_it   = image_by_stem.find(image_stem);
            if (image_it == image_by_stem.end())
            {
                ++skipped_masks;
                spdlog::warn("Mask 未找到匹配图像，跳过: {}", mask_path.toUtf8().constData());
                continue;
            }

            int image_width  = 0;
            int image_height = 0;
            if (!DatasetIO::getImageDimensions(image_it->second, image_width, image_height))
            {
                ++skipped_masks;
                continue;
            }

            MaskGeometry geometry;
            if (!readMaskGeometry(mask_path, geometry, polygon_approx_epsilon_ratio))
            {
                ++skipped_masks;
                continue;
            }

            const QVariantMap label_data = maskToLabelData(geometry, image_width, image_height);
            if (label_data.isEmpty())
            {
                ++skipped_masks;
                continue;
            }

            const QString label_class_name = sanitizeName(labelClassNameForMask(mask_path, data_dir));
            if (label_class_name.isEmpty())
            {
                ++skipped_masks;
                continue;
            }

            if (label_class_colors.find(label_class_name) == label_class_colors.end())
                label_class_colors[label_class_name]
                    = DatasetIO::generateDefaultColor(static_cast<int>(label_class_colors.size()));
            batch_label_class_info[label_class_name] = label_class_colors[label_class_name];

            batch_image_paths.push_back(image_it->second);
            batch_image_widths.push_back(image_width);
            batch_image_heights.push_back(image_height);

            ImportedLabel imported_label;
            imported_label.label_class_name = label_class_name;
            imported_label.image_path       = image_it->second;
            imported_label.data             = label_data;
            batch_labels.push_back(imported_label);
            ++valid_masks;

            if (processed_masks % std::max<int>(1, static_cast<int>(mask_files.size()) / 10) == 0
                || processed_masks == static_cast<int>(mask_files.size()))
            {
                const int progress = 10 + processed_masks * 80 / std::max<int>(1, static_cast<int>(mask_files.size()));
                updateProgress(progress, QString("已处理 Mask %1/%2").arg(processed_masks).arg(mask_files.size()));
            }

            if (batch_image_paths.size() >= DataIO::ImportBatchImageCount)
            {
                if (!flush_batch())
                {
                    emit importFinished(false, {}, {});
                    return;
                }
            }
        }

        if (!flush_batch())
        {
            emit importFinished(false, {}, {});
            return;
        }

        updateProgress(100, QString("Mask 导入完成: 有效 %1 个，跳过 %2 个").arg(valid_masks).arg(skipped_masks));
        emit importFinished(valid_masks > 0, {}, {});
    }
    catch (const std::exception &e)
    {
        spdlog::error("Mask 导入失败: {}", e.what());
        updateProgress(100, QString("Mask 导入失败: %1").arg(e.what()));
        emit importFinished(false, {}, {});
    }
}

std::vector<QString> MaskIO::scanMaskFiles(const QString &mask_dir) const
{
    std::vector<QString> masks;
    QDir                 dir(mask_dir);
    if (!dir.exists())
        return masks;

    const QStringList filters{QStringLiteral("*.png"),  QStringLiteral("*.bmp"), QStringLiteral("*.tif"),
                              QStringLiteral("*.tiff"), QStringLiteral("*.PNG"), QStringLiteral("*.BMP"),
                              QStringLiteral("*.TIF"),  QStringLiteral("*.TIFF")};
    QDirIterator      it(mask_dir, filters, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) masks.push_back(it.next());
    return masks;
}

bool MaskIO::readMaskGeometry(const QString &mask_path, MaskGeometry &geometry,
                              const double polygon_approx_epsilon_ratio) const
{
    const QImage mask = QImage(mask_path).convertToFormat(QImage::Format_Grayscale8);
    if (mask.isNull())
    {
        spdlog::warn("无法读取 Mask: {}", mask_path.toUtf8().constData());
        return false;
    }

    geometry.mask_width  = mask.width();
    geometry.mask_height = mask.height();

    geometry.bbox = foregroundBoundingBox(mask);
    if (geometry.bbox.isNull() || geometry.bbox.isEmpty())
        return false;

    std::vector<uint8_t> binary_mask;
    binary_mask.reserve(static_cast<size_t>(mask.width()) * static_cast<size_t>(mask.height()));
    for (int y = 0; y < mask.height(); ++y)
    {
        const uchar *row = mask.constScanLine(y);
        for (int x = 0; x < mask.width(); ++x)
            binary_mask.push_back(row[x] >= kMaskThreshold ? uint8_t{1} : uint8_t{0});
    }

    std::vector<std::vector<QPointF>> polygons
        = dltool::common::maskToPolygons(binary_mask, mask.width(), mask.height(), false, polygon_approx_epsilon_ratio);
    if (!polygons.empty())
        geometry.polygon = std::move(polygons.front());
    else
        geometry.polygon.clear();

    return true;
}

QVariantMap MaskIO::maskToLabelData(const MaskGeometry &geometry, int image_width, int image_height) const
{
    if (geometry.mask_width <= 0 || geometry.mask_height <= 0 || image_width <= 0 || image_height <= 0)
        return {};

    const double sx = static_cast<double>(image_width) / geometry.mask_width;
    const double sy = static_cast<double>(image_height) / geometry.mask_height;

    if (target_method_ == DeepLearningMethod::Detection)
        return DatasetIO::bboxToLabelData(geometry.bbox.x() * sx, geometry.bbox.y() * sy, geometry.bbox.width() * sx,
                                          geometry.bbox.height() * sy, image_width, image_height);

    if (target_method_ == DeepLearningMethod::Segmentation || target_method_ == DeepLearningMethod::AnomalyDetection)
    {
        std::vector<QPointF> scaled_points;
        scaled_points.reserve(geometry.polygon.size());
        for (const QPointF &point : geometry.polygon) scaled_points.emplace_back(point.x() * sx, point.y() * sy);
        return DatasetIO::pointsToLabelData(scaled_points, image_width, image_height);
    }

    spdlog::warn("Mask 导入仅支持检测、分割和异常检测项目，当前项目类型: {}", target_method_);
    return {};
}

QString MaskIO::labelClassNameForMask(const QString &mask_path, const QString &mask_root) const
{
    QDir    root_dir(mask_root);
    QString rel_path = QDir::fromNativeSeparators(root_dir.relativeFilePath(QFileInfo(mask_path).absoluteFilePath()));
    const QStringList parts = rel_path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() > 1)
        return parts.first();
    return QFileInfo(mask_root).completeBaseName();
}

QString MaskIO::imageStemForMask(const QString &mask_path, const QString &mask_root, const QString &mask_stem) const
{
    const QFileInfo                  mask_info(mask_path);
    const std::map<QString, QString> local_map = loadQueryNameMap(mask_info.dir().absolutePath());
    const auto                       local_it  = local_map.find(mask_stem);
    if (local_it != local_map.end())
        return local_it->second;

    const std::map<QString, QString> root_map = loadQueryNameMap(mask_root);
    const auto                       root_it  = root_map.find(mask_stem);
    if (root_it != root_map.end())
        return root_it->second;

    return mask_stem;
}

std::map<QString, QString> MaskIO::loadQueryNameMap(const QString &dir_path) const
{
    std::map<QString, QString> result;
    QFile                      file(QDir(dir_path).filePath(QStringLiteral("query.txt")));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return result;

    QTextStream stream(&file);
    while (!stream.atEnd())
    {
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty())
            continue;

        const int comma = line.indexOf(QLatin1Char(','));
        if (comma < 0)
            continue;

        const QString id   = line.left(comma).trimmed();
        const QString name = line.mid(comma + 1).trimmed();
        if (!id.isEmpty() && !name.isEmpty())
            result[id] = name;
    }
    return result;
}

void MaskIO::doExport(ExportDataset dataset, QString output_dir, QVariantMap options)
{
    try
    {
        QString       err_msg;
        const QString images_dir = QDir(output_dir).filePath(QStringLiteral("images"));
        const QString masks_dir  = QDir(output_dir).filePath(QStringLiteral("masks"));
        if (!ensureDirectory(images_dir, err_msg) || !ensureDirectory(masks_dir, err_msg))
        {
            emit exportFinished(false, err_msg);
            return;
        }

        const MaskOutputMode mode = maskOutputModeFromOptions(options);
        if (mode == MaskOutputMode::ClassIndex && dataset.label_classes.size() > 255)
        {
            emit exportFinished(
                false, QString("Mask 按类别导出最多支持 255 个类别，当前 %1 个").arg(dataset.label_classes.size()));
            return;
        }

        std::map<int64_t, int> class_values;
        for (size_t i = 0; i < dataset.label_classes.size(); ++i)
            class_values[dataset.label_classes[i].id]
                = mode == MaskOutputMode::ClassIndex ? static_cast<int>(i) + 1 : 255;

        std::map<QString, int>     used_image_names;
        std::map<QString, int>     used_image_stems;
        std::map<int64_t, QString> image_name_by_id;
        const int                  image_count = static_cast<int>(dataset.images.size());

        for (int i = 0; i < image_count; ++i)
        {
            const ExportImage &image = dataset.images[i];
            const QString file_name  = uniqueImageName(image.path, image.image_id, used_image_names, used_image_stems);
            used_image_names[file_name]++;
            used_image_stems[QFileInfo(file_name).completeBaseName()]++;
            image_name_by_id[image.image_id] = file_name;

            const QString target_path = QDir(images_dir).filePath(file_name);
            if (!DatasetIO::copyFile(image.path, target_path, err_msg))
            {
                emit exportFinished(false, err_msg);
                return;
            }

            if ((i + 1) % std::max(1, image_count / 10) == 0 || i + 1 == image_count)
                updateProgress((i + 1) * 40 / std::max(1, image_count),
                               QString("已复制图像 %1/%2").arg(i + 1).arg(image_count));
        }

        std::map<int64_t, std::vector<ExportLabel>> labels_by_image_id;
        for (const ExportLabel &label : dataset.labels) labels_by_image_id[label.image_id].push_back(label);

        int written_label_count = 0;
        int skipped_label_count = 0;
        for (int i = 0; i < image_count; ++i)
        {
            const ExportImage &image  = dataset.images[i];
            int                width  = image.width;
            int                height = image.height;
            if ((width <= 0 || height <= 0) && !DatasetIO::getImageDimensions(image.path, width, height))
            {
                emit exportFinished(false, QString("无法读取图像尺寸，不能导出 Mask: %1").arg(image.path));
                return;
            }

            QImage mask(width, height, QImage::Format_ARGB32);
            mask.fill(Qt::black);

            for (const ExportLabel &label : labels_by_image_id[image.image_id])
            {
                const auto value_it = class_values.find(label.label_class_id);
                if (value_it == class_values.end())
                {
                    ++skipped_label_count;
                    continue;
                }

                if (paintLabelToMask(mask, label.data, value_it->second))
                    ++written_label_count;
                else
                    ++skipped_label_count;
            }

            const QString image_name = image_name_by_id[image.image_id];
            const QString mask_name  = QString("%1.png").arg(QFileInfo(image_name).completeBaseName());
            const QImage  gray_mask  = mask.convertToFormat(QImage::Format_Grayscale8);
            if (!gray_mask.save(QDir(masks_dir).filePath(mask_name), "PNG"))
            {
                emit exportFinished(false, QString("写入 Mask 失败: %1").arg(mask_name));
                return;
            }

            if ((i + 1) % std::max(1, image_count / 10) == 0 || i + 1 == image_count)
            {
                const int progress = 40 + (i + 1) * 55 / std::max(1, image_count);
                updateProgress(progress, QString("已写入 Mask %1/%2").arg(i + 1).arg(image_count));
            }
        }

        if (!writeClassMetadata(dataset, output_dir, mode, class_values, err_msg))
        {
            emit exportFinished(false, err_msg);
            return;
        }

        updateProgress(100, QString("Mask 类别映射文件已写入"));
        emit exportFinished(true, QString("Mask 导出完成: %1 个图像, %2 个标注, 跳过 %3 个标注")
                                      .arg(dataset.images.size())
                                      .arg(written_label_count)
                                      .arg(skipped_label_count));
    }
    catch (const std::exception &e)
    {
        spdlog::error("Mask 导出失败: {}", e.what());
        emit exportFinished(false, QString("Mask 导出失败: %1").arg(e.what()));
    }
}

// ============================================================================
// FolderIO
// ============================================================================

void FolderIO::startImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir)
{
    Q_UNUSED(data_dir)
    runInThread([this, dataset_id, image_dir]() { doImport(dataset_id, image_dir); });
}

void FolderIO::startScanLabelClasses(const QString &image_dir, const QString &data_dir)
{
    Q_UNUSED(data_dir)
    runInThread([this, image_dir]() { doScanLabelClasses(image_dir); });
}

void FolderIO::startExport(ExportDataset dataset, const QString &output_dir, const QVariantMap &options)
{
    Q_UNUSED(options)
    runInThread([this, dataset = std::move(dataset), output_dir]() { doExport(std::move(dataset), output_dir); });
}

void FolderIO::doScanLabelClasses(const QString &image_dir)
{
    try
    {
        updateProgress(0, QStringLiteral("正在扫描类别目录..."));

        const QDir root_dir(image_dir);
        if (!root_dir.exists())
        {
            emit labelClassesScanned(false, {}, QString("图像目录不存在: %1").arg(image_dir));
            return;
        }

        std::map<QString, QString>         label_class_info;
        int                                color_index = 0;
        const std::vector<FolderClassInfo> classes     = collectFolderClasses(root_dir);
        for (const FolderClassInfo &cls : classes)
        {
            if (isCancelRequested())
            {
                emit labelClassesScanned(false, {}, QStringLiteral("类别扫描已取消"));
                return;
            }

            addScannedLabelClass(label_class_info, cls.name, color_index);
        }

        if (label_class_info.empty())
        {
            emit labelClassesScanned(false, {}, QStringLiteral("未找到包含图像的目录"));
            return;
        }

        updateProgress(100, QStringLiteral("类别目录扫描完成: %1 个类别").arg(label_class_info.size()));
        emit labelClassesScanned(true, label_class_info, QString());
    }
    catch (const std::exception &e)
    {
        spdlog::error("类别目录扫描失败: {}", e.what());
        emit labelClassesScanned(false, {}, QString("类别目录扫描失败: %1").arg(e.what()));
    }
}

void FolderIO::doImport(int64_t dataset_id, const QString &image_dir)
{
    spdlog::info("开始解析文件夹分类数据: dataset_id={}, image_dir={}", dataset_id, image_dir.toUtf8().constData());

    try
    {
        updateProgress(0, QString("正在扫描类别目录..."));

        const QDir root_dir(image_dir);
        if (!root_dir.exists())
        {
            updateProgress(100, QString("图像目录不存在: %1").arg(image_dir));
            emit importFinished(false, {}, {});
            return;
        }

        std::vector<FolderClassInfo> classes;
        int                          total_images = 0;

        for (FolderClassInfo &cls : collectFolderClasses(root_dir))
        {
            if (cls.name.isEmpty())
            {
                spdlog::warn("类别名称无效，跳过");
                continue;
            }
            if (cls.image_paths.empty())
            {
                spdlog::warn("类别目录中无图像，跳过: {}", cls.name.toUtf8().constData());
                continue;
            }

            total_images += static_cast<int>(cls.image_paths.size());
            classes.push_back(std::move(cls));
        }

        if (classes.empty())
        {
            updateProgress(100, QString("没有有效的类别目录或根目录图像"));
            emit importFinished(false, {}, {});
            return;
        }

        std::vector<QString>       batch_image_paths;
        std::vector<int64_t>       batch_image_widths;
        std::vector<int64_t>       batch_image_heights;
        std::map<QString, QString> batch_label_class_info;
        std::vector<ImportedLabel> batch_labels;
        batch_image_paths.reserve(DataIO::ImportBatchImageCount);
        batch_image_widths.reserve(DataIO::ImportBatchImageCount);
        batch_image_heights.reserve(DataIO::ImportBatchImageCount);

        int                        color_index      = 0;
        int                        processed_images = 0;
        int                        valid_images     = 0;
        int                        skipped_images   = 0;
        std::map<QString, QString> class_colors;

        auto flush_batch = [&]() -> bool
        {
            if (batch_image_paths.empty() && batch_labels.empty())
                return true;

            emit dataBatchReady(dataset_id, std::move(batch_image_paths), std::move(batch_image_widths),
                                std::move(batch_image_heights), std::move(batch_label_class_info),
                                std::move(batch_labels), processed_images, total_images);

            batch_image_paths.clear();
            batch_image_widths.clear();
            batch_image_heights.clear();
            batch_label_class_info.clear();
            batch_labels.clear();
            batch_image_paths.reserve(DataIO::ImportBatchImageCount);
            batch_image_widths.reserve(DataIO::ImportBatchImageCount);
            batch_image_heights.reserve(DataIO::ImportBatchImageCount);
            return !isCancelRequested();
        };

        for (const FolderClassInfo &cls : classes)
        {
            if (class_colors.find(cls.name) == class_colors.end())
            {
                class_colors[cls.name]           = DatasetIO::generateDefaultColor(color_index++);
                batch_label_class_info[cls.name] = class_colors[cls.name];
            }

            for (const QString &image_path : cls.image_paths)
            {
                if (isCancelRequested())
                {
                    emit importFinished(false, {}, {});
                    return;
                }

                ++processed_images;

                int width  = 0;
                int height = 0;
                if (!DatasetIO::getImageDimensions(image_path, width, height))
                {
                    ++skipped_images;
                    continue;
                }

                ++valid_images;
                batch_image_paths.push_back(image_path);
                batch_image_widths.push_back(width);
                batch_image_heights.push_back(height);

                ImportedLabel label;
                label.label_class_name = cls.name;
                label.data             = DatasetIO::bboxToLabelData(0, 0, width, height, width, height);
                label.image_path       = image_path;
                batch_labels.push_back(label);

                if (processed_images % std::max(1, total_images / 10) == 0 || processed_images == total_images)
                {
                    const int progress = 10 + (processed_images * 80 / std::max(1, total_images));
                    updateProgress(progress, QString("已处理文件夹图像 %1/%2").arg(processed_images).arg(total_images));
                }

                if (batch_image_paths.size() >= DataIO::ImportBatchImageCount)
                {
                    if (!flush_batch())
                    {
                        emit importFinished(false, {}, {});
                        return;
                    }
                }
            }
        }

        if (!flush_batch())
        {
            emit importFinished(false, {}, {});
            return;
        }

        if (valid_images == 0)
        {
            updateProgress(100, QString("没有有效的图像可导入"));
            emit importFinished(false, {}, {});
            return;
        }

        updateProgress(100, QString("文件夹导入完成: %1 个图像, %2 个类别，跳过图像 %3 个")
                                .arg(valid_images)
                                .arg(classes.size())
                                .arg(skipped_images));
        emit importFinished(true, {}, {});
    }
    catch (const std::exception &e)
    {
        spdlog::error("文件夹导入异常: {}", e.what());
        updateProgress(100, QString("导入失败: %1").arg(e.what()));
        emit importFinished(false, {}, {});
    }
}

void FolderIO::doExport(ExportDataset dataset, QString output_dir)
{
    try
    {
        QString err_msg;
        if (!ensureDirectory(output_dir, err_msg))
        {
            emit exportFinished(false, err_msg);
            return;
        }

        std::map<int64_t, QString> class_name_by_id;
        for (const ExportLabelClass &label_class : dataset.label_classes)
            class_name_by_id[label_class.id] = label_class.name;

        std::map<int64_t, std::vector<ExportLabel>> labels_by_image;
        for (const ExportLabel &label : dataset.labels) labels_by_image[label.image_id].push_back(label);

        const int image_count = static_cast<int>(dataset.images.size());
        int       exported    = 0;

        for (int i = 0; i < image_count; ++i)
        {
            const ExportImage &image      = dataset.images[i];
            const auto         label_it   = labels_by_image.find(image.image_id);
            QString            class_name = "unknown";

            if (label_it != labels_by_image.end() && !label_it->second.empty())
            {
                const auto name_it = class_name_by_id.find(label_it->second[0].label_class_id);
                if (name_it != class_name_by_id.end())
                    class_name = name_it->second;
            }

            const QString class_dir = QDir(output_dir).filePath(class_name);
            if (!ensureDirectory(class_dir, err_msg))
            {
                emit exportFinished(false, err_msg);
                return;
            }

            const QString file_name   = QFileInfo(image.path).fileName();
            const QString target_path = QDir(class_dir).filePath(file_name);
            if (!DatasetIO::copyFile(image.path, target_path, err_msg))
            {
                emit exportFinished(false, err_msg);
                return;
            }

            ++exported;

            if ((i + 1) % std::max(1, image_count / 10) == 0 || i + 1 == image_count)
                updateProgress((i + 1) * 100 / std::max(1, image_count),
                               QString("已导出图像 %1/%2").arg(i + 1).arg(image_count));
        }

        emit exportFinished(true, QString("文件夹导出完成: %1 个图像").arg(exported));
    }
    catch (const std::exception &e)
    {
        spdlog::error("文件夹导出失败: {}", e.what());
        emit exportFinished(false, QString("文件夹导出失败: %1").arg(e.what()));
    }
}

} // namespace dltool::data
