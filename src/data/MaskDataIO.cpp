#include "data/DataIO.h"
#include "DataIOInternal.h"
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
// Mask helpers
// ============================================================================

constexpr int kMaskThreshold = 1;

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
    return QRect(x_min, y_min, x_max - x_min + 1, y_max - y_min + 1);
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
// MaskIO
// ============================================================================

void MaskIO::startImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir)
{
    const double polygon_approx_epsilon_ratio = detail::maskImportPolygonApproxRatio();
    const int    thread_count                 = detail::dataIOThreadCount();
    runInThread([this, dataset_id, image_dir, data_dir, polygon_approx_epsilon_ratio, thread_count]()
                { doImport(dataset_id, image_dir, data_dir, polygon_approx_epsilon_ratio, thread_count); },
                [this](const QString &) { emit importFinished(false, {}, {}); });
}

void MaskIO::startScanLabelClasses(const QString &image_dir, const QString &data_dir)
{
    Q_UNUSED(image_dir)
    runInThread([this, data_dir]() { doScanLabelClasses(data_dir); },
                [this](const QString &error) { emit labelClassesScanned(false, {}, error); });
}

void MaskIO::startExport(ExportDataset dataset, const QString &output_dir, const QVariantMap &options)
{
    const int thread_count = detail::dataIOThreadCount();
    runInThread([this, dataset = std::move(dataset), output_dir, options, thread_count]()
                { doExport(std::move(dataset), output_dir, options, thread_count); },
                [this](const QString &error) { emit exportFinished(false, error); });
}

void MaskIO::doScanLabelClasses(const QString &data_dir)
{
    try
    {
        const QString annotation_dir = data_dir.trimmed();
        if (annotation_dir.isEmpty())
        {
            emit labelClassesScanned(true, {}, QString("未提供 Mask 标注目录"));
            return;
        }

        updateProgress(0, QString("正在扫描 Mask 类别..."));
        const std::vector<QString> mask_files = scanMaskFiles(annotation_dir);
        if (mask_files.empty())
        {
            emit labelClassesScanned(true, {}, QString("未找到 Mask 文件"));
            return;
        }

        std::map<QString, QString> label_class_info;
        int                        color_index = 0;
        int                        processed   = 0;
        for (const QString &mask_path : mask_files)
        {
            if (isCancelRequested())
            {
                emit labelClassesScanned(false, {}, QString("Mask 类别扫描已取消"));
                return;
            }

            ++processed;
            detail::addScannedLabelClass(label_class_info, labelClassNameForMask(mask_path, annotation_dir), color_index);
            if (processed % 1000 == 0)
                updateProgress(50, QString("已扫描 Mask %1").arg(processed));
        }

        updateProgress(100, QString("Mask 类别扫描完成: %1 个类别").arg(label_class_info.size()));
        emit labelClassesScanned(true, label_class_info, QString());
    }
    catch (const std::exception &e)
    {
        spdlog::error("Mask 类别扫描失败: {}", e.what());
        emit labelClassesScanned(false, {}, QString("Mask 类别扫描失败: %1").arg(e.what()));
    }
}

void MaskIO::doImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir,
                      const double polygon_approx_epsilon_ratio, const int thread_count)
{
    try
    {
        const QString annotation_dir = data_dir.trimmed();
        if (annotation_dir.isEmpty())
        {
            importImagesOnly(dataset_id, image_dir, QStringLiteral("Mask"), thread_count);
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

        int processed_masks       = 0;
        int valid_masks           = 0;
        int skipped_masks         = 0;
        int generated_label_count = 0;

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

        struct MaskImportResult
        {
            bool                       valid{false};
            QString                    image_path;
            QString                    label_class_name;
            int                        image_width{0};
            int                        image_height{0};
            std::vector<ImportedLabel> labels;
        };

        std::vector<MaskImportResult> results(mask_files.size());
        parallelFor(mask_files.size(), thread_count, cancel_requested_,
                    [&](const std::size_t index)
                    {
                        const QString &mask_path  = mask_files[index];
                        auto          &result     = results[index];
                        const QString  mask_stem  = QFileInfo(mask_path).completeBaseName();
                        const QString  image_stem = imageStemForMask(mask_path, data_dir, mask_stem);
                        const auto     image_it   = image_by_stem.find(image_stem);
                        if (image_it == image_by_stem.end())
                            return;

                        result.image_path = image_it->second;
                        if (!DatasetIO::getImageDimensions(result.image_path, result.image_width, result.image_height))
                            return;

                        MaskGeometry geometry;
                        if (!readMaskGeometry(mask_path, geometry, polygon_approx_epsilon_ratio))
                            return;

                        result.label_class_name = sanitizeName(labelClassNameForMask(mask_path, data_dir));
                        if (result.label_class_name.isEmpty())
                            return;

                        for (const auto &polygon : geometry.polygons)
                        {
                            const QVariantMap label_data
                                = maskToLabelData(polygon, geometry.mask_width, geometry.mask_height,
                                                  result.image_width, result.image_height);
                            if (label_data.isEmpty())
                                continue;

                            ImportedLabel imported_label;
                            imported_label.label_class_name = result.label_class_name;
                            imported_label.image_path       = result.image_path;
                            imported_label.data             = label_data;
                            result.labels.push_back(std::move(imported_label));
                        }
                        result.valid = !result.labels.empty();
                    },
                    [this](const std::size_t completed, const std::size_t total)
                    {
                        const int progress = 10 + static_cast<int>(completed * 80 / std::max<std::size_t>(1, total));
                        updateProgress(progress, QString("已并行解析 Mask %1/%2").arg(completed).arg(total));
                    });

        for (std::size_t index = 0; index < results.size(); ++index)
        {
            if (isCancelRequested())
            {
                emit importFinished(false, {}, {});
                return;
            }

            ++processed_masks;
            const auto &result = results[index];
            if (!result.valid)
            {
                ++skipped_masks;
            }
            else
            {
                ++valid_masks;
                generated_label_count += static_cast<int>(result.labels.size());
                if (label_class_colors.find(result.label_class_name) == label_class_colors.end())
                    label_class_colors[result.label_class_name]
                        = DatasetIO::generateDefaultColor(static_cast<int>(label_class_colors.size()));
                batch_label_class_info[result.label_class_name] = label_class_colors[result.label_class_name];

                for (const ImportedLabel &label : result.labels)
                {
                    batch_image_paths.push_back(result.image_path);
                    batch_image_widths.push_back(result.image_width);
                    batch_image_heights.push_back(result.image_height);
                    batch_labels.push_back(label);
                }
            }

            if (processed_masks % std::max<int>(1, static_cast<int>(mask_files.size()) / 10) == 0
                || processed_masks == static_cast<int>(mask_files.size()))
            {
                const int progress = 10 + processed_masks * 80 / std::max<int>(1, static_cast<int>(mask_files.size()));
                updateProgress(progress, QString("已处理 Mask %1/%2").arg(processed_masks).arg(mask_files.size()));
            }

            if (batch_labels.size() >= DataIO::ImportBatchImageCount && !flush_batch())
            {
                emit importFinished(false, {}, {});
                return;
            }
        }

        if (!flush_batch())
        {
            emit importFinished(false, {}, {});
            return;
        }

        updateProgress(100, QString("Mask 导入完成: 有效 %1 个，生成 %2 个标注，跳过 %3 个")
                                .arg(valid_masks)
                                .arg(generated_label_count)
                                .arg(skipped_masks));
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
    {
        geometry.polygons = std::move(polygons);
    }
    else
    {
        const double left      = geometry.bbox.x();
        const double top       = geometry.bbox.y();
        const double right     = left + geometry.bbox.width();
        const double bottom    = top + geometry.bbox.height();
        const auto   rect_poly = dltool::common::geometry::rectangleToPolygon(QPointF(left, top), QPointF(right, bottom));
        if (rect_poly.empty())
            return false;
        geometry.polygons = {rect_poly};
    }

    return true;
}

QVariantMap MaskIO::maskToLabelData(const std::vector<QPointF> &polygon, int mask_width, int mask_height,
                                    int image_width, int image_height) const
{
    if (mask_width <= 0 || mask_height <= 0 || image_width <= 0 || image_height <= 0 || polygon.size() < 3)
        return {};

    const std::vector<QPointF> mapped_polygon
        = dltool::common::geometry::mapPolygon(polygon, QSize(mask_width, mask_height), QSize(image_width, image_height));
    const QRectF mapped_bounds = dltool::common::geometry::polygonBounds(mapped_polygon);
    if (!mapped_bounds.isValid() || mapped_bounds.width() <= 0.0 || mapped_bounds.height() <= 0.0)
        return {};

    if (target_method_ == DeepLearningMethod::Detection)
        return DatasetIO::bboxToLabelData(mapped_bounds.x(), mapped_bounds.y(), mapped_bounds.width(),
                                          mapped_bounds.height(), image_width, image_height);

    if (target_method_ == DeepLearningMethod::Segmentation || target_method_ == DeepLearningMethod::AnomalyDetection)
        return DatasetIO::pointsToLabelData(mapped_polygon, image_width, image_height);

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

void MaskIO::doExport(ExportDataset dataset, QString output_dir, QVariantMap options, const int thread_count)
{
    try
    {
        QString err_msg;
        if (!DataIO::checkExportSourceCollision(dataset, output_dir, DataFormat::Mask, err_msg))
        {
            emit exportFinished(false, err_msg);
            return;
        }

        SafeExportScope scope(output_dir);
        if (!scope.isValid())
        {
            emit exportFinished(false, scope.error());
            return;
        }

        const QString images_dir = QDir(scope.stagingDir()).filePath(QStringLiteral("images"));
        const QString masks_dir  = QDir(scope.stagingDir()).filePath(QStringLiteral("masks"));
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
            const QString file_name  = detail::uniqueImageName(image.path, image.image_id, used_image_names, used_image_stems);
            used_image_names[file_name]++;
            used_image_stems[QFileInfo(file_name).completeBaseName()]++;
            image_name_by_id[image.image_id] = file_name;
        }

        std::set<QString> mask_names;
        for (const ExportImage &image : dataset.images)
        {
            const QString mask_name
                = QString("%1.png").arg(QFileInfo(image_name_by_id[image.image_id]).completeBaseName());
            if (!mask_names.insert(mask_name).second)
            {
                emit exportFinished(false, QString("Mask 文件名冲突: %1").arg(mask_name));
                return;
            }
        }

        std::map<int64_t, std::vector<ExportLabel>> labels_by_image_id;
        for (const ExportLabel &label : dataset.labels) labels_by_image_id[label.image_id].push_back(label);
        const auto &image_name_by_id_readonly   = image_name_by_id;
        const auto &class_values_readonly       = class_values;
        const auto &labels_by_image_id_readonly = labels_by_image_id;

        struct MaskExportResult
        {
            bool    success{true};
            QString error;
            int     written_labels{0};
            int     skipped_labels{0};
        };

        if (isCancelRequested())
        {
            emit exportFinished(false, QStringLiteral("导出已取消"));
            return;
        }

        std::vector<MaskExportResult> results(image_count);
        parallelFor(
            static_cast<std::size_t>(image_count), thread_count, cancel_requested_,
            [&](const std::size_t index)
            {
                const ExportImage &image  = dataset.images[index];
                auto              &result = results[index];
                QString            task_error;
                if (!DatasetIO::copyFile(image.path,
                                         QDir(images_dir).filePath(image_name_by_id_readonly.at(image.image_id)),
                                         task_error))
                {
                    result.success = false;
                    result.error   = task_error;
                    return;
                }

                int width  = image.width;
                int height = image.height;
                if ((width <= 0 || height <= 0) && !DatasetIO::getImageDimensions(image.path, width, height))
                {
                    result.success = false;
                    result.error   = QString("无法读取图像尺寸，不能导出 Mask: %1").arg(image.path);
                    return;
                }

                QImage mask(width, height, QImage::Format_ARGB32);
                mask.fill(Qt::black);
                const auto labels_it = labels_by_image_id_readonly.find(image.image_id);
                if (labels_it != labels_by_image_id_readonly.end())
                    for (const ExportLabel &label : labels_it->second)
                    {
                        const auto value_it = class_values_readonly.find(label.label_class_id);
                        if (value_it == class_values_readonly.end())
                        {
                            ++result.skipped_labels;
                            continue;
                        }
                        if (paintLabelToMask(mask, label.data, value_it->second))
                            ++result.written_labels;
                        else
                            ++result.skipped_labels;
                    }

                const QString mask_name
                    = QString("%1.png").arg(QFileInfo(image_name_by_id_readonly.at(image.image_id)).completeBaseName());
                if (!mask.convertToFormat(QImage::Format_Grayscale8).save(QDir(masks_dir).filePath(mask_name), "PNG"))
                {
                    result.success = false;
                    result.error   = QString("写入 Mask 失败: %1").arg(mask_name);
                }
            },
            [this](const std::size_t completed, const std::size_t total)
            {
                updateProgress(5 + static_cast<int>(completed * 90 / std::max<std::size_t>(1, total)),
                               QString("已写入 Mask %1/%2").arg(completed).arg(total));
            });

        if (isCancelRequested())
        {
            emit exportFinished(false, QStringLiteral("导出已取消"));
            return;
        }

        int written_label_count = 0;
        int skipped_label_count = 0;
        for (int i = 0; i < image_count; ++i)
        {
            if (!results[i].success)
            {
                emit exportFinished(false, results[i].error);
                return;
            }
            written_label_count += results[i].written_labels;
            skipped_label_count += results[i].skipped_labels;
        }

        if (!writeClassMetadata(dataset, scope.stagingDir(), mode, class_values, err_msg))
        {
            emit exportFinished(false, err_msg);
            return;
        }

        if (!DataIO::validateExportOutput(DataFormat::Mask, dataset, scope.stagingDir(), options, err_msg))
        {
            emit exportFinished(false, err_msg);
            return;
        }

        if (!scope.publish(err_msg))
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

} // namespace dltool::data
