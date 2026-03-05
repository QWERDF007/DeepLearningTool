#include "data/LabelMeImporter.h"

#include "data/DataBase.h"

#include <json.hpp>
#include <spdlog/spdlog.h>

#include <QColor>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <algorithm>

namespace dltool::data {

LabelMeImporter::LabelMeImporter(ProjectDataBase *database, QObject *parent)
    : DataImporter(database, parent)
{
}

LabelMeImporter::~LabelMeImporter() {}

void LabelMeImporter::startImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir)
{
    // 创建工作线程
    QThread *worker_thread = new QThread();

    // 使用 lambda 在工作线程中执行导入
    connect(
        worker_thread, &QThread::started, this,
        [this, dataset_id, image_dir, data_dir]() { doImport(dataset_id, image_dir, data_dir); }, Qt::DirectConnection);

    // 导入完成后清理线程
    connect(this, &LabelMeImporter::importFinished, worker_thread, &QThread::quit);
    connect(worker_thread, &QThread::finished, worker_thread, &QThread::deleteLater);

    // 启动线程
    worker_thread->start();
}

void LabelMeImporter::doImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir)
{
    spdlog::info("开始解析 LabelMe 数据: dataset_id={}", dataset_id);

    try
    {
        // ========== Phase 1: 扫描和收集所有图像 ==========
        updateProgress(0, "正在扫描图像文件...");
        std::vector<QString> image_files = scanImageFiles(image_dir);

        if (image_files.empty())
        {
            spdlog::warn("未找到任何图像文件");
            updateProgress(100, "未找到任何图像文件");
            emit dataReady(false, dataset_id, {}, {}, {}, {}, {});
            return;
        }

        int total_images = static_cast<int>(image_files.size());
        spdlog::info("找到 {} 个图像文件，开始读取尺寸...", total_images);

        // 创建文件名 -> ImageData 的映射，用于后续匹配标注
        std::map<QString, ImageData> images_map;
        int                          processed_images = 0;
        int                          skipped_images   = 0;

        for (const auto &image_path : image_files)
        {
            int width  = 0;
            int height = 0;

            // 读取图像尺寸
            if (!getImageDimensions(image_path, width, height))
            {
                spdlog::warn("跳过无法读取尺寸的图像: {}, 原因: 无法读取图像尺寸", image_path.toStdString());
                skipped_images++;
                continue;
            }

            // 提取文件名（不含路径和扩展名）作为键
            QFileInfo file_info(image_path);
            QString   base_name = file_info.baseName(); // 不含扩展名的文件名

            // 创建 ImageData 并添加到映射
            ImageData img_data;
            img_data.image_path   = image_path;
            img_data.image_width  = width;
            img_data.image_height = height;

            images_map[base_name] = img_data;
            processed_images++;

            // 每处理 10% 更新一次进度
            int progress = (processed_images * 40 / total_images);
            if (processed_images % std::max(1, total_images / 10) == 0 || processed_images == total_images)
            {
                updateProgress(progress, QString("已扫描图像: %1/%2").arg(processed_images).arg(total_images));
            }
        }

        spdlog::info("图像扫描完成: 总数={}, 成功读取尺寸={}, 跳过={}", total_images, processed_images, skipped_images);
        updateProgress(40, QString("图像扫描完成: 总数=%1, 成功=%2, 跳过=%3")
                               .arg(total_images)
                               .arg(processed_images)
                               .arg(skipped_images));

        if (images_map.empty())
        {
            spdlog::warn("没有有效的图像可导入");
            updateProgress(100, "没有有效的图像可导入");
            emit dataReady(false, dataset_id, {}, {}, {}, {}, {});
            return;
        }

        // ========== Phase 2: 解析标注并匹配到图像 ==========
        std::vector<LabelMeData> parsed_annotations;

        // 检查 data_dir 是否为空或不存在
        if (data_dir.isEmpty() || !QDir(data_dir).exists())
        {
            spdlog::info("标注目录为空或不存在，将只导入图像（无标注）");
            updateProgress(90, "标注目录为空，只导入图像");

            // 直接处理数据（只有图像，没有标注）
            processAndEmitData(dataset_id, images_map, parsed_annotations);
            updateProgress(100, QString("导入完成: %1 个图像, 0 个标注").arg(images_map.size()));
            return;
        }

        updateProgress(40, "正在扫描标注文件...");
        std::vector<QString> json_files = scanJsonFiles(data_dir);

        int total_json_files = static_cast<int>(json_files.size());
        spdlog::info("找到 {} 个标注文件，开始解析...", total_json_files);

        // 如果没有找到标注文件，直接导入图像
        if (total_json_files == 0)
        {
            spdlog::info("未找到标注文件，将只导入图像（无标注）");
            updateProgress(90, "未找到标注文件，只导入图像");

            // 直接处理数据（只有图像，没有标注）
            processAndEmitData(dataset_id, images_map, parsed_annotations);
            updateProgress(100, QString("导入完成: %1 个图像, 0 个标注").arg(images_map.size()));
            return;
        }

        int parsed_count    = 0;
        int skipped_count   = 0;
        int matched_count   = 0;
        int unmatched_count = 0;

        for (const auto &json_path : json_files)
        {
            LabelMeData data;
            if (!parseLabelMeJson(json_path, data))
            {
                spdlog::warn("跳过无法解析的标注文件: {}, 原因: JSON 解析失败", json_path.toStdString());
                skipped_count++;
                continue;
            }

            // 从标注文件路径提取文件名（不含扩展名）
            // 例如：/path/to/annotations/image001.json -> "image001"
            QFileInfo json_file_info(json_path);
            QString   annotation_base_name = json_file_info.baseName(); // 不含扩展名的标注文件名

            // 使用标注文件名在图像映射表中查找对应的图像
            auto it = images_map.find(annotation_base_name);
            if (it == images_map.end())
            {
                spdlog::warn("标注文件对应的图像不存在，跳过: {}, 期望图像名: {}", json_path.toStdString(),
                             annotation_base_name.toStdString());
                unmatched_count++;
                skipped_count++;
                continue;
            }

            // 使用扫描到的图像信息更新标注数据（覆盖标注中可能错误的 imagePath）
            data.image_path   = it->second.image_path;
            data.image_width  = it->second.image_width;
            data.image_height = it->second.image_height;

            parsed_annotations.push_back(data);
            parsed_count++;
            matched_count++;

            // 每解析 10% 更新一次进度
            int progress = 40 + (parsed_count * 50 / std::max(1, total_json_files));
            if (parsed_count % std::max(1, total_json_files / 10) == 0 || parsed_count == total_json_files)
            {
                updateProgress(progress, QString("已解析标注: %1/%2").arg(parsed_count).arg(total_json_files));
            }
        }

        spdlog::info("标注解析完成: 总文件数={}, 成功解析={}, 成功匹配={}, 未匹配={}, 跳过={}", total_json_files,
                     parsed_count, matched_count, unmatched_count, skipped_count);
        updateProgress(90, QString("标注解析完成: 总文件数=%1, 成功解析=%2, 跳过=%3")
                               .arg(total_json_files)
                               .arg(parsed_count)
                               .arg(skipped_count));

        updateProgress(90, "正在处理数据...");

        // 处理数据并发射 dataReady 信号
        processAndEmitData(dataset_id, images_map, parsed_annotations);

        updateProgress(100, QString("导入完成: %1 个图像, %2 个标注").arg(images_map.size()).arg(parsed_count));
    }
    catch (const std::exception &e)
    {
        spdlog::error("导入过程中发生异常: {}", e.what());
        updateProgress(100, QString("导入失败: %1").arg(e.what()));
        emit dataReady(false, dataset_id, {}, {}, {}, {}, {});
    }
}

std::vector<QString> LabelMeImporter::scanJsonFiles(const QString &data_dir)
{
    std::vector<QString> json_files;

    // 检查目录是否存在
    QDir dir(data_dir);
    if (!dir.exists())
    {
        spdlog::warn("数据目录不存在: {}", data_dir.toStdString());
        return json_files;
    }

    // 使用 QDirIterator 递归扫描目录
    QDirIterator it(data_dir, QStringList() << "*.json", QDir::Files, QDirIterator::Subdirectories);

    while (it.hasNext())
    {
        QString json_path = it.next();
        json_files.push_back(json_path);
    }

    return json_files;
}

std::vector<QString> LabelMeImporter::scanImageFiles(const QString &image_dir)
{
    std::vector<QString> image_files;

    // 检查目录是否存在
    QDir dir(image_dir);
    if (!dir.exists())
    {
        spdlog::warn("图像目录不存在: {}", image_dir.toStdString());
        return image_files;
    }

    // 定义支持的图像格式
    QStringList image_filters;
    image_filters << "*.jpg" << "*.jpeg" << "*.png" << "*.bmp" << "*.gif" << "*.tiff" << "*.tif" << "*.webp"
                  << "*.JPG" << "*.JPEG" << "*.PNG" << "*.BMP" << "*.GIF" << "*.TIFF" << "*.TIF" << "*.WEBP";

    // 使用 QDirIterator 递归扫描目录
    QDirIterator it(image_dir, image_filters, QDir::Files, QDirIterator::Subdirectories);

    while (it.hasNext())
    {
        QString image_path = it.next();
        image_files.push_back(image_path);
    }

    spdlog::info("在目录 {} 中找到 {} 个图像文件", image_dir.toStdString(), image_files.size());

    return image_files;
}

bool LabelMeImporter::getImageDimensions(const QString &image_path, int &width, int &height)
{
    // 使用 QImageReader 读取图像尺寸（不加载完整图像数据）
    QImageReader reader(image_path);
    QSize        size = reader.size();

    if (!size.isValid())
    {
        spdlog::warn("无法读取图像尺寸: {}, 错误: {}", image_path.toStdString(), reader.errorString().toStdString());
        return false;
    }

    width  = size.width();
    height = size.height();

    return true;
}

bool LabelMeImporter::parseLabelMeJson(const QString &json_path, LabelMeData &data)
{
    try
    {
        // 使用 QFile 读取 JSON 文件（支持中文路径）
        QFile file(json_path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            spdlog::error("无法打开 JSON 文件: {}", json_path.toStdString());
            return false;
        }

        // 读取文件内容
        QByteArray json_bytes = file.readAll();
        file.close();

        // 解析 JSON
        nlohmann::json json_data;
        try
        {
            json_data = nlohmann::json::parse(json_bytes.constData(), json_bytes.constData() + json_bytes.size());
        }
        catch (const nlohmann::json::parse_error &e)
        {
            spdlog::error("JSON 解析失败: {}, 错误: {}", json_path.toStdString(), e.what());
            return false;
        }

        // 注意：不使用 JSON 中的 imagePath 字段，因为它可能是错误的
        // 我们将在 doImport() 中基于标注文件名来匹配正确的图像
        // 这里只是初始化为空字符串，稍后会被覆盖
        data.image_path = "";

        // 提取 imageWidth 字段（可选，默认为 0）
        // 注意：这些值也可能不准确，会在匹配图像后被覆盖
        if (json_data.contains("imageWidth") && json_data["imageWidth"].is_number())
        {
            data.image_width = json_data["imageWidth"].get<int>();
        }
        else
        {
            data.image_width = 0;
        }

        // 提取 imageHeight 字段（可选，默认为 0）
        if (json_data.contains("imageHeight") && json_data["imageHeight"].is_number())
        {
            data.image_height = json_data["imageHeight"].get<int>();
        }
        else
        {
            data.image_height = 0;
        }

        // 提取 shapes 数组（可选）
        if (!json_data.contains("shapes"))
        {
            return true; // 没有标注也算成功
        }

        if (!json_data["shapes"].is_array())
        {
            return true;
        }

        // 解析每个 shape
        for (const auto &shape_json : json_data["shapes"])
        {
            LabelMeShape shape;

            // 提取 label 字段（必需）
            if (!shape_json.contains("label"))
            {
                continue;
            }
            shape.label = QString::fromStdString(shape_json["label"].get<std::string>());

            // 提取 shape_type 字段（必需）
            if (!shape_json.contains("shape_type"))
            {
                continue;
            }
            shape.shape_type = QString::fromStdString(shape_json["shape_type"].get<std::string>());

            // 提取 points 数组（必需）
            if (!shape_json.contains("points") || !shape_json["points"].is_array())
            {
                continue;
            }

            // 解析坐标点
            for (const auto &point_json : shape_json["points"])
            {
                if (!point_json.is_array() || point_json.size() < 2)
                {
                    continue;
                }

                double x = point_json[0].get<double>();
                double y = point_json[1].get<double>();
                shape.points.push_back(QPointF(x, y));
            }

            // 如果 shape 有有效的点，添加到数据中
            if (!shape.points.empty())
            {
                data.shapes.push_back(shape);
            }
        }

        return true;
    }
    catch (const std::exception &e)
    {
        spdlog::error("解析 JSON 文件时发生异常: {}, 错误: {}", json_path.toStdString(), e.what());
        return false;
    }
}

std::set<QString> LabelMeImporter::extractLabelClasses(const std::vector<LabelMeData> &all_data)
{
    std::set<QString> label_classes;

    // 遍历所有 LabelMeData，提取标签类别名称
    for (const auto &data : all_data)
    {
        for (const auto &shape : data.shapes)
        {
            // 使用 std::set 自动去重
            label_classes.insert(shape.label);
        }
    }

    return label_classes;
}

QVariantMap LabelMeImporter::convertShapeToLabelData(const LabelMeShape &shape, int image_width, int image_height)
{
    QVariantMap label_data;

    // 检查图像尺寸是否有效
    if (image_width <= 0 || image_height <= 0)
    {
        spdlog::warn("图像尺寸无效: width={}, height={}", image_width, image_height);
        return label_data;
    }

    // 处理 rectangle 类型
    if (shape.shape_type == "rectangle")
    {
        if (shape.points.size() < 2)
        {
            spdlog::warn("rectangle 类型的 shape 点数不足: {}", shape.points.size());
            return label_data;
        }

        QPointF p1 = shape.points[0];
        QPointF p2 = shape.points[1];

        double x_min = std::min(p1.x(), p2.x());
        double y_min = std::min(p1.y(), p2.y());
        double x_max = std::max(p1.x(), p2.x());
        double y_max = std::max(p1.y(), p2.y());

        double width  = x_max - x_min;
        double height = y_max - y_min;

        label_data["x"]      = x_min;
        label_data["y"]      = y_min;
        label_data["width"]  = width;
        label_data["height"] = height;
    }
    else if (shape.shape_type == "polygon")
    {
        if (shape.points.empty())
        {
            spdlog::warn("polygon 类型的 shape 没有点");
            return label_data;
        }

        double x_min = shape.points[0].x();
        double y_min = shape.points[0].y();
        double x_max = shape.points[0].x();
        double y_max = shape.points[0].y();

        for (const auto &point : shape.points)
        {
            x_min = std::min(x_min, point.x());
            y_min = std::min(y_min, point.y());
            x_max = std::max(x_max, point.x());
            y_max = std::max(y_max, point.y());
        }

        double width  = x_max - x_min;
        double height = y_max - y_min;

        label_data["x"]      = x_min;
        label_data["y"]      = y_min;
        label_data["width"]  = width;
        label_data["height"] = height;
    }
    else
    {
        spdlog::warn("不支持的 shape_type: {}, label: {}", shape.shape_type.toStdString(), shape.label.toStdString());
        return label_data;
    }

    return label_data;
}

QString LabelMeImporter::generateDefaultColor(int index)
{
    const double golden_ratio = 0.618033988749895;
    double       hue          = fmod(index * golden_ratio, 1.0);
    QColor       color        = QColor::fromHsvF(hue, 0.8, 0.9);
    return color.name();
}

void LabelMeImporter::processAndEmitData(int64_t dataset_id, const std::map<QString, ImageData> &images,
                                         const std::vector<LabelMeData> &parsed_annotations)
{
    spdlog::info("开始处理数据并准备发射 dataReady 信号");

    // 准备数据结构
    std::vector<QString>       image_paths;
    std::vector<int64_t>       image_widths;
    std::vector<int64_t>       image_heights;
    std::map<QString, QString> label_class_info; // name -> color
    std::vector<ImportedLabel> labels;

    // 1. 收集所有图像信息
    // 重要：为了确保 image_id 和标注数据的对应关系正确，
    // 我们需要按照标注数据中引用的顺序来收集图像
    std::set<QString> images_with_annotations; // 记录已经添加的图像

    // 1.1 先添加有标注的图像（按标注顺序）
    for (const auto &annotation : parsed_annotations)
    {
        if (images_with_annotations.find(annotation.image_path) == images_with_annotations.end())
        {
            image_paths.push_back(annotation.image_path);
            image_widths.push_back(annotation.image_width);
            image_heights.push_back(annotation.image_height);
            images_with_annotations.insert(annotation.image_path);
        }
    }

    // 1.2 再添加没有标注的图像
    for (const auto &pair : images)
    {
        const ImageData &img_data = pair.second;
        if (images_with_annotations.find(img_data.image_path) == images_with_annotations.end())
        {
            image_paths.push_back(img_data.image_path);
            image_widths.push_back(img_data.image_width);
            image_heights.push_back(img_data.image_height);
        }
    }

    // 2. 从标注中提取标签类别
    std::set<QString> label_class_names;
    for (const auto &annotation : parsed_annotations)
    {
        for (const auto &shape : annotation.shapes)
        {
            label_class_names.insert(shape.label);
        }
    }

    // 3. 为标签类别生成颜色
    int color_index = 0;
    for (const auto &class_name : label_class_names)
    {
        QString color                = generateDefaultColor(color_index++);
        label_class_info[class_name] = color;
        spdlog::debug("标签类别: {}, 颜色: {}", class_name.toStdString(), color.toStdString());
    }

    // 4. 处理每个标注的数据
    for (const auto &annotation : parsed_annotations)
    {
        // 转换每个形状为标注数据
        for (const auto &shape : annotation.shapes)
        {
            QVariantMap label_data = convertShapeToLabelData(shape, annotation.image_width, annotation.image_height);

            // 如果转换失败（返回空映射），跳过该标注
            if (label_data.isEmpty())
            {
                spdlog::warn("跳过无效的标注: label={}, shape_type={}, image={}", shape.label.toStdString(),
                             shape.shape_type.toStdString(), annotation.image_path.toStdString());
                continue;
            }

            // 创建 ImportedLabel 结构
            ImportedLabel imported_label;
            imported_label.label_class_name = shape.label;
            imported_label.data             = label_data;
            imported_label.image_path       = annotation.image_path;

            labels.push_back(imported_label);
        }
    }

    spdlog::info("数据处理完成: images={}, label_classes={}, labels={}", image_paths.size(), label_class_info.size(),
                 labels.size());

    // 5. 发射 dataReady 信号
    emit dataReady(true, dataset_id, image_paths, image_widths, image_heights, label_class_info, labels);
}

} // namespace dltool::data
