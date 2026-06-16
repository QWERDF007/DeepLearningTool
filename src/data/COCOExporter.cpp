#include "data/COCOExporter.h"

#include <json.hpp>
#include <spdlog/spdlog.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <map>

namespace dltool::data {

namespace {

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

} // namespace

COCOExporter::COCOExporter(QObject *parent)
    : DataExporter(parent)
{
}

COCOExporter::~COCOExporter() {}

void COCOExporter::startExport(const ExportDataset &dataset, const QString &output_dir)
{
    QThread *worker_thread = new QThread();

    connect(
        worker_thread, &QThread::started, this, [this, dataset, output_dir]() { doExport(dataset, output_dir); },
        Qt::DirectConnection);

    connect(this, &COCOExporter::exportFinished, worker_thread, &QThread::quit);
    connect(worker_thread, &QThread::finished, worker_thread, &QThread::deleteLater);

    worker_thread->start();
}

void COCOExporter::doExport(ExportDataset dataset, QString output_dir)
{
    try
    {
        QString       err_msg;
        const QString images_dir      = QDir(output_dir).filePath(QStringLiteral("images"));
        const QString annotations_dir = QDir(output_dir).filePath(QStringLiteral("annotations"));
        if (!DatasetIO::ensureDirectory(images_dir, err_msg) || !DatasetIO::ensureDirectory(annotations_dir, err_msg))
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
            {
                updateProgress((i + 1) * 45 / std::max(1, image_count),
                               QString("已复制图像 %1/%2").arg(i + 1).arg(image_count));
            }
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
            {
                continue;
            }

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
                {
                    area = w * h;
                }
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
            {
                updateProgress(45 + (i + 1) * 45 / std::max(1, label_count),
                               QString("已写入 COCO 标注 %1/%2").arg(i + 1).arg(label_count));
            }
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

} // namespace dltool::data
