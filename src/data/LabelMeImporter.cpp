#include "data/LabelMeImporter.h"

#include "data/DataBase.h"
#include "data/LabelData.h"

#include <json.hpp>
#include <spdlog/spdlog.h>

#include <QColor>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <algorithm>
#include <fstream>
#include <memory>

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

    bool                     success = false;
    std::vector<LabelMeData> parsed_data;
    std::set<QString>        label_class_names;

    try
    {
        // 1. 扫描 JSON 文件
        updateProgress(0, "正在扫描 JSON 文件...");
        std::vector<QString> json_files = scanJsonFiles(data_dir);

        if (json_files.empty())
        {
            spdlog::warn("未找到任何 JSON 文件");
            updateProgress(100, "未找到任何 JSON 文件");
            emit dataReady(false, dataset_id, {}, {}, {}, {}, {});
            return;
        }

        int total_files = static_cast<int>(json_files.size());
        spdlog::info("找到 {} 个 JSON 文件，开始解析...", total_files);

        // 2. 解析所有 JSON 文件
        updateProgress(10, QString("正在解析 %1 个 JSON 文件...").arg(total_files));
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

            // 验证图像文件路径
            QString image_path;
            if (QFileInfo(data.image_path).isAbsolute())
            {
                image_path = data.image_path;
            }
            else
            {
                // 相对路径：相对于 JSON 文件所在目录或 image_dir
                QFileInfo json_file_info(json_path);
                QString   json_dir = json_file_info.absolutePath();

                // 首先尝试相对于 JSON 文件目录
                QString candidate1 = QDir(json_dir).filePath(data.image_path);
                if (QFileInfo::exists(candidate1))
                {
                    image_path = candidate1;
                }
                else
                {
                    // 然后尝试相对于 image_dir
                    QString candidate2 = QDir(image_dir).filePath(data.image_path);
                    if (QFileInfo::exists(candidate2))
                    {
                        image_path = candidate2;
                    }
                    else
                    {
                        // 最后尝试只使用文件名在 image_dir 中查找
                        QString filename   = QFileInfo(data.image_path).fileName();
                        QString candidate3 = QDir(image_dir).filePath(filename);
                        if (QFileInfo::exists(candidate3))
                        {
                            image_path = candidate3;
                        }
                        else
                        {
                            skipped_count++;
                            continue;
                        }
                    }
                }
            }

            // 更新为完整路径
            data.image_path = image_path;

            // 如果 JSON 中没有尺寸信息，从图像文件读取
            if (data.image_width <= 0 || data.image_height <= 0)
            {
                QImageReader reader(image_path);
                QSize        size = reader.size();
                if (size.isValid())
                {
                    data.image_width  = size.width();
                    data.image_height = size.height();
                }
                else
                {
                    skipped_count++;
                    continue;
                }
            }

            parsed_data.push_back(data);
            parsed_count++;

            // 每解析 10% 更新一次进度
            int progress = 10 + (parsed_count * 80 / total_files);
            if (parsed_count % std::max(1, total_files / 10) == 0 || parsed_count == total_files)
            {
                updateProgress(progress, QString("已解析: %1/%2").arg(parsed_count).arg(total_files));
            }
        }

        // 汇总解析结果
        spdlog::info("解析完成: 总文件数={}, 成功解析={}, 跳过={}", total_files, parsed_count, skipped_count);

        if (parsed_data.empty())
        {
            spdlog::warn("没有有效的数据可导入");
            updateProgress(100, "没有有效的数据可导入");
            emit dataReady(false, dataset_id, {}, {}, {}, {}, {});
            return;
        }

        // 3. 提取标签类别
        updateProgress(90, "正在提取标签类别...");
        label_class_names = extractLabelClasses(parsed_data);

        spdlog::info("提取到 {} 个唯一的标签类别", label_class_names.size());

        success = true;
        updateProgress(100, QString("解析完成: %1 个图像").arg(parsed_data.size()));

        // 处理数据并发射 dataReady 信号
        processAndEmitData(dataset_id, parsed_data, label_class_names);
    }

    catch (const std::exception &e)
    {
        spdlog::error("解析过程中发生异常: {}", e.what());
        updateProgress(100, QString("解析失败: %1").arg(e.what()));
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

        // 提取 imagePath 字段（必需）
        if (!json_data.contains("imagePath"))
        {
            return false;
        }
        data.image_path = QString::fromStdString(json_data["imagePath"].get<std::string>());

        // 提取 imageWidth 字段（可选，默认为 0）
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

void LabelMeImporter::processAndEmitData(int64_t dataset_id, const std::vector<LabelMeData> &parsed_data,
                                         const std::set<QString> &label_class_names)
{
    spdlog::info("开始处理数据并准备发射 dataReady 信号");

    // 准备数据结构
    std::vector<QString>       image_paths;
    std::vector<int64_t>       image_widths;
    std::vector<int64_t>       image_heights;
    std::map<QString, QString> label_class_info; // name -> color
    std::vector<ImportedLabel> labels;

    // 1. 为标签类别生成颜色
    int color_index = 0;
    for (const auto &class_name : label_class_names)
    {
        QString color                = generateDefaultColor(color_index++);
        label_class_info[class_name] = color;
        spdlog::debug("标签类别: {}, 颜色: {}", class_name.toStdString(), color.toStdString());
    }

    // 2. 处理每个图像的数据
    for (const auto &data : parsed_data)
    {
        // 添加图像信息
        image_paths.push_back(data.image_path);
        image_widths.push_back(data.image_width);
        image_heights.push_back(data.image_height);

        // 3. 转换每个形状为标注数据
        for (const auto &shape : data.shapes)
        {
            QVariantMap label_data = convertShapeToLabelData(shape, data.image_width, data.image_height);

            // 如果转换失败（返回空映射），跳过该标注
            if (label_data.isEmpty())
            {
                spdlog::warn("跳过无效的标注: label={}, shape_type={}, image={}", shape.label.toStdString(),
                             shape.shape_type.toStdString(), data.image_path.toStdString());
                continue;
            }

            // 创建 ImportedLabel 结构
            ImportedLabel imported_label;
            imported_label.label_class_name = shape.label;
            imported_label.data             = label_data;
            imported_label.image_path       = data.image_path;

            labels.push_back(imported_label);
        }
    }

    spdlog::info("数据处理完成: images={}, label_classes={}, labels={}", image_paths.size(), label_class_info.size(),
                 labels.size());

    // 4. 发射 dataReady 信号
    emit dataReady(true, dataset_id, image_paths, image_widths, image_heights, label_class_info, labels);
}

} // namespace dltool::data
