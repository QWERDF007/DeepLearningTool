#include "data/MaskExporter.h"

#include "data/DatasetIO.h"

#include <json.hpp>
#include <spdlog/spdlog.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QPolygonF>
#include <algorithm>
#include <map>

namespace dltool::data {

namespace {

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

QString uniqueMaskImageName(const QString &source_path, int64_t stable_id, const std::map<QString, int> &used_names,
                            const std::map<QString, int> &used_stems)
{
    const QFileInfo file_info(source_path);
    const QString   suffix = file_info.suffix().isEmpty() ? QString() : QString(".%1").arg(file_info.suffix());
    const QString   stem
        = file_info.completeBaseName().isEmpty() ? QString::number(stable_id) : file_info.completeBaseName();
    QString candidate = QString("%1%2").arg(stem, suffix);
    if (used_names.find(candidate) == used_names.end() && used_stems.find(stem) == used_stems.end())
    {
        return candidate;
    }

    QString candidate_stem = QString("%1_%2").arg(stem).arg(stable_id);
    candidate              = QString("%1%2").arg(candidate_stem, suffix);
    int index              = 1;
    while (used_names.find(candidate) != used_names.end() || used_stems.find(candidate_stem) != used_stems.end())
    {
        candidate_stem = QString("%1_%2_%3").arg(stem).arg(stable_id).arg(index++);
        candidate      = QString("%1%2").arg(candidate_stem, suffix);
    }
    return candidate;
}

QPolygonF variantPointsToPolygon(const QVariant &value)
{
    QPolygonF polygon;
    for (const QPointF &point : DatasetIO::variantListToPoints(value))
    {
        polygon << point;
    }
    return polygon;
}

bool paintLabelToMask(QImage &mask, const QVariantMap &label_data, int value)
{
    if (mask.isNull() || value <= 0)
    {
        return false;
    }

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
    {
        return false;
    }

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
        {
            continue;
        }

        json_data["classes"].push_back({
            {   "value",             value_it->second},
            {      "id",             label_class.id},
            {    "name", label_class.name.toStdString()},
            {   "color", label_class.color.toStdString()},
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

MaskExporter::MaskExporter(QObject *parent)
    : DataExporter(parent)
{
}

MaskExporter::~MaskExporter() = default;

void MaskExporter::startExport(const ExportDataset &dataset, const QString &output_dir, const QVariantMap &options)
{
    QThread *worker_thread = new QThread();

    connect(
        worker_thread, &QThread::started, this,
        [this, dataset, output_dir, options]() { doExport(dataset, output_dir, options); }, Qt::DirectConnection);

    connect(this, &MaskExporter::exportFinished, worker_thread, &QThread::quit);
    connect(worker_thread, &QThread::finished, worker_thread, &QThread::deleteLater);

    worker_thread->start();
}

void MaskExporter::doExport(ExportDataset dataset, QString output_dir, QVariantMap options)
{
    try
    {
        QString       err_msg;
        const QString images_dir = QDir(output_dir).filePath(QStringLiteral("images"));
        const QString masks_dir  = QDir(output_dir).filePath(QStringLiteral("masks"));
        if (!DatasetIO::ensureDirectory(images_dir, err_msg) || !DatasetIO::ensureDirectory(masks_dir, err_msg))
        {
            emit exportFinished(false, err_msg);
            return;
        }

        const MaskOutputMode mode = maskOutputModeFromOptions(options);
        if (mode == MaskOutputMode::ClassIndex && dataset.label_classes.size() > 255)
        {
            emit exportFinished(false, QString("Mask 按类别导出最多支持 255 个类别，当前 %1 个")
                                           .arg(dataset.label_classes.size()));
            return;
        }

        std::map<int64_t, int> class_values;
        for (size_t i = 0; i < dataset.label_classes.size(); ++i)
        {
            const ExportLabelClass &label_class = dataset.label_classes[i];
            class_values[label_class.id]
                = mode == MaskOutputMode::ClassIndex ? static_cast<int>(i) + 1 : 255;
        }

        std::map<QString, int>     used_image_names;
        std::map<QString, int>     used_image_stems;
        std::map<int64_t, QString> image_name_by_id;
        const int                  image_count = static_cast<int>(dataset.images.size());

        for (int i = 0; i < image_count; ++i)
        {
            const ExportImage &image     = dataset.images[i];
            const QString      file_name = uniqueMaskImageName(image.path, image.image_id, used_image_names,
                                                               used_image_stems);
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
                updateProgress((i + 1) * 40 / std::max(1, image_count),
                               QString("已复制图像 %1/%2").arg(i + 1).arg(image_count));
            }
        }

        std::map<int64_t, std::vector<ExportLabel>> labels_by_image_id;
        for (const ExportLabel &label : dataset.labels)
        {
            labels_by_image_id[label.image_id].push_back(label);
        }

        int written_label_count = 0;
        int skipped_label_count = 0;
        for (int i = 0; i < image_count; ++i)
        {
            const ExportImage &image = dataset.images[i];
            int                width = image.width;
            int                height = image.height;
            if ((width <= 0 || height <= 0) && !DatasetIO::getImageDimensions(image.path, width, height))
            {
                emit exportFinished(false, QString("无法读取图像尺寸，不能导出 Mask: %1").arg(image.path));
                return;
            }

            QImage mask(width, height, QImage::Format_ARGB32);
            mask.fill(Qt::black);

            for (const ExportLabel &label : labels_by_image_id[image.image_id])
            {
                const auto value_it = class_values.find(label.label_class_id);
                if (value_it == class_values.end())
                {
                    ++skipped_label_count;
                    continue;
                }

                if (paintLabelToMask(mask, label.data, value_it->second))
                {
                    ++written_label_count;
                }
                else
                {
                    ++skipped_label_count;
                }
            }

            const QString image_name = image_name_by_id[image.image_id];
            const QString mask_name  = QString("%1.png").arg(QFileInfo(image_name).completeBaseName());
            const QImage  gray_mask  = mask.convertToFormat(QImage::Format_Grayscale8);
            if (!gray_mask.save(QDir(masks_dir).filePath(mask_name), "PNG"))
            {
                emit exportFinished(false, QString("写入 Mask 失败: %1").arg(mask_name));
                return;
            }

            if ((i + 1) % std::max(1, image_count / 10) == 0 || i + 1 == image_count)
            {
                const int progress = 40 + (i + 1) * 55 / std::max(1, image_count);
                updateProgress(progress, QString("已写入 Mask %1/%2").arg(i + 1).arg(image_count));
            }
        }

        if (!writeClassMetadata(dataset, output_dir, mode, class_values, err_msg))
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
