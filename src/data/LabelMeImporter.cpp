#include "data/LabelMeImporter.h"

#include "database/DataBase.h"

#include <json.hpp>
#include <spdlog/spdlog.h>

#include <QFile>
#include <QFileInfo>
#include <algorithm>


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
        updateProgress(0, QString("正在扫描图像文件..."));
        const std::vector<QString> image_files = DatasetIO::scanImageFiles(image_dir);
        if (image_files.empty())
        {
            updateProgress(100, QString("未找到任何图像文件"));
            emit importFinished(false, {}, {});
            return;
        }

        std::map<QString, QString> annotation_files_by_name;
        int                        total_json_files = 0;
        if (!data_dir.isEmpty() && QFileInfo(data_dir).exists())
        {
            updateProgress(5, QString("正在扫描标注文件..."));
            const std::vector<QString> json_files = DatasetIO::scanJsonFiles(data_dir);
            total_json_files                      = static_cast<int>(json_files.size());
            for (const QString &json_path : json_files)
            {
                annotation_files_by_name[QFileInfo(json_path).baseName()] = json_path;
            }
        }

        const int total_images = static_cast<int>(image_files.size());

        std::vector<QString>       batch_image_paths;
        std::vector<int64_t>       batch_image_widths;
        std::vector<int64_t>       batch_image_heights;
        std::map<QString, QString> batch_label_class_info;
        std::vector<ImportedLabel> batch_labels;
        batch_image_paths.reserve(DataImporter::ImportBatchImageCount);
        batch_image_widths.reserve(DataImporter::ImportBatchImageCount);
        batch_image_heights.reserve(DataImporter::ImportBatchImageCount);

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
            {
                return true;
            }

            emit dataBatchReady(dataset_id, std::move(batch_image_paths), std::move(batch_image_widths),
                                std::move(batch_image_heights), std::move(batch_label_class_info),
                                std::move(batch_labels), processed_images, total_images);

            batch_image_paths.clear();
            batch_image_widths.clear();
            batch_image_heights.clear();
            batch_label_class_info.clear();
            batch_labels.clear();
            batch_image_paths.reserve(DataImporter::ImportBatchImageCount);
            batch_image_widths.reserve(DataImporter::ImportBatchImageCount);
            batch_image_heights.reserve(DataImporter::ImportBatchImageCount);

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

            const QString image_name = QFileInfo(image_path).baseName();
            auto          json_it    = annotation_files_by_name.find(image_name);
            if (json_it != annotation_files_by_name.end())
            {
                LabelMeData data;
                if (parseLabelMeJson(json_it->second, data))
                {
                    data.image_path   = image_path;
                    data.image_width  = width;
                    data.image_height = height;
                    ++parsed_annotations;

                    for (const LabelMeShape &shape : data.shapes)
                    {
                        if (shape.label.isEmpty())
                        {
                            continue;
                        }

                        auto color_it = label_class_colors.find(shape.label);
                        if (color_it == label_class_colors.end())
                        {
                            const QString color                 = DatasetIO::generateDefaultColor(color_index++);
                            color_it                            = label_class_colors.emplace(shape.label, color).first;
                            batch_label_class_info[shape.label] = color;
                        }

                        const QVariantMap label_data = convertShapeToLabelData(shape, width, height);
                        if (label_data.isEmpty())
                        {
                            continue;
                        }

                        ImportedLabel imported_label;
                        imported_label.label_class_name = shape.label;
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

            if (batch_image_paths.size() >= DataImporter::ImportBatchImageCount)
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

        const QPointF p1    = shape.points[0];
        const QPointF p2    = shape.points[1];
        const double  x_min = std::min(p1.x(), p2.x());
        const double  y_min = std::min(p1.y(), p2.y());
        const double  x_max = std::max(p1.x(), p2.x());
        const double  y_max = std::max(p1.y(), p2.y());
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

    spdlog::warn("不支持的 LabelMe shape_type: {}, label: {}", shape.shape_type.toStdString(),
                 shape.label.toStdString());
    return {};
}

} // namespace dltool::data
