#include "data/COCOImporter.h"

#include "database/DataBase.h"

#include <json.hpp>
#include <spdlog/spdlog.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <set>

namespace dltool::data {

namespace {

bool readJsonFile(const QString &json_path, nlohmann::json &json_data)
{
    QFile file(json_path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        spdlog::error("无法打开 JSON 文件: {}", json_path.toStdString());
        return false;
    }

    try
    {
        const QByteArray json_bytes = file.readAll();
        json_data = nlohmann::json::parse(json_bytes.constData(), json_bytes.constData() + json_bytes.size());
        return true;
    }
    catch (const std::exception &e)
    {
        spdlog::error("解析 JSON 文件失败: {}, 错误: {}", json_path.toStdString(), e.what());
        return false;
    }
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
    nlohmann::json json_data;
    if (!readJsonFile(json_path, json_data) || !json_data.is_object())
    {
        return false;
    }

    return json_data.contains("images") && json_data["images"].is_array() && json_data.contains("annotations")
           && json_data["annotations"].is_array() && json_data.contains("categories")
           && json_data["categories"].is_array();
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
        updateProgress(0, QStringLiteral("正在查找 COCO 标注文件..."));
        const QString coco_json_path = findCocoJsonFile(data_dir);
        if (coco_json_path.isEmpty())
        {
            updateProgress(100, QStringLiteral("未找到有效的 COCO 标注文件"));
            emit dataReady(false, dataset_id, {}, {}, {}, {}, {});
            emit importFinished(false, {}, {});
            return;
        }

        nlohmann::json json_data;
        if (!readJsonFile(coco_json_path, json_data))
        {
            updateProgress(100, QStringLiteral("COCO 标注文件解析失败"));
            emit dataReady(false, dataset_id, {}, {}, {}, {}, {});
            emit importFinished(false, {}, {});
            return;
        }

        updateProgress(10, QStringLiteral("正在索引图像文件..."));
        std::map<QString, QString> image_file_index;
        for (const QString &image_path : DatasetIO::scanImageFiles(image_dir))
        {
            image_file_index[QFileInfo(image_path).fileName()] = image_path;
        }

        std::map<int64_t, CocoCategory> categories_by_id;
        int                             color_index = 0;
        std::map<QString, QString>      label_class_info;
        for (const auto &category_json : json_data["categories"])
        {
            if (!category_json.contains("id") || !category_json.contains("name") || !category_json["name"].is_string())
            {
                continue;
            }

            int64_t category_id = 0;
            if (!jsonToInt64(category_json["id"], category_id))
            {
                continue;
            }

            CocoCategory category;
            category.id   = category_id;
            category.name = QString::fromStdString(category_json["name"].get<std::string>());
            if (category.name.isEmpty())
            {
                continue;
            }

            categories_by_id[category.id] = category;
            if (label_class_info.find(category.name) == label_class_info.end())
            {
                label_class_info[category.name] = DatasetIO::generateDefaultColor(color_index++);
            }
        }

        updateProgress(25, QStringLiteral("正在解析 COCO 图像列表..."));
        std::vector<CocoImage>          images;
        std::map<int64_t, CocoImage>    images_by_coco_id;
        const auto                     &image_array = json_data["images"];
        const int                       total_images = static_cast<int>(image_array.size());
        int                             skipped_images = 0;

        for (const auto &image_json : image_array)
        {
            if (!image_json.contains("id") || !image_json.contains("file_name") || !image_json["file_name"].is_string())
            {
                skipped_images++;
                continue;
            }

            int64_t coco_image_id = 0;
            if (!jsonToInt64(image_json["id"], coco_image_id))
            {
                skipped_images++;
                continue;
            }

            CocoImage image;
            image.coco_id   = coco_image_id;
            image.file_name = QString::fromStdString(image_json["file_name"].get<std::string>());
            image.image_path = resolveImagePath(image_dir, image.file_name, image_file_index);
            if (image.image_path.isEmpty())
            {
                spdlog::warn("COCO 图像文件不存在，跳过: {}", image.file_name.toStdString());
                skipped_images++;
                continue;
            }

            image.width  = image_json.contains("width") ? static_cast<int>(jsonToDouble(image_json["width"])) : 0;
            image.height = image_json.contains("height") ? static_cast<int>(jsonToDouble(image_json["height"])) : 0;
            if (image.width <= 0 || image.height <= 0)
            {
                int real_width  = 0;
                int real_height = 0;
                if (!DatasetIO::getImageDimensions(image.image_path, real_width, real_height))
                {
                    skipped_images++;
                    continue;
                }
                image.width  = real_width;
                image.height = real_height;
            }

            images.push_back(image);
            images_by_coco_id[image.coco_id] = image;

            const int processed = static_cast<int>(images.size());
            if (processed % std::max(1, total_images / 10) == 0 || processed == total_images)
            {
                updateProgress(25 + processed * 35 / std::max(1, total_images),
                               QStringLiteral("已解析 COCO 图像 %1/%2").arg(processed).arg(total_images));
            }
        }

        if (images.empty())
        {
            updateProgress(100, QStringLiteral("COCO 数据中没有可导入的有效图像"));
            emit dataReady(false, dataset_id, {}, {}, {}, {}, {});
            emit importFinished(false, {}, {});
            return;
        }

        updateProgress(60, QStringLiteral("正在解析 COCO 标注..."));
        std::vector<ImportedLabel> labels;
        const auto                &annotation_array = json_data["annotations"];
        const int                  total_annotations = static_cast<int>(annotation_array.size());
        int                        skipped_annotations = 0;

        for (const auto &annotation_json : annotation_array)
        {
            if (!annotation_json.contains("image_id") || !annotation_json.contains("category_id")
                || !annotation_json.contains("bbox") || !annotation_json["bbox"].is_array()
                || annotation_json["bbox"].size() < 4)
            {
                skipped_annotations++;
                continue;
            }

            int64_t coco_image_id = 0;
            int64_t category_id   = 0;
            if (!jsonToInt64(annotation_json["image_id"], coco_image_id)
                || !jsonToInt64(annotation_json["category_id"], category_id))
            {
                skipped_annotations++;
                continue;
            }

            auto image_it = images_by_coco_id.find(coco_image_id);
            auto class_it = categories_by_id.find(category_id);
            if (image_it == images_by_coco_id.end() || class_it == categories_by_id.end())
            {
                skipped_annotations++;
                continue;
            }

            const auto &bbox = annotation_json["bbox"];
            const QVariantMap label_data
                = DatasetIO::bboxToLabelData(jsonToDouble(bbox[0]), jsonToDouble(bbox[1]), jsonToDouble(bbox[2]),
                                             jsonToDouble(bbox[3]), image_it->second.width, image_it->second.height);
            if (label_data.isEmpty())
            {
                skipped_annotations++;
                continue;
            }

            ImportedLabel label;
            label.label_class_name = class_it->second.name;
            label.data             = label_data;
            label.image_path       = image_it->second.image_path;
            labels.push_back(label);

            const int processed = static_cast<int>(labels.size() + skipped_annotations);
            if (processed % std::max(1, total_annotations / 10) == 0 || processed == total_annotations)
            {
                updateProgress(60 + processed * 30 / std::max(1, total_annotations),
                               QStringLiteral("已解析 COCO 标注 %1/%2").arg(processed).arg(total_annotations));
            }
        }

        std::vector<QString> image_paths;
        std::vector<int64_t> image_widths;
        std::vector<int64_t> image_heights;
        image_paths.reserve(images.size());
        image_widths.reserve(images.size());
        image_heights.reserve(images.size());
        for (const CocoImage &image : images)
        {
            image_paths.push_back(image.image_path);
            image_widths.push_back(image.width);
            image_heights.push_back(image.height);
        }

        updateProgress(100, QStringLiteral("导入完成: %1 个图像, %2 个标注，跳过图像 %3 个，跳过标注 %4 个")
                                .arg(images.size())
                                .arg(labels.size())
                                .arg(skipped_images)
                                .arg(skipped_annotations));
        emit dataReady(true, dataset_id, image_paths, image_widths, image_heights, label_class_info, labels);
        emit importFinished(true, {}, {});
    }
    catch (const std::exception &e)
    {
        spdlog::error("COCO 导入过程中发生异常: {}", e.what());
        updateProgress(100, QStringLiteral("COCO 导入失败: %1").arg(e.what()));
        emit dataReady(false, dataset_id, {}, {}, {}, {}, {});
        emit importFinished(false, {}, {});
    }
}

} // namespace dltool::data
