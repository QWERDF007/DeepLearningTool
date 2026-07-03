#include "data/COCOImporter.h"

#include "common/MaskPolygonUtils.h"
#include "core/CoreDef.h"
#include "data/DataNameUtils.h"
#include "database/DataBase.h"

#include <json.hpp>
#include <spdlog/spdlog.h>

#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <unordered_map>
#include <utility>

namespace dltool::data {

using dltool::core::DeepLearningMethod;

namespace {

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

bool parseTopLevelArrayObjects(
    const QString                                                                     &json_path,
    const std::function<bool(const QString &array_key, const nlohmann::json &object)> &on_object)
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
            {
                throw ImportCancelled();
            }
            return false;
        }

        if (event == nlohmann::json::parse_event_t::array_end && depth == 1)
        {
            active_array_key.clear();
            return false;
        }

        if (event == nlohmann::json::parse_event_t::object_end && depth == 1)
        {
            return false;
        }

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
        {
            return false;
        }

        if (event == nlohmann::json::parse_event_t::array_end && depth == 1)
        {
            active_array_key.clear();
            return false;
        }

        if (event == nlohmann::json::parse_event_t::object_end && depth == 1)
        {
            return false;
        }

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
    {
        return points;
    }

    points.reserve(polygon_json.size() / 2);
    for (size_t i = 0; i + 1 < polygon_json.size(); i += 2)
    {
        if (polygon_json[i].is_number() && polygon_json[i + 1].is_number())
        {
            points.emplace_back(polygon_json[i].get<double>(), polygon_json[i + 1].get<double>());
        }
    }

    return points.size() >= 3 ? points : std::vector<QPointF>();
}

double polygonArea(const std::vector<QPointF> &points)
{
    if (points.size() < 3)
    {
        return 0.0;
    }

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
    {
        return false;
    }

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
            {
                value |= -1LL << (5 * shift);
            }
        }
        while (more && pos < counts.size());

        if (decoded.size() > 2)
        {
            value += decoded[decoded.size() - 2];
        }
        if (value < 0)
        {
            return {};
        }
        decoded.push_back(value);
    }

    return decoded;
}

bool decodeRleMask(const nlohmann::json &segmentation, std::vector<uint8_t> &mask, int &width, int &height)
{
    if (!segmentation.is_object() || !readRleSize(segmentation, height, width) || !segmentation.contains("counts"))
    {
        return false;
    }

    std::vector<int64_t> counts;
    const auto          &counts_json = segmentation["counts"];
    if (counts_json.is_array())
    {
        counts.reserve(counts_json.size());
        for (const auto &count_json : counts_json)
        {
            if (!count_json.is_number_integer() && !count_json.is_number_unsigned())
            {
                return false;
            }
            const int64_t count = count_json.get<int64_t>();
            if (count < 0)
            {
                return false;
            }
            counts.push_back(count);
        }
    }
    else if (counts_json.is_string())
    {
        counts = decodeCompressedRleCounts(counts_json.get<std::string>());
        if (counts.empty())
        {
            return false;
        }
    }
    else
    {
        return false;
    }

    const int64_t total = static_cast<int64_t>(width) * static_cast<int64_t>(height);
    if (total <= 0)
    {
        return false;
    }

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
        {
            break;
        }
    }

    return has_foreground;
}

bool hasSegmentationData(const nlohmann::json &annotation_json)
{
    if (!annotation_json.contains("segmentation"))
    {
        return false;
    }

    const auto &segmentation = annotation_json["segmentation"];
    if (segmentation.is_array())
    {
        return !segmentation.empty();
    }
    if (segmentation.is_object())
    {
        return segmentation.contains("counts") && segmentation.contains("size");
    }
    return false;
}

std::vector<std::vector<QPointF>> parseSegmentationPolygons(const nlohmann::json &annotation_json)
{
    if (!hasSegmentationData(annotation_json))
    {
        return {};
    }

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
                {
                    polygons.push_back(std::move(points));
                }
            }
        }
        else
        {
            std::vector<QPointF> points = jsonArrayToPolygon(segmentation);
            if (!points.empty())
            {
                polygons.push_back(std::move(points));
            }
        }
    }
    else if (segmentation.is_object())
    {
        std::vector<uint8_t> mask;
        int                  width  = 0;
        int                  height = 0;
        if (decodeRleMask(segmentation, mask, width, height))
        {
            polygons = dltool::common::maskToPolygons(mask, width, height, false);
        }
    }

    std::sort(polygons.begin(), polygons.end(), [](const std::vector<QPointF> &left, const std::vector<QPointF> &right)
              { return polygonArea(left) > polygonArea(right); });
    return polygons;
}

} // namespace

COCOImporter::COCOImporter(dltool::database::ProjectDataBase *database, QObject *parent)
    : DataImporter(database, parent)
{
}

COCOImporter::~COCOImporter() {}

void COCOImporter::startImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir)
{
    QThread *worker_thread = new QThread();

    connect(
        worker_thread, &QThread::started, this,
        [this, dataset_id, image_dir, data_dir]() { doImport(dataset_id, image_dir, data_dir); }, Qt::DirectConnection);

    connect(this, &COCOImporter::importFinished, worker_thread, &QThread::quit);
    connect(worker_thread, &QThread::finished, worker_thread, &QThread::deleteLater);

    worker_thread->start();
}

QString COCOImporter::findCocoJsonFile(const QString &data_path) const
{
    const std::vector<QString> json_files = DatasetIO::scanJsonFiles(data_path);
    for (const QString &json_path : json_files)
    {
        if (looksLikeCocoJson(json_path))
        {
            return json_path;
        }
    }

    return QString();
}

bool COCOImporter::looksLikeCocoJson(const QString &json_path) const
{
    bool has_images      = false;
    bool has_annotations = false;
    bool has_categories  = false;
    if (!parseTopLevelKeys(json_path, has_images, has_annotations, has_categories))
    {
        return false;
    }

    return has_images && has_annotations && has_categories;
}

QString COCOImporter::resolveImagePath(const QString &image_dir, const QString &file_name,
                                       const std::map<QString, QString> &image_file_index) const
{
    QDir    root_dir(image_dir);
    QString direct_path = root_dir.filePath(file_name);
    direct_path         = QDir::cleanPath(direct_path);
    if (QFileInfo::exists(direct_path))
    {
        return QFileInfo(direct_path).absoluteFilePath();
    }

    const QString basename = QFileInfo(file_name).fileName();
    auto          found    = image_file_index.find(basename);
    if (found != image_file_index.end())
    {
        return found->second;
    }

    return QString();
}

void COCOImporter::doImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir)
{
    spdlog::info("开始解析 COCO 数据: dataset_id={}", dataset_id);

    try
    {
        updateProgress(0, QString("正在查找 COCO 标注文件..."));
        const QString coco_json_path = findCocoJsonFile(data_dir);
        if (coco_json_path.isEmpty())
        {
            updateProgress(100, QString("未找到有效的 COCO 标注文件"));
            emit importFinished(false, {}, {});
            return;
        }

        updateProgress(10, QString("正在索引图像文件..."));
        std::map<QString, QString> image_file_index;
        for (const QString &image_path : DatasetIO::scanImageFiles(image_dir))
        {
            image_file_index[QFileInfo(image_path).fileName()] = image_path;
        }

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
                {
                    return false;
                }

                if (array_key == QStringLiteral("categories"))
                {
                    if (!object_json.contains("id") || !object_json.contains("name")
                        || !object_json["name"].is_string())
                    {
                        return true;
                    }

                    int64_t category_id = 0;
                    if (!jsonToInt64(object_json["id"], category_id))
                    {
                        return true;
                    }

                    CocoCategory category;
                    category.id   = category_id;
                    category.name = sanitizeName(QString::fromStdString(object_json["name"].get<std::string>()));
                    if (category.name.isEmpty())
                    {
                        return true;
                    }

                    categories_by_id[category.id] = category;
                    if (label_class_info.find(category.name) == label_class_info.end())
                    {
                        label_class_info[category.name] = DatasetIO::generateDefaultColor(color_index++);
                    }
                    return true;
                }

                if (array_key != QStringLiteral("images"))
                {
                    return true;
                }

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
                {
                    updateProgress(20, QString("已解析 COCO 图像 %1").arg(processed_image_entries));
                }
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
        batch_image_paths.reserve(DataImporter::ImportBatchImageCount);
        batch_image_widths.reserve(DataImporter::ImportBatchImageCount);
        batch_image_heights.reserve(DataImporter::ImportBatchImageCount);

        size_t emitted_images    = 0;
        bool   sent_classes      = false;
        auto   flush_image_batch = [&]() -> bool
        {
            if (batch_image_paths.empty())
            {
                return true;
            }

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
            batch_image_paths.reserve(DataImporter::ImportBatchImageCount);
            batch_image_widths.reserve(DataImporter::ImportBatchImageCount);
            batch_image_heights.reserve(DataImporter::ImportBatchImageCount);

            return !isCancelRequested();
        };

        updateProgress(40, QString("正在分批写入 COCO 图像..."));
        for (const CocoImage &image : images)
        {
            batch_image_paths.push_back(image.image_path);
            batch_image_widths.push_back(image.width);
            batch_image_heights.push_back(image.height);

            if (batch_image_paths.size() >= DataImporter::ImportBatchImageCount)
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
        batch_labels.reserve(DataImporter::ImportBatchImageCount);
        int processed_annotations = 0;
        int skipped_annotations   = 0;
        int imported_label_count  = 0;

        auto flush_label_batch = [&]() -> bool
        {
            if (batch_labels.empty())
            {
                return true;
            }

            emit dataBatchReady(dataset_id, {}, {}, {}, {}, std::move(batch_labels),
                                static_cast<int64_t>(images.size()), static_cast<int64_t>(images.size()));
            batch_labels.clear();
            batch_labels.reserve(DataImporter::ImportBatchImageCount);
            return !isCancelRequested();
        };

        const bool import_as_segmentation = target_method_ == DeepLearningMethod::Segmentation;
        const bool pass2_ok               = parseTopLevelArrayObjects(
            coco_json_path,
            [&](const QString &array_key, const nlohmann::json &annotation_json) -> bool
            {
                if (isCancelRequested())
                {
                    return false;
                }

                if (array_key != QStringLiteral("annotations"))
                {
                    return true;
                }

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
                        = parseSegmentationPolygons(annotation_json);
                    for (const std::vector<QPointF> &polygon : segmentation_polygons)
                    {
                        QVariantMap label_data
                            = DatasetIO::pointsToLabelData(polygon, image_it->second.width, image_it->second.height);
                        if (!label_data.isEmpty())
                        {
                            label_data_list.push_back(label_data);
                        }
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
                    {
                        label_data_list.push_back(label_data);
                    }
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

                if (batch_labels.size() >= DataImporter::ImportBatchImageCount)
                {
                    if (!flush_label_batch())
                    {
                        return false;
                    }
                }

                if (processed_annotations % 1000 == 0)
                {
                    updateProgress(75, QString("已解析 COCO 标注 %1").arg(processed_annotations));
                }
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

} // namespace dltool::data
