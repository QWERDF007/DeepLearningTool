#include "data/DataIO.h"
#include "DataIOInternal.h"
#include "ExportPipeline.h"
#include "data/ParallelFor.h"

#include "common/GeometryKernel.h"
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
#include <mutex>
#include <set>
#include <thread>
#include <type_traits>
#include <utility>

using dltool::common::ensureDirectory;
using dltool::core::DeepLearningMethod;


namespace dltool::data {

namespace {

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
              { return dltool::common::polygonArea(left) > dltool::common::polygonArea(right); });
    return polygons;
}

} // namespace

// ============================================================================
// COCOIO
// ============================================================================

void COCOIO::startImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir)
{
    const double polygon_approx_epsilon_ratio = detail::maskImportPolygonApproxRatio();
    const int    thread_count                 = detail::dataIOThreadCount();
    runInThread([this, dataset_id, image_dir, data_dir, polygon_approx_epsilon_ratio, thread_count]()
                { doImport(dataset_id, image_dir, data_dir, polygon_approx_epsilon_ratio, thread_count); },
                [this](const QString &) { emit importFinished(false, {}, {}); });
}

void COCOIO::startScanLabelClasses(const QString &image_dir, const QString &data_dir)
{
    Q_UNUSED(image_dir)
    runInThread([this, data_dir]() { doScanLabelClasses(data_dir); },
                [this](const QString &error) { emit labelClassesScanned(false, {}, error); });
}

void COCOIO::startExport(ExportDataset dataset, const QString &output_dir, const QVariantMap &options)
{
    Q_UNUSED(options)
    const int thread_count = detail::dataIOThreadCount();
    runInThread([this, dataset = std::move(dataset), output_dir, thread_count]()
                { doExport(std::move(dataset), output_dir, thread_count); },
                [this](const QString &error) { emit exportFinished(false, error); });
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
            emit labelClassesScanned(true, {}, QString("未提供 COCO 标注目录"));
            return;
        }

        updateProgress(0, QString("正在扫描 COCO 类别..."));
        const QString coco_json_path = findCocoJsonFile(annotation_dir);
        if (coco_json_path.isEmpty())
        {
            emit labelClassesScanned(false, {}, QString("未找到有效的 COCO 标注文件"));
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
                    detail::addScannedLabelClass(label_class_info,
                                                                  QString::fromStdString(object_json["name"].get<std::string>()), color_index);
                return true;
            });

        if (!ok || isCancelRequested())
        {
            emit labelClassesScanned(false, {}, QString("COCO 类别扫描已取消或失败"));
            return;
        }

        updateProgress(100, QString("COCO 类别扫描完成: %1 个类别").arg(label_class_info.size()));
        emit labelClassesScanned(true, label_class_info, QString());
    }
    catch (const std::exception &e)
    {
        spdlog::error("COCO 类别扫描失败: {}", e.what());
        emit labelClassesScanned(false, {}, QString("COCO 类别扫描失败: %1").arg(e.what()));
    }
}

void COCOIO::doImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir,
                      const double polygon_approx_epsilon_ratio, const int thread_count)
{
    spdlog::info("开始解析 COCO 数据: dataset_id={}", dataset_id);

    try
    {
        const QString annotation_dir = data_dir.trimmed();
        if (annotation_dir.isEmpty())
        {
            importImagesOnly(dataset_id, image_dir, QStringLiteral("COCO"), thread_count);
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

        std::vector<uint8_t> valid_image_flags(images.size(), 1);
        parallelFor(images.size(), thread_count, cancel_requested_,
                    [&](const std::size_t index)
                    {
                        CocoImage &image = images[index];
                        if (image.width > 0 && image.height > 0)
                            return;

                        if (!DatasetIO::getImageDimensions(image.image_path, image.width, image.height))
                            valid_image_flags[index] = 0;
                    },
                    [this](const std::size_t completed, const std::size_t total)
                    {
                        const int progress = 20 + static_cast<int>(completed * 20 / std::max<std::size_t>(1, total));
                        updateProgress(progress, QString("已校验 COCO 图像 %1/%2").arg(completed).arg(total));
                    });

        std::vector<CocoImage> valid_images;
        valid_images.reserve(images.size());
        for (std::size_t index = 0; index < images.size(); ++index)
        {
            if (!valid_image_flags[index])
            {
                ++skipped_images;
                continue;
            }
            valid_images.push_back(std::move(images[index]));
        }
        images = std::move(valid_images);
        for (const CocoImage &image : images) images_by_coco_id[image.coco_id] = image;

        if (images.empty())
        {
            updateProgress(100, QString("COCO 数据中没有可导入的有效图像"));
            emit importFinished(false, {}, {});
            return;
        }

        ImportBatchEmitter emitter(*this, dataset_id);
        for (const auto &[class_name, class_color] : label_class_info) emitter.pushLabelClass(class_name, class_color);

        updateProgress(40, QString("正在分批写入 COCO 图像..."));
        int64_t pushed_images = 0;
        for (const CocoImage &image : images)
        {
            emitter.pushImage(image.image_path, image.width, image.height);
            ++pushed_images;

            if (!emitter.flushIfFullByImages(pushed_images, static_cast<int64_t>(images.size())))
            {
                emit importFinished(false, {}, {});
                return;
            }
        }

        if (!emitter.flush(pushed_images, static_cast<int64_t>(images.size())))
        {
            emit importFinished(false, {}, {});
            return;
        }

        updateProgress(60, QString("正在流式解析 COCO 标注..."));
        int processed_annotations = 0;
        int skipped_annotations   = 0;
        int imported_label_count  = 0;

        struct CocoAnnotationResult
        {
            int                        skipped{0};
            std::vector<ImportedLabel> labels;
        };

        std::vector<nlohmann::json> annotation_batch;
        annotation_batch.reserve(DataIO::ImportBatchImageCount);
        const auto &images_by_coco_id_readonly = images_by_coco_id;
        const auto &categories_by_id_readonly  = categories_by_id;
        auto        process_annotation_batch   = [&](const std::vector<nlohmann::json> &batch) -> bool
        {
            std::vector<CocoAnnotationResult> results(batch.size());
            const int                         target_method = target_method_;
            parallelFor(
                batch.size(), thread_count, cancel_requested_,
                [&](const std::size_t index)
                {
                    const auto &annotation_json = batch[index];
                    auto       &result          = results[index];
                    if (!annotation_json.contains("image_id") || !annotation_json.contains("category_id"))
                    {
                        result.skipped = 1;
                        return;
                    }

                    int64_t coco_image_id = 0;
                    int64_t category_id   = 0;
                    if (!jsonToInt64(annotation_json["image_id"], coco_image_id)
                        || !jsonToInt64(annotation_json["category_id"], category_id))
                    {
                        result.skipped = 1;
                        return;
                    }

                    const auto image_it = images_by_coco_id_readonly.find(coco_image_id);
                    const auto class_it = categories_by_id_readonly.find(category_id);
                    if (image_it == images_by_coco_id_readonly.end() || class_it == categories_by_id_readonly.end())
                    {
                        result.skipped = 1;
                        return;
                    }

                    const bool               annotation_has_seg = hasSegmentationData(annotation_json);
                    std::vector<QVariantMap> label_data_list;
                    if ((target_method == DeepLearningMethod::Segmentation
                         || target_method == DeepLearningMethod::AnomalyDetection)
                        && annotation_has_seg)
                    {
                        const auto segmentation_polygons
                            = parseSegmentationPolygons(annotation_json, polygon_approx_epsilon_ratio);
                        for (const auto &polygon : segmentation_polygons)
                        {
                            QVariantMap label_data = DatasetIO::pointsToLabelData(polygon, image_it->second.width,
                                                                                  image_it->second.height);
                            if (!label_data.isEmpty())
                                label_data_list.push_back(std::move(label_data));
                        }
                    }

                    if (label_data_list.empty())
                    {
                        if ((target_method == DeepLearningMethod::Segmentation
                             || target_method == DeepLearningMethod::AnomalyDetection)
                            && annotation_has_seg)
                        {
                            result.skipped = 1;
                            return;
                        }

                        if (!annotation_json.contains("bbox") || !annotation_json["bbox"].is_array()
                            || annotation_json["bbox"].size() < 4)
                        {
                            result.skipped = 1;
                            return;
                        }

                        const auto &bbox       = annotation_json["bbox"];
                        QVariantMap label_data;
                        if (target_method == DeepLearningMethod::Segmentation
                            || target_method == DeepLearningMethod::AnomalyDetection)
                        {
                            const double bx = jsonToDouble(bbox[0]);
                            const double by = jsonToDouble(bbox[1]);
                            const double bw = jsonToDouble(bbox[2]);
                            const double bh = jsonToDouble(bbox[3]);
                            const std::vector<QPointF> rect_poly
                                = dltool::common::geometry::rectangleToPolygon(QPointF(bx, by), QPointF(bx + bw, by + bh));
                            label_data = DatasetIO::pointsToLabelData(rect_poly, image_it->second.width, image_it->second.height);
                        }
                        else
                        {
                            label_data = DatasetIO::bboxToLabelData(
                                jsonToDouble(bbox[0]), jsonToDouble(bbox[1]), jsonToDouble(bbox[2]), jsonToDouble(bbox[3]),
                                image_it->second.width, image_it->second.height);
                        }
                        if (!label_data.isEmpty())
                            label_data_list.push_back(std::move(label_data));
                    }

                    if (label_data_list.empty())
                    {
                        result.skipped = 1;
                        return;
                    }

                    result.labels.reserve(label_data_list.size());
                    for (auto &label_data : label_data_list)
                    {
                        ImportedLabel label;
                        label.label_class_name = class_it->second.name;
                        label.data             = std::move(label_data);
                        label.image_path       = image_it->second.image_path;
                        result.labels.push_back(std::move(label));
                    }
                });

            for (auto &result : results)
            {
                skipped_annotations += result.skipped;
                imported_label_count += static_cast<int>(result.labels.size());
                for (auto &label : result.labels) emitter.pushLabel(std::move(label));
                if (!emitter.flushIfFullByLabels(static_cast<int64_t>(images.size()),
                                                 static_cast<int64_t>(images.size())))
                    return false;
            }
            return !isCancelRequested();
        };

        const bool pass2_ok = parseTopLevelArrayObjects(
            coco_json_path,
            [&](const QString &array_key, const nlohmann::json &annotation_json) -> bool
            {
                if (isCancelRequested())
                    return false;

                if (array_key != QStringLiteral("annotations"))
                    return true;

                ++processed_annotations;
                annotation_batch.push_back(annotation_json);
                if (annotation_batch.size() >= DataIO::ImportBatchImageCount)
                {
                    if (!process_annotation_batch(annotation_batch))
                        return false;
                    annotation_batch.clear();
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

        if (!annotation_batch.empty() && !process_annotation_batch(annotation_batch))
        {
            emit importFinished(false, {}, {});
            return;
        }

        if (!emitter.flush(static_cast<int64_t>(images.size()), static_cast<int64_t>(images.size())))
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

void COCOIO::doExport(ExportDataset dataset, QString output_dir, const int thread_count)
{
    const auto result = detail::runExportPipeline(
        DataFormat::COCO, dataset, output_dir, {}, QStringLiteral("COCO"),
        [this, &dataset, thread_count](const QString &staging_dir, QString &error, QString &summary,
                                       QString &done_hint) -> bool
        {
            const QString images_dir      = QDir(staging_dir).filePath(QStringLiteral("images"));
            const QString annotations_dir = QDir(staging_dir).filePath(QStringLiteral("annotations"));
            if (!ensureDirectory(images_dir, error) || !ensureDirectory(annotations_dir, error))
            {
                return false;
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
            const auto                &image_name_by_id_readonly = image_name_by_id;
            const int                  image_count               = static_cast<int>(dataset.images.size());

            for (int i = 0; i < image_count; ++i)
            {
                const ExportImage &image     = dataset.images[i];
                const QString      file_name = DatasetIO::uniqueFileName(image.path, image.image_id, used_image_names);
                used_image_names[file_name]++;
                image_name_by_id[image.image_id] = file_name;
            }

            struct CocoImageExportResult
            {
                bool           success{true};
                QString        error;
                nlohmann::json image_json;
            };

            if (isCancelRequested())
            {
                error = QStringLiteral("导出已取消");
                return false;
            }

            std::vector<CocoImageExportResult> image_results(image_count);
            parallelFor(static_cast<std::size_t>(image_count), thread_count, cancel_requested_,
                        [&](const std::size_t index)
                        {
                            const ExportImage &image     = dataset.images[index];
                            const QString      file_name = image_name_by_id_readonly.at(image.image_id);
                            QString            task_error;
                            if (!DatasetIO::copyFile(image.path, QDir(images_dir).filePath(file_name), task_error))
                            {
                                image_results[index].success = false;
                                image_results[index].error   = task_error;
                                return;
                            }

                            image_results[index].image_json = {
                                {       "id",          image.image_id},
                                {"file_name", file_name.toStdString()},
                                {    "width",             image.width},
                                {   "height",            image.height},
                                {  "license",                       0},
                            };
                        },
                        [this](const std::size_t completed, const std::size_t total)
                        {
                            updateProgress(5 + static_cast<int>(completed * 40 / std::max<std::size_t>(1, total)),
                                           QString("已复制 COCO 图像 %1/%2").arg(completed).arg(total));
                        });

            if (isCancelRequested())
            {
                error = QStringLiteral("导出已取消");
                return false;
            }

            for (int i = 0; i < image_count; ++i)
            {
                if (!image_results[i].success)
                {
                    error = image_results[i].error;
                    return false;
                }
                json_data["images"].push_back(std::move(image_results[i].image_json));
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

            struct CocoAnnotationExportResult
            {
                bool           valid{false};
                nlohmann::json annotation_json;
            };

            std::vector<CocoAnnotationExportResult> annotation_results(label_count);
            parallelFor(static_cast<std::size_t>(label_count), thread_count, cancel_requested_,
                        [&](const std::size_t index)
                        {
                            const ExportLabel &label = dataset.labels[index];
                            const double       x     = label.data.value(QStringLiteral("x")).toDouble();
                            const double       y     = label.data.value(QStringLiteral("y")).toDouble();
                            const double       w     = label.data.value(QStringLiteral("width")).toDouble();
                            const double       h     = label.data.value(QStringLiteral("height")).toDouble();
                            if (w <= 0 || h <= 0)
                                return;

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
                                area = dltool::common::polygonArea(points);
                                if (area <= 0)
                                    area = w * h;
                            }
                            else if ((target_method_ == DeepLearningMethod::Segmentation
                                      || target_method_ == DeepLearningMethod::AnomalyDetection)
                                     && w > 0.0 && h > 0.0)
                            {
                                const std::vector<QPointF> rect_poly
                                    = dltool::common::geometry::rectangleToPolygon(QPointF(x, y), QPointF(x + w, y + h));
                                if (rect_poly.size() >= 3)
                                {
                                    nlohmann::json flat_points = nlohmann::json::array();
                                    for (const QPointF &point : rect_poly)
                                    {
                                        flat_points.push_back(point.x());
                                        flat_points.push_back(point.y());
                                    }
                                    segmentation.push_back(flat_points);
                                }
                            }

                            annotation_results[index].annotation_json = {
                                {          "id",       label.label_id},
                                {    "image_id",       label.image_id},
                                { "category_id", label.label_class_id},
                                {        "bbox",         {x, y, w, h}},
                                {        "area",                 area},
                                {     "iscrowd",                    0},
                                {"segmentation",         segmentation},
                            };
                            annotation_results[index].valid = true;
                        },
                        [this](const std::size_t completed, const std::size_t total)
                        {
                            updateProgress(50 + static_cast<int>(completed * 40 / std::max<std::size_t>(1, total)),
                                           QString("已生成 COCO 标注 %1/%2").arg(completed).arg(total));
                        });

            if (isCancelRequested())
            {
                error = QStringLiteral("导出已取消");
                return false;
            }

            for (int i = 0; i < label_count; ++i)
            {
                if (annotation_results[i].valid)
                    json_data["annotations"].push_back(std::move(annotation_results[i].annotation_json));
            }

            QFile annotation_file(QDir(annotations_dir).filePath(QStringLiteral("instances.json")));
            if (!annotation_file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
            {
                error = QString("无法写入标注文件: %1").arg(annotation_file.fileName());
                return false;
            }

            annotation_file.write(QByteArray::fromStdString(json_data.dump(2)));
            annotation_file.close();
            summary   = QString("COCO 导出完成: %1 个图像, %2 个标注")
                            .arg(dataset.images.size())
                            .arg(dataset.labels.size());
            done_hint = QStringLiteral("COCO 标注文件已写入");
            return true;
        });

    if (result.success)
        updateProgress(100, result.done_hint);
    emit exportFinished(result.success, result.message);
}

} // namespace dltool::data
