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

// ============================================================================
// LabelMeIO
// ============================================================================

void LabelMeIO::startImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir)
{
    const int thread_count = detail::dataIOThreadCount();
    runInThread([this, dataset_id, image_dir, data_dir, thread_count]()
                { doImport(dataset_id, image_dir, data_dir, thread_count); },
                [this](const QString &) { emit importFinished(false, {}, {}); });
}

void LabelMeIO::startScanLabelClasses(const QString &image_dir, const QString &data_dir)
{
    runInThread([this, image_dir, data_dir]() { doScanLabelClasses(image_dir, data_dir); },
                [this](const QString &error) { emit labelClassesScanned(false, {}, error); });
}

void LabelMeIO::startExport(ExportDataset dataset, const QString &output_dir, const QVariantMap &options)
{
    Q_UNUSED(options)
    const int thread_count = detail::dataIOThreadCount();
    runInThread([this, dataset = std::move(dataset), output_dir, thread_count]()
                { doExport(std::move(dataset), output_dir, thread_count); },
                [this](const QString &error) { emit exportFinished(false, error); });
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

QVariantMap LabelMeIO::convertShapeToLabelData(const LabelMeShape &shape, int source_image_width,
                                               int source_image_height, int image_width, int image_height,
                                               bool convert_rectangle_to_polygon)
{
    const QSize target_size(image_width, image_height);
    const auto mapPoints = [source_image_width, source_image_height, target_size](const std::vector<QPointF> &points)
    {
        if (source_image_width > 0 && source_image_height > 0)
        {
            return dltool::common::geometry::mapPolygon(
                points, QSize(source_image_width, source_image_height), target_size);
        }
        return dltool::common::geometry::clipPolygon(
            points, QRectF(0.0, 0.0, target_size.width(), target_size.height()));
    };

    if (shape.shape_type == QStringLiteral("rectangle"))
    {
        if (shape.points.size() < 2)
        {
            spdlog::warn("rectangle 标注点数不足: {}", shape.points.size());
            return {};
        }

        const std::vector<QPointF> source_rectangle
            = dltool::common::geometry::rectangleToPolygon(shape.points[0], shape.points[1]);
        const std::vector<QPointF> image_rectangle = mapPoints(source_rectangle);
        const QRectF               rectangle_bounds = dltool::common::geometry::polygonBounds(image_rectangle);
        if (!rectangle_bounds.isValid() || rectangle_bounds.width() <= 0.0 || rectangle_bounds.height() <= 0.0)
            return {};
        if (target_method_ == DeepLearningMethod::Segmentation
            || target_method_ == DeepLearningMethod::AnomalyDetection)
        {
            if (!convert_rectangle_to_polygon)
                return {};

            const QVariantMap label_data = DatasetIO::pointsToLabelData(image_rectangle, image_width, image_height);
            if (label_data.isEmpty())
            {
                spdlog::warn("rectangle 标注无法转换为四点多边形: label={}", shape.label.toUtf8().constData());
                return {};
            }
            return label_data;
        }

        return DatasetIO::bboxToLabelData(rectangle_bounds.x(), rectangle_bounds.y(), rectangle_bounds.width(),
                                           rectangle_bounds.height(), image_width, image_height);
    }

    if (shape.shape_type == QStringLiteral("polygon"))
    {
        if (shape.points.empty())
        {
            spdlog::warn("polygon 标注没有坐标点");
            return {};
        }

        const std::vector<QPointF> image_points = mapPoints(shape.points);

        const QVariantMap label_data = DatasetIO::pointsToLabelData(image_points, image_width, image_height);
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
            emit labelClassesScanned(true, {}, QString("未提供 LabelMe 标注目录"));
            return;
        }

        updateProgress(0, QString("正在扫描 LabelMe 类别..."));
        const std::vector<QString> json_files = DatasetIO::scanJsonFiles(annotation_dir);
        if (json_files.empty())
        {
            emit labelClassesScanned(true, {}, QString("未找到 LabelMe 标注文件"));
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
                emit labelClassesScanned(false, {}, QString("LabelMe 类别扫描已取消"));
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
                detail::addScannedLabelClass(label_class_info, shape.label, color_index);

            if (processed_files % 500 == 0)
                updateProgress(50, QString("已扫描 LabelMe 标注 %1").arg(processed_files));
        }

        updateProgress(100, QString("LabelMe 类别扫描完成: %1 个类别，跳过 %2 个文件")
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

void LabelMeIO::doImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir, const int thread_count)
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

        struct LabelMeImportResult
        {
            int                        width{0};
            int                        height{0};
            bool                       valid_image{false};
            bool                       parsed_annotation{false};
            bool                       skipped_annotation{false};
            std::vector<ImportedLabel> labels;
        };

        const int                        target_method                              = target_method_;
        const auto                      &annotation_files_by_relative_name_readonly = annotation_files_by_relative_name;
        std::vector<LabelMeImportResult> results(image_files.size());
        parallelFor(image_files.size(), thread_count, cancel_requested_,
                    [&](const std::size_t index)
                    {
                        const QString &image_path = image_files[index];
                        auto          &result     = results[index];
                        result.valid_image = DatasetIO::getImageDimensions(image_path, result.width, result.height);
                        if (!result.valid_image)
                            return;

                        const QString image_relative = dltool::common::relativePath(image_root_dir, image_path);
                        const QString image_key      = QFileInfo(image_relative).completeBaseName();
                        const auto    json_it        = annotation_files_by_relative_name_readonly.find(image_key);
                        if (json_it == annotation_files_by_relative_name_readonly.end())
                            return;

                        LabelMeData data;
                        if (!parseLabelMeJson(json_it->second, data))
                        {
                            result.skipped_annotation = true;
                            return;
                        }

                        result.parsed_annotation = true;
                        const bool convert_rectangles_to_polygons
                            = (target_method == DeepLearningMethod::Segmentation
                               || target_method == DeepLearningMethod::AnomalyDetection);

                        for (const LabelMeShape &shape : data.shapes)
                        {
                            if (shape.label.isEmpty())
                                continue;
                            const QString label_class_name = sanitizeName(shape.label);
                            if (label_class_name.isEmpty())
                                continue;

                            const QVariantMap label_data
                                = convertShapeToLabelData(shape, data.image_width, data.image_height, result.width,
                                                          result.height, convert_rectangles_to_polygons);
                            if (label_data.isEmpty())
                                continue;

                            ImportedLabel imported_label;
                            imported_label.label_class_name = label_class_name;
                            imported_label.data             = label_data;
                            imported_label.image_path       = image_path;
                            result.labels.push_back(std::move(imported_label));
                        }
                    },
                    [this](const std::size_t completed, const std::size_t total)
                    {
                        const int progress = 10 + static_cast<int>(completed * 80 / std::max<std::size_t>(1, total));
                        updateProgress(progress, QString("已并行解析 LabelMe 图像 %1/%2").arg(completed).arg(total));
                    });

        for (std::size_t index = 0; index < image_files.size(); ++index)
        {
            if (isCancelRequested())
            {
                emit importFinished(false, {}, {});
                return;
            }

            ++processed_images;
            const QString &image_path = image_files[index];
            const auto    &result     = results[index];
            if (!result.valid_image)
            {
                ++skipped_images;
            }
            else
            {
                ++valid_images;
                batch_image_paths.push_back(image_path);
                batch_image_widths.push_back(result.width);
                batch_image_heights.push_back(result.height);

                if (result.parsed_annotation)
                    ++parsed_annotations;
                if (result.skipped_annotation)
                    ++skipped_annotations;

                for (const ImportedLabel &label : result.labels)
                {
                    if (label_class_colors.find(label.label_class_name) == label_class_colors.end())
                    {
                        const QString color                            = DatasetIO::generateDefaultColor(color_index++);
                        label_class_colors[label.label_class_name]     = color;
                        batch_label_class_info[label.label_class_name] = color;
                    }
                    batch_labels.push_back(label);
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

void LabelMeIO::doExport(ExportDataset dataset, QString output_dir, const int thread_count)
{
    try
    {
        QString err_msg;
        if (!DataIO::checkExportSourceCollision(dataset, output_dir, DataFormat::LabelMe, err_msg))
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

        const QString images_dir      = QDir(scope.stagingDir()).filePath(QStringLiteral("images"));
        const QString annotations_dir = QDir(scope.stagingDir()).filePath(QStringLiteral("annotations"));
        if (!ensureDirectory(images_dir, err_msg) || !ensureDirectory(annotations_dir, err_msg))
        {
            emit exportFinished(false, err_msg);
            return;
        }

        updateProgress(5, QString("正在导出 LabelMe 数据..."));

        std::map<QString, int>     used_image_names;
        std::map<QString, int>     used_image_stems;
        std::map<int64_t, QString> image_name_by_id;
        const auto                &image_name_by_id_readonly = image_name_by_id;
        const int                  image_count               = static_cast<int>(dataset.images.size());

        for (int i = 0; i < image_count; ++i)
        {
            const ExportImage &image = dataset.images[i];
            const QString file_name  = detail::uniqueImageName(image.path, image.image_id, used_image_names, used_image_stems);
            used_image_names[file_name]++;
            used_image_stems[QFileInfo(file_name).completeBaseName()]++;
            image_name_by_id[image.image_id] = file_name;
        }

        std::map<int64_t, QString> class_name_by_id;
        for (const ExportLabelClass &label_class : dataset.label_classes)
            class_name_by_id[label_class.id] = label_class.name;

        std::map<int64_t, std::vector<ExportLabel>> labels_by_image_id;
        for (const ExportLabel &label : dataset.labels) labels_by_image_id[label.image_id].push_back(label);
        const auto &class_name_by_id_readonly   = class_name_by_id;
        const auto &labels_by_image_id_readonly = labels_by_image_id;

        struct LabelMeExportResult
        {
            bool    success{true};
            QString error;
        };

        if (isCancelRequested())
        {
            emit exportFinished(false, QStringLiteral("导出已取消"));
            return;
        }

        std::vector<LabelMeExportResult> results(image_count);
        parallelFor(
            static_cast<std::size_t>(image_count), thread_count, cancel_requested_,
            [&](const std::size_t index)
            {
                const ExportImage &image      = dataset.images[index];
                const QString      image_name = image_name_by_id_readonly.at(image.image_id);
                QString            task_error;
                if (!DatasetIO::copyFile(image.path, QDir(images_dir).filePath(image_name), task_error))
                {
                    results[index].success = false;
                    results[index].error   = task_error;
                    return;
                }

                nlohmann::json json_data;
                json_data["version"]     = "5.0.1";
                json_data["flags"]       = nlohmann::json::object();
                json_data["shapes"]      = nlohmann::json::array();
                json_data["imagePath"]   = image_name.toStdString();
                json_data["imageData"]   = nullptr;
                json_data["imageHeight"] = image.height;
                json_data["imageWidth"]  = image.width;

                const auto labels_it = labels_by_image_id_readonly.find(image.image_id);
                if (labels_it != labels_by_image_id_readonly.end())
                    for (const ExportLabel &label : labels_it->second)
                    {
                        const double x = label.data.value(QStringLiteral("x")).toDouble();
                        const double y = label.data.value(QStringLiteral("y")).toDouble();
                        const double w = label.data.value(QStringLiteral("width")).toDouble();
                        const double h = label.data.value(QStringLiteral("height")).toDouble();
                        if (w <= 0 || h <= 0)
                            continue;

                        nlohmann::json shape;
                        const auto     class_it = class_name_by_id_readonly.find(label.label_class_id);
                        shape["label"] = (class_it != class_name_by_id_readonly.end() ? class_it->second : QString())
                                             .toStdString();
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
                    results[index].success = false;
                    results[index].error   = QString("无法写入标注文件: %1").arg(annotation_file.fileName());
                    return;
                }
                annotation_file.write(QByteArray::fromStdString(json_data.dump(2)));
            },
            [this](const std::size_t completed, const std::size_t total)
            {
                updateProgress(5 + static_cast<int>(completed * 90 / std::max<std::size_t>(1, total)),
                               QString("已处理 LabelMe 导出 %1/%2").arg(completed).arg(total));
            });

        if (isCancelRequested())
        {
            emit exportFinished(false, QStringLiteral("导出已取消"));
            return;
        }

        for (int i = 0; i < image_count; ++i)
        {
            if (!results[i].success)
            {
                emit exportFinished(false, results[i].error);
                return;
            }
        }

        if (!DataIO::validateExportOutput(DataFormat::LabelMe, dataset, scope.stagingDir(), {}, err_msg))
        {
            emit exportFinished(false, err_msg);
            return;
        }

        if (!scope.publish(err_msg))
        {
            emit exportFinished(false, err_msg);
            return;
        }

        updateProgress(100, QString("LabelMe 导出完成"));
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

} // namespace dltool::data
