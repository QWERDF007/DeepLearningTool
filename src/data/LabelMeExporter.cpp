#include "data/LabelMeExporter.h"

#include <json.hpp>
#include <spdlog/spdlog.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <map>

namespace dltool::data {

namespace {

QString uniqueLabelMeImageName(const QString &source_path, int64_t stable_id, const std::map<QString, int> &used_names,
                               const std::map<QString, int> &used_stems)
{
    const QFileInfo file_info(source_path);
    const QString   suffix = file_info.suffix().isEmpty() ? QString() : QStringLiteral(".%1").arg(file_info.suffix());
    const QString   stem = file_info.completeBaseName().isEmpty() ? QString::number(stable_id)
                                                                  : file_info.completeBaseName();
    QString         candidate = QStringLiteral("%1%2").arg(stem, suffix);
    if (used_names.find(candidate) == used_names.end() && used_stems.find(stem) == used_stems.end())
    {
        return candidate;
    }

    QString candidate_stem = QStringLiteral("%1_%2").arg(stem).arg(stable_id);
    candidate              = QStringLiteral("%1%2").arg(candidate_stem, suffix);
    int index              = 1;
    while (used_names.find(candidate) != used_names.end() || used_stems.find(candidate_stem) != used_stems.end())
    {
        candidate_stem = QStringLiteral("%1_%2_%3").arg(stem).arg(stable_id).arg(index++);
        candidate      = QStringLiteral("%1%2").arg(candidate_stem, suffix);
    }
    return candidate;
}

} // namespace

LabelMeExporter::LabelMeExporter(QObject *parent)
    : DataExporter(parent)
{
}

LabelMeExporter::~LabelMeExporter() {}

void LabelMeExporter::startExport(const ExportDataset &dataset, const QString &output_dir)
{
    QThread *worker_thread = new QThread();

    connect(
        worker_thread, &QThread::started, this, [this, dataset, output_dir]() { doExport(dataset, output_dir); },
        Qt::DirectConnection);

    connect(this, &LabelMeExporter::exportFinished, worker_thread, &QThread::quit);
    connect(worker_thread, &QThread::finished, worker_thread, &QThread::deleteLater);

    worker_thread->start();
}

void LabelMeExporter::doExport(ExportDataset dataset, QString output_dir)
{
    try
    {
        QString err_msg;
        const QString images_dir      = QDir(output_dir).filePath(QStringLiteral("images"));
        const QString annotations_dir = QDir(output_dir).filePath(QStringLiteral("annotations"));
        if (!DatasetIO::ensureDirectory(images_dir, err_msg) || !DatasetIO::ensureDirectory(annotations_dir, err_msg))
        {
            emit exportFinished(false, err_msg);
            return;
        }

        std::map<QString, int>     used_image_names;
        std::map<QString, int>     used_image_stems;
        std::map<int64_t, QString> image_name_by_id;
        const int image_count = static_cast<int>(dataset.images.size());

        for (int i = 0; i < image_count; ++i)
        {
            const ExportImage &image = dataset.images[i];
            const QString      file_name
                = uniqueLabelMeImageName(image.path, image.image_id, used_image_names, used_image_stems);
            used_image_names[file_name]++;
            used_image_stems[QFileInfo(file_name).completeBaseName()]++;
            image_name_by_id[image.image_id] = file_name;

            const QString target_path = QDir(images_dir).filePath(file_name);
            if (!DatasetIO::copyFile(image.path, target_path, err_msg))
            {
                emit exportFinished(false, err_msg);
                return;
            }

            if ((i + 1) % std::max(1, image_count / 10) == 0 || i + 1 == image_count)
            {
                updateProgress((i + 1) * 45 / std::max(1, image_count),
                               QStringLiteral("已复制图像 %1/%2").arg(i + 1).arg(image_count));
            }
        }

        std::map<int64_t, QString> class_name_by_id;
        for (const ExportLabelClass &label_class : dataset.label_classes)
        {
            class_name_by_id[label_class.id] = label_class.name;
        }

        std::map<int64_t, std::vector<ExportLabel>> labels_by_image_id;
        for (const ExportLabel &label : dataset.labels)
        {
            labels_by_image_id[label.image_id].push_back(label);
        }

        for (int i = 0; i < image_count; ++i)
        {
            const ExportImage &image = dataset.images[i];
            const QString      image_name = image_name_by_id[image.image_id];

            nlohmann::json json_data;
            json_data["version"]     = "5.0.1";
            json_data["flags"]       = nlohmann::json::object();
            json_data["shapes"]      = nlohmann::json::array();
            json_data["imagePath"]   = image_name.toStdString();
            json_data["imageData"]   = nullptr;
            json_data["imageHeight"] = image.height;
            json_data["imageWidth"]  = image.width;

            for (const ExportLabel &label : labels_by_image_id[image.image_id])
            {
                const double x = label.data.value(QStringLiteral("x")).toDouble();
                const double y = label.data.value(QStringLiteral("y")).toDouble();
                const double w = label.data.value(QStringLiteral("width")).toDouble();
                const double h = label.data.value(QStringLiteral("height")).toDouble();
                if (w <= 0 || h <= 0)
                {
                    continue;
                }

                nlohmann::json shape;
                shape["label"]       = class_name_by_id[label.label_class_id].toStdString();
                shape["group_id"]    = nullptr;
                shape["description"] = "";
                shape["flags"]       = nlohmann::json::object();

                const std::vector<QPointF> points = DatasetIO::variantListToPoints(label.data.value(QStringLiteral("points")));
                if (points.size() >= 3)
                {
                    nlohmann::json point_array = nlohmann::json::array();
                    for (const QPointF &point : points)
                    {
                        point_array.push_back({point.x(), point.y()});
                    }
                    shape["points"]     = point_array;
                    shape["shape_type"] = "polygon";
                }
                else
                {
                    shape["points"]     = {{x, y}, {x + w, y + h}};
                    shape["shape_type"] = "rectangle";
                }
                json_data["shapes"].push_back(shape);
            }

            const QString annotation_name
                = QStringLiteral("%1.json").arg(QFileInfo(image_name).completeBaseName());
            QFile annotation_file(QDir(annotations_dir).filePath(annotation_name));
            if (!annotation_file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
            {
                emit exportFinished(false, QStringLiteral("无法写入标注文件: %1").arg(annotation_file.fileName()));
                return;
            }

            const QByteArray json_bytes = QByteArray::fromStdString(json_data.dump(2));
            annotation_file.write(json_bytes);

            const int progress = 45 + (i + 1) * 55 / std::max(1, image_count);
            if ((i + 1) % std::max(1, image_count / 10) == 0 || i + 1 == image_count)
            {
                updateProgress(progress, QStringLiteral("已写入 LabelMe 标注 %1/%2").arg(i + 1).arg(image_count));
            }
        }

        emit exportFinished(true, QStringLiteral("LabelMe 导出完成: %1 个图像, %2 个标注")
                                      .arg(dataset.images.size())
                                      .arg(dataset.labels.size()));
    }
    catch (const std::exception &e)
    {
        spdlog::error("LabelMe 导出失败: {}", e.what());
        emit exportFinished(false, QStringLiteral("LabelMe 导出失败: %1").arg(e.what()));
    }
}

} // namespace dltool::data
