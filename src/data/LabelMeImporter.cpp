#include "data/LabelMeImporter.h"

#include "database/DataBase.h"

#include <json.hpp>
#include <spdlog/spdlog.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <set>

namespace dltool::data {

LabelMeImporter::LabelMeImporter(dltool::database::ProjectDataBase *database, QObject *parent)
    : DataImporter(database, parent)
{
}

LabelMeImporter::~LabelMeImporter() {}

void LabelMeImporter::startImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir)
{
    QThread *worker_thread = new QThread();

    connect(
        worker_thread, &QThread::started, this,
        [this, dataset_id, image_dir, data_dir]() { doImport(dataset_id, image_dir, data_dir); }, Qt::DirectConnection);

    connect(this, &LabelMeImporter::importFinished, worker_thread, &QThread::quit);
    connect(worker_thread, &QThread::finished, worker_thread, &QThread::deleteLater);

    worker_thread->start();
}

void LabelMeImporter::doImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir)
{
    spdlog::info("开始解析 LabelMe 数据: dataset_id={}", dataset_id);

    try
    {
        updateProgress(0, QStringLiteral("正在扫描图像文件..."));
        const std::vector<QString> image_files = DatasetIO::scanImageFiles(image_dir);

        if (image_files.empty())
        {
            updateProgress(100, QStringLiteral("未找到任何图像文件"));
            emit dataReady(false, dataset_id, {}, {}, {}, {}, {});
            emit importFinished(false, {}, {});
            return;
        }

        const int total_images = static_cast<int>(image_files.size());
        std::map<QString, ImageData> images_map;
        int                          processed_images = 0;
        int                          skipped_images   = 0;

        for (const auto &image_path : image_files)
        {
            int width  = 0;
            int height = 0;
            if (!DatasetIO::getImageDimensions(image_path, width, height))
            {
                skipped_images++;
                continue;
            }

            QFileInfo file_info(image_path);
            ImageData img_data;
            img_data.image_path   = image_path;
            img_data.image_width  = width;
            img_data.image_height = height;
            images_map[file_info.baseName()] = img_data;
            processed_images++;

            if (processed_images % std::max(1, total_images / 10) == 0 || processed_images == total_images)
            {
                updateProgress(processed_images * 40 / total_images,
                               QStringLiteral("已扫描图像 %1/%2").arg(processed_images).arg(total_images));
            }
        }

        if (images_map.empty())
        {
            updateProgress(100, QStringLiteral("没有有效的图像可导入"));
            emit dataReady(false, dataset_id, {}, {}, {}, {}, {});
            emit importFinished(false, {}, {});
            return;
        }

        updateProgress(40, QStringLiteral("图像扫描完成: 总数=%1, 成功=%2, 跳过=%3")
                               .arg(total_images)
                               .arg(processed_images)
                               .arg(skipped_images));

        std::vector<LabelMeData> parsed_annotations;
        if (data_dir.isEmpty() || !QFileInfo(data_dir).exists())
        {
            updateProgress(90, QStringLiteral("标注路径为空，只导入图像"));
            processAndEmitData(dataset_id, images_map, parsed_annotations);
            updateProgress(100, QStringLiteral("导入完成: %1 个图像, 0 个标注").arg(images_map.size()));
            emit importFinished(true, {}, {});
            return;
        }

        updateProgress(40, QStringLiteral("正在扫描标注文件..."));
        const std::vector<QString> json_files = DatasetIO::scanJsonFiles(data_dir);
        const int                  total_json_files = static_cast<int>(json_files.size());
        if (json_files.empty())
        {
            updateProgress(90, QStringLiteral("未找到标注文件，只导入图像"));
            processAndEmitData(dataset_id, images_map, parsed_annotations);
            updateProgress(100, QStringLiteral("导入完成: %1 个图像, 0 个标注").arg(images_map.size()));
            emit importFinished(true, {}, {});
            return;
        }

        int parsed_count  = 0;
        int skipped_count = 0;
        for (const auto &json_path : json_files)
        {
            LabelMeData data;
            if (!parseLabelMeJson(json_path, data))
            {
                skipped_count++;
                continue;
            }

            QFileInfo json_file_info(json_path);
            auto      image_it = images_map.find(json_file_info.baseName());
            if (image_it == images_map.end())
            {
                spdlog::warn("标注文件对应的图像不存在，跳过: {}", json_path.toStdString());
                skipped_count++;
                continue;
            }

            data.image_path   = image_it->second.image_path;
            data.image_width  = image_it->second.image_width;
            data.image_height = image_it->second.image_height;
            parsed_annotations.push_back(data);
            parsed_count++;

            if (parsed_count % std::max(1, total_json_files / 10) == 0 || parsed_count == total_json_files)
            {
                const int progress = 40 + (parsed_count * 50 / std::max(1, total_json_files));
                updateProgress(progress,
                               QStringLiteral("已解析标注 %1/%2").arg(parsed_count).arg(total_json_files));
            }
        }

        updateProgress(90, QStringLiteral("标注解析完成: 总数=%1, 成功=%2, 跳过=%3")
                               .arg(total_json_files)
                               .arg(parsed_count)
                               .arg(skipped_count));
        processAndEmitData(dataset_id, images_map, parsed_annotations);
        updateProgress(100, QStringLiteral("导入完成: %1 个图像, %2 个标注文件")
                               .arg(images_map.size())
                               .arg(parsed_count));
        emit importFinished(true, {}, {});
    }
    catch (const std::exception &e)
    {
        spdlog::error("导入过程中发生异常: {}", e.what());
        updateProgress(100, QStringLiteral("导入失败: %1").arg(e.what()));
        emit dataReady(false, dataset_id, {}, {}, {}, {}, {});
        emit importFinished(false, {}, {});
    }
}

bool LabelMeImporter::parseLabelMeJson(const QString &json_path, LabelMeData &data)
{
    try
    {
        QFile file(json_path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            spdlog::error("无法打开 LabelMe JSON 文件: {}", json_path.toStdString());
            return false;
        }

        const QByteArray json_bytes = file.readAll();
        const auto       json_data
            = nlohmann::json::parse(json_bytes.constData(), json_bytes.constData() + json_bytes.size());

        data.image_path   = QString();
        data.image_width  = json_data.value("imageWidth", 0);
        data.image_height = json_data.value("imageHeight", 0);

        if (!json_data.contains("shapes") || !json_data["shapes"].is_array())
        {
            return true;
        }

        for (const auto &shape_json : json_data["shapes"])
        {
            if (!shape_json.contains("label") || !shape_json["label"].is_string() || !shape_json.contains("points")
                || !shape_json["points"].is_array())
            {
                continue;
            }

            LabelMeShape shape;
            shape.label      = QString::fromStdString(shape_json["label"].get<std::string>());
            shape.shape_type = QString::fromStdString(shape_json.value("shape_type", "polygon"));

            for (const auto &point_json : shape_json["points"])
            {
                if (!point_json.is_array() || point_json.size() < 2 || !point_json[0].is_number()
                    || !point_json[1].is_number())
                {
                    continue;
                }

                shape.points.emplace_back(point_json[0].get<double>(), point_json[1].get<double>());
            }

            if (!shape.points.empty())
            {
                data.shapes.push_back(shape);
            }
        }

        return true;
    }
    catch (const std::exception &e)
    {
        spdlog::error("解析 LabelMe JSON 失败: {}, 错误: {}", json_path.toStdString(), e.what());
        return false;
    }
}

QVariantMap LabelMeImporter::convertShapeToLabelData(const LabelMeShape &shape, int image_width, int image_height)
{
    if (shape.shape_type == QStringLiteral("rectangle"))
    {
        if (shape.points.size() < 2)
        {
            spdlog::warn("rectangle 标注点数不足: {}", shape.points.size());
            return {};
        }

        const QPointF p1     = shape.points[0];
        const QPointF p2     = shape.points[1];
        const double  x_min  = std::min(p1.x(), p2.x());
        const double  y_min  = std::min(p1.y(), p2.y());
        const double  x_max  = std::max(p1.x(), p2.x());
        const double  y_max  = std::max(p1.y(), p2.y());
        return DatasetIO::bboxToLabelData(x_min, y_min, x_max - x_min, y_max - y_min, image_width, image_height);
    }

    if (shape.shape_type == QStringLiteral("polygon"))
    {
        if (shape.points.empty())
        {
            spdlog::warn("polygon 标注没有坐标点");
            return {};
        }

        double x_min = shape.points[0].x();
        double y_min = shape.points[0].y();
        double x_max = shape.points[0].x();
        double y_max = shape.points[0].y();
        for (const QPointF &point : shape.points)
        {
            x_min = std::min(x_min, point.x());
            y_min = std::min(y_min, point.y());
            x_max = std::max(x_max, point.x());
            y_max = std::max(y_max, point.y());
        }

        return DatasetIO::bboxToLabelData(x_min, y_min, x_max - x_min, y_max - y_min, image_width, image_height);
    }

    spdlog::warn("不支持的 LabelMe shape_type: {}, label: {}", shape.shape_type.toStdString(),
                 shape.label.toStdString());
    return {};
}

void LabelMeImporter::processAndEmitData(int64_t dataset_id, const std::map<QString, ImageData> &images,
                                         const std::vector<LabelMeData> &parsed_annotations)
{
    std::vector<QString>       image_paths;
    std::vector<int64_t>       image_widths;
    std::vector<int64_t>       image_heights;
    std::map<QString, QString> label_class_info;
    std::vector<ImportedLabel> labels;

    std::set<QString> images_with_annotations;
    for (const auto &annotation : parsed_annotations)
    {
        if (images_with_annotations.insert(annotation.image_path).second)
        {
            image_paths.push_back(annotation.image_path);
            image_widths.push_back(annotation.image_width);
            image_heights.push_back(annotation.image_height);
        }
    }

    for (const auto &[_, img_data] : images)
    {
        if (images_with_annotations.find(img_data.image_path) == images_with_annotations.end())
        {
            image_paths.push_back(img_data.image_path);
            image_widths.push_back(img_data.image_width);
            image_heights.push_back(img_data.image_height);
        }
    }

    std::set<QString> label_class_names;
    for (const auto &annotation : parsed_annotations)
    {
        for (const auto &shape : annotation.shapes)
        {
            label_class_names.insert(shape.label);
        }
    }

    int color_index = 0;
    for (const auto &class_name : label_class_names)
    {
        label_class_info[class_name] = DatasetIO::generateDefaultColor(color_index++);
    }

    for (const auto &annotation : parsed_annotations)
    {
        for (const auto &shape : annotation.shapes)
        {
            const QVariantMap label_data
                = convertShapeToLabelData(shape, annotation.image_width, annotation.image_height);
            if (label_data.isEmpty())
            {
                continue;
            }

            ImportedLabel imported_label;
            imported_label.label_class_name = shape.label;
            imported_label.data             = label_data;
            imported_label.image_path       = annotation.image_path;
            labels.push_back(imported_label);
        }
    }

    spdlog::info("LabelMe 数据处理完成: images={}, label_classes={}, labels={}", image_paths.size(),
                 label_class_info.size(), labels.size());
    emit dataReady(true, dataset_id, image_paths, image_widths, image_heights, label_class_info, labels);
}

} // namespace dltool::data
