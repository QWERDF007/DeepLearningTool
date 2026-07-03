#include "data/MaskImporter.h"

#include "common/MaskPolygonUtils.h"
#include "core/CoreDef.h"
#include "data/DataNameUtils.h"
#include "data/DatasetIO.h"

#include <spdlog/spdlog.h>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QTextStream>
#include <QThread>
#include <algorithm>
#include <map>

namespace dltool::data {

namespace {

constexpr int kMaskThreshold = 128;

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
    return QRect(QPoint(x_min, y_min), QPoint(x_max + 1, y_max + 1));
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
                    image_by_stem[alias] = path;
                continue;
            }

            image_by_stem[QFileInfo(line).completeBaseName()] = line;
        }
        return image_by_stem;
    }

    const std::vector<QString> image_files = DatasetIO::scanImageFiles(image_dir);
    for (const QString &image_path : image_files) image_by_stem[QFileInfo(image_path).completeBaseName()] = image_path;
    return image_by_stem;
}

} // namespace

MaskImporter::MaskImporter(dltool::database::ProjectDataBase *database, QObject *parent)
    : DataImporter(database, parent)
{
}

MaskImporter::~MaskImporter() = default;

void MaskImporter::startImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir)
{
    QThread *worker_thread = new QThread();
    connect(
        worker_thread, &QThread::started, this,
        [this, dataset_id, image_dir, data_dir]() { doImport(dataset_id, image_dir, data_dir); }, Qt::DirectConnection);
    connect(this, &MaskImporter::importFinished, worker_thread, &QThread::quit);
    connect(worker_thread, &QThread::finished, worker_thread, &QThread::deleteLater);
    worker_thread->start();
}

void MaskImporter::doImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir)
{
    try
    {
        updateProgress(0, QString("正在扫描图像和 Mask..."));
        const std::map<QString, QString> image_by_stem = loadImageMap(image_dir);
        const std::vector<QString>       mask_files    = scanMaskFiles(data_dir);
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

        int processed_masks = 0;
        int valid_masks     = 0;
        int skipped_masks   = 0;

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

        for (const QString &mask_path : mask_files)
        {
            if (isCancelRequested())
            {
                emit importFinished(false, {}, {});
                return;
            }

            ++processed_masks;
            const QString mask_stem  = QFileInfo(mask_path).completeBaseName();
            const QString image_stem = imageStemForMask(mask_path, data_dir, mask_stem);
            const auto    image_it   = image_by_stem.find(image_stem);
            if (image_it == image_by_stem.end())
            {
                ++skipped_masks;
                spdlog::warn("Mask 未找到匹配图像，跳过: {}", mask_path.toUtf8().constData());
                continue;
            }

            int image_width  = 0;
            int image_height = 0;
            if (!DatasetIO::getImageDimensions(image_it->second, image_width, image_height))
            {
                ++skipped_masks;
                continue;
            }

            MaskGeometry geometry;
            if (!readMaskGeometry(mask_path, geometry))
            {
                ++skipped_masks;
                continue;
            }

            const QVariantMap label_data = maskToLabelData(geometry, image_width, image_height);
            if (label_data.isEmpty())
            {
                ++skipped_masks;
                continue;
            }

            const QString label_class_name = sanitizeName(labelClassNameForMask(mask_path, data_dir));
            if (label_class_name.isEmpty())
            {
                ++skipped_masks;
                continue;
            }

            if (label_class_colors.find(label_class_name) == label_class_colors.end())
            {
                label_class_colors[label_class_name]
                    = DatasetIO::generateDefaultColor(static_cast<int>(label_class_colors.size()));
            }
            batch_label_class_info[label_class_name] = label_class_colors[label_class_name];

            batch_image_paths.push_back(image_it->second);
            batch_image_widths.push_back(image_width);
            batch_image_heights.push_back(image_height);

            ImportedLabel imported_label;
            imported_label.label_class_name = label_class_name;
            imported_label.image_path       = image_it->second;
            imported_label.data             = label_data;
            batch_labels.push_back(imported_label);
            ++valid_masks;

            if (processed_masks % std::max<int>(1, static_cast<int>(mask_files.size()) / 10) == 0
                || processed_masks == static_cast<int>(mask_files.size()))
            {
                const int progress = 10 + processed_masks * 80 / std::max<int>(1, static_cast<int>(mask_files.size()));
                updateProgress(progress, QString("已处理 Mask %1/%2").arg(processed_masks).arg(mask_files.size()));
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

        updateProgress(100, QString("Mask 导入完成: 有效 %1 个，跳过 %2 个").arg(valid_masks).arg(skipped_masks));
        emit importFinished(valid_masks > 0, {}, {});
    }
    catch (const std::exception &e)
    {
        spdlog::error("Mask 导入失败: {}", e.what());
        updateProgress(100, QString("Mask 导入失败: %1").arg(e.what()));
        emit importFinished(false, {}, {});
    }
}

std::vector<QString> MaskImporter::scanMaskFiles(const QString &mask_dir) const
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

bool MaskImporter::readMaskGeometry(const QString &mask_path, MaskGeometry &geometry) const
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
        {
            binary_mask.push_back(row[x] >= kMaskThreshold ? uint8_t{1} : uint8_t{0});
        }
    }

    std::vector<std::vector<QPointF>> polygons
        = dltool::common::maskToPolygons(binary_mask, mask.width(), mask.height(), false);
    if (!polygons.empty())
        geometry.polygon = std::move(polygons.front());
    else
        geometry.polygon.clear();

    return true;
}

QVariantMap MaskImporter::maskToLabelData(const MaskGeometry &geometry, int image_width, int image_height) const
{
    if (geometry.mask_width <= 0 || geometry.mask_height <= 0 || image_width <= 0 || image_height <= 0)
        return {};

    const double sx = static_cast<double>(image_width) / geometry.mask_width;
    const double sy = static_cast<double>(image_height) / geometry.mask_height;

    if (target_method_ == dltool::core::DeepLearningMethod::Detection)
    {
        return DatasetIO::bboxToLabelData(geometry.bbox.x() * sx, geometry.bbox.y() * sy, geometry.bbox.width() * sx,
                                          geometry.bbox.height() * sy, image_width, image_height);
    }

    if (target_method_ == dltool::core::DeepLearningMethod::Segmentation)
    {
        std::vector<QPointF> scaled_points;
        scaled_points.reserve(geometry.polygon.size());
        for (const QPointF &point : geometry.polygon) scaled_points.emplace_back(point.x() * sx, point.y() * sy);
        return DatasetIO::pointsToLabelData(scaled_points, image_width, image_height);
    }

    spdlog::warn("Mask 导入仅支持检测和分割项目，当前项目类型: {}", target_method_);
    return {};
}

QString MaskImporter::labelClassNameForMask(const QString &mask_path, const QString &mask_root) const
{
    QDir    root_dir(mask_root);
    QString rel_path = QDir::fromNativeSeparators(root_dir.relativeFilePath(QFileInfo(mask_path).absoluteFilePath()));
    const QStringList parts = rel_path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() > 1)
        return parts.first();
    return QFileInfo(mask_root).completeBaseName();
}

QString MaskImporter::imageStemForMask(const QString &mask_path, const QString &mask_root,
                                       const QString &mask_stem) const
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

std::map<QString, QString> MaskImporter::loadQueryNameMap(const QString &dir_path) const
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

} // namespace dltool::data
