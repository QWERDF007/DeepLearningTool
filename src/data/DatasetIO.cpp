#include "data/DatasetIO.h"

#include "common/GeometryKernel.h"
#include "common/Utils.h"

#include <spdlog/spdlog.h>
#include <filesystem>

#include <QColor>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QRectF>
#include <algorithm>
#include <cmath>

namespace dltool::data {

std::vector<QString> DatasetIO::scanImageFiles(const QString &image_dir)
{
    std::vector<QString> image_files;

    QDir dir(image_dir);
    if (!dir.exists())
    {
        spdlog::warn("图像目录不存在: {}", image_dir.toStdString());
        return image_files;
    }

    QStringList image_filters;
    image_filters << "*.jpg" << "*.jpeg" << "*.png" << "*.bmp" << "*.gif" << "*.tiff" << "*.tif" << "*.webp"
                  << "*.JPG" << "*.JPEG" << "*.PNG" << "*.BMP" << "*.GIF" << "*.TIFF" << "*.TIF" << "*.WEBP";

    QDirIterator it(image_dir, image_filters, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        image_files.push_back(it.next());
    }

    spdlog::info("在目录 {} 中找到 {} 个图像文件", image_dir.toStdString(), image_files.size());
    return image_files;
}

std::vector<QString> DatasetIO::scanJsonFiles(const QString &data_path)
{
    std::vector<QString> json_files;

    QFileInfo path_info(data_path);
    if (path_info.isFile())
    {
        if (path_info.suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) == 0)
        {
            json_files.push_back(path_info.absoluteFilePath());
        }
        return json_files;
    }

    QDir dir(data_path);
    if (!dir.exists())
    {
        spdlog::warn("数据路径不存在: {}", data_path.toStdString());
        return json_files;
    }

    QDirIterator it(data_path, QStringList() << QStringLiteral("*.json"), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        json_files.push_back(it.next());
    }

    return json_files;
}

bool DatasetIO::getImageDimensions(const QString &image_path, int &width, int &height)
{
    QImageReader reader(image_path);
    const QSize  size = reader.size();
    if (!size.isValid())
    {
        spdlog::warn("无法读取图像尺寸: {}, 错误: {}", image_path.toStdString(), reader.errorString().toStdString());
        return false;
    }

    width  = size.width();
    height = size.height();
    return true;
}

QVariantMap DatasetIO::bboxToLabelData(double x, double y, double width, double height, int image_width,
                                       int image_height)
{
    if (image_width <= 0 || image_height <= 0 || width <= 0 || height <= 0)
    {
        return {};
    }

    const QRectF image_rect(0, 0, image_width, image_height);
    const QRectF bbox(x, y, width, height);
    const QRectF clipped = bbox.intersected(image_rect);
    if (clipped.width() <= 0 || clipped.height() <= 0)
    {
        return {};
    }

    return QVariantMap{
        {     "x",      clipped.x()},
        {     "y",      clipped.y()},
        { "width",  clipped.width()},
        {"height", clipped.height()},
    };
}

QVariantList DatasetIO::pointsToVariantList(const std::vector<QPointF> &points)
{
    QVariantList result;
    result.reserve(static_cast<int>(points.size()));
    for (const QPointF &point : points)
    {
        result.push_back(QVariantMap{
            {"x", point.x()},
            {"y", point.y()},
        });
    }
    return result;
}

std::vector<QPointF> DatasetIO::variantListToPoints(const QVariant &value)
{
    std::vector<QPointF> points;
    const QVariantList   list = value.toList();
    points.reserve(static_cast<size_t>(list.size()));

    for (const QVariant &item : list)
    {
        if (item.canConvert<QVariantMap>())
        {
            const QVariantMap map = item.toMap();
            points.emplace_back(map.value(QStringLiteral("x")).toDouble(), map.value(QStringLiteral("y")).toDouble());
        }
        else if (item.canConvert<QVariantList>())
        {
            const QVariantList pair = item.toList();
            if (pair.size() >= 2)
            {
                points.emplace_back(pair[0].toDouble(), pair[1].toDouble());
            }
        }
    }

    return points;
}

QVariantMap DatasetIO::pointsToLabelData(const std::vector<QPointF> &points, int image_width, int image_height)
{
    if (image_width <= 0 || image_height <= 0 || points.size() < 3)
    {
        return {};
    }

    const std::vector<QPointF> clipped_points
        = common::geometry::clipPolygon(points, QRectF(0.0, 0.0, image_width, image_height));
    if (clipped_points.size() < 3)
        return {};

    double x_min = clipped_points.front().x();
    double y_min = clipped_points.front().y();
    double x_max = clipped_points.front().x();
    double y_max = clipped_points.front().y();
    for (const QPointF &point : clipped_points)
    {
        x_min = std::min(x_min, point.x());
        y_min = std::min(y_min, point.y());
        x_max = std::max(x_max, point.x());
        y_max = std::max(y_max, point.y());
    }

    QVariantMap data = bboxToLabelData(x_min, y_min, x_max - x_min, y_max - y_min, image_width, image_height);
    if (data.isEmpty())
    {
        return {};
    }

    data[QStringLiteral("point_count")] = static_cast<int>(clipped_points.size());
    data[QStringLiteral("points")]      = pointsToVariantList(clipped_points);
    return data;
}

QString DatasetIO::generateDefaultColor(int index)
{
    const double golden_ratio = 0.618033988749895;
    const double hue          = std::fmod(index * golden_ratio, 1.0);
    const QColor color        = QColor::fromHsvF(static_cast<float>(hue), 0.8F, 0.9F);
    return color.name();
}

QString DatasetIO::uniqueFileName(const QString &source_path, int64_t stable_id,
                                  const std::map<QString, int> &used_names)
{
    QFileInfo file_info(source_path);
    QString   file_name = file_info.fileName();
    if (file_name.isEmpty())
    {
        file_name = QString("%1.jpg").arg(stable_id);
    }

    if (used_names.find(file_name) == used_names.end())
    {
        return file_name;
    }

    const QString suffix = file_info.suffix().isEmpty() ? QString() : QString(".%1").arg(file_info.suffix());
    const QString stem
        = file_info.completeBaseName().isEmpty() ? QString::number(stable_id) : file_info.completeBaseName();
    QString candidate = QString("%1_%2%3").arg(stem).arg(stable_id).arg(suffix);
    int     index     = 1;
    while (used_names.find(candidate) != used_names.end())
    {
        candidate = QString("%1_%2_%3%4").arg(stem).arg(stable_id).arg(index++).arg(suffix);
    }
    return candidate;
}

bool DatasetIO::copyFile(const QString &source_path, const QString &target_path, QString &err_msg)
{
    if (!QFile::exists(source_path))
    {
        err_msg = QString("源文件不存在: %1").arg(source_path);
        return false;
    }

    if (isSameFileOrAlias(source_path, target_path))
    {
        err_msg = QString("源文件与目标文件为同一文件或路径别名，拒绝覆盖: %1 -> %2").arg(source_path, target_path);
        return false;
    }

    QFileInfo target_info(target_path);
    if (!common::ensureDirectory(target_info.dir().path(), err_msg))
    {
        return false;
    }

    if (QFile::exists(target_path) && !QFile::remove(target_path))
    {
        err_msg = QString("无法覆盖目标文件: %1").arg(target_path);
        return false;
    }

    if (!QFile::copy(source_path, target_path))
    {
        err_msg = QString("复制文件失败: %1 -> %2").arg(source_path, target_path);
        return false;
    }
    return true;
}

bool DatasetIO::isSameFileOrAlias(const QString &path1, const QString &path2)
{
    const QString clean1 = common::cleanPath(path1);
    const QString clean2 = common::cleanPath(path2);
    if (clean1.isEmpty() || clean2.isEmpty())
        return false;

    if (clean1 == clean2)
        return true;

#ifdef Q_OS_WIN
    if (clean1.compare(clean2, Qt::CaseInsensitive) == 0)
        return true;
#endif

    const QFileInfo fi1(clean1);
    const QFileInfo fi2(clean2);
    if (fi1.exists() && fi2.exists())
    {
        const QString canon1 = fi1.canonicalFilePath();
        const QString canon2 = fi2.canonicalFilePath();
        if (!canon1.isEmpty() && !canon2.isEmpty())
        {
#ifdef Q_OS_WIN
            if (canon1.compare(canon2, Qt::CaseInsensitive) == 0)
                return true;
#else
            if (canon1 == canon2)
                return true;
#endif
        }
    }

    const QString canon_dir1 = fi1.exists() ? fi1.canonicalPath()
                                            : (fi1.dir().exists() ? fi1.dir().canonicalPath() : fi1.dir().absolutePath());
    const QString canon_dir2 = fi2.exists() ? fi2.canonicalPath()
                                            : (fi2.dir().exists() ? fi2.dir().canonicalPath() : fi2.dir().absolutePath());
    if (!canon_dir1.isEmpty() && !canon_dir2.isEmpty())
    {
        const QString full1 = common::cleanPath(canon_dir1 + QStringLiteral("/") + fi1.fileName());
        const QString full2 = common::cleanPath(canon_dir2 + QStringLiteral("/") + fi2.fileName());
#ifdef Q_OS_WIN
        if (full1.compare(full2, Qt::CaseInsensitive) == 0)
            return true;
#else
        if (full1 == full2)
            return true;
#endif
    }

    return false;
}

bool DatasetIO::isPathInsideDirectory(const QString &file_path, const QString &dir_path)
{
    const QString clean_file = common::cleanPath(file_path);
    const QString clean_dir  = common::cleanPath(dir_path);
    if (clean_file.isEmpty() || clean_dir.isEmpty())
        return false;

    QFileInfo fi_file(clean_file);
    QFileInfo fi_dir(clean_dir);
    QString canon_file = fi_file.exists() ? fi_file.canonicalFilePath() : fi_file.absoluteFilePath();
    QString canon_dir  = fi_dir.exists() ? fi_dir.canonicalFilePath() : fi_dir.absoluteFilePath();
    if (canon_file.isEmpty())
        canon_file = clean_file;
    if (canon_dir.isEmpty())
        canon_dir = clean_dir;

    canon_file = common::cleanPath(canon_file);
    canon_dir  = common::cleanPath(canon_dir);

#ifdef Q_OS_WIN
    canon_file = canon_file.toLower();
    canon_dir  = canon_dir.toLower();
#endif

    if (canon_file == canon_dir)
        return true;

    if (!canon_dir.endsWith(QLatin1Char('/')))
        canon_dir += QLatin1Char('/');

    return canon_file.startsWith(canon_dir);
}

bool DatasetIO::resolveExportPath(const QString &output_dir, const QVariantMap &options, QString &resolved_dir,
                                  QString &err_msg)
{
    const QString trimmed = output_dir.trimmed();
    if (trimmed.isEmpty())
    {
        err_msg = QStringLiteral("导出目录路径为空");
        return false;
    }

    if (common::isUncPath(trimmed))
    {
        if (!common::isValidUncPath(trimmed))
        {
            err_msg = QStringLiteral("导出目录包含无效的 UNC 共享路径: %1").arg(output_dir);
            return false;
        }
        resolved_dir = common::cleanPath(trimmed);
        return true;
    }

    const QString cleaned = common::cleanPath(trimmed);
    if (QFileInfo(cleaned).isAbsolute())
    {
        resolved_dir = cleaned;
        return true;
    }

    const QString explicit_root = options.value(QStringLiteral("base_dir"),
                                                options.value(QStringLiteral("root_dir"))).toString().trimmed();
    if (!explicit_root.isEmpty())
    {
        const QString cleaned_root = common::cleanPath(explicit_root);
        if (QFileInfo(cleaned_root).isAbsolute() || common::isValidUncPath(cleaned_root))
        {
            resolved_dir = common::resolvePath(cleaned_root, cleaned);
            return true;
        }
        else
        {
            err_msg = QStringLiteral("指定的相对根目录不是有效的绝对路径: %1").arg(explicit_root);
            return false;
        }
    }

    err_msg = QStringLiteral("导出目录必须是有效的绝对路径: %1").arg(output_dir);
    return false;
}

} // namespace dltool::data
