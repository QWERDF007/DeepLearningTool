#include "data/DatasetIO.h"

#include <spdlog/spdlog.h>

#include <QColor>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QRectF>

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

    QDirIterator it(data_path, QStringList() << QStringLiteral("*.json"), QDir::Files,
                    QDirIterator::Subdirectories);
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

QString DatasetIO::generateDefaultColor(int index)
{
    const double golden_ratio = 0.618033988749895;
    const double hue          = std::fmod(index * golden_ratio, 1.0);
    const QColor color        = QColor::fromHsvF(static_cast<float>(hue), 0.8F, 0.9F);
    return color.name();
}

QString DatasetIO::uniqueFileName(const QString &source_path, int64_t stable_id, const std::map<QString, int> &used_names)
{
    QFileInfo file_info(source_path);
    QString   file_name = file_info.fileName();
    if (file_name.isEmpty())
    {
        file_name = QStringLiteral("%1.jpg").arg(stable_id);
    }

    if (used_names.find(file_name) == used_names.end())
    {
        return file_name;
    }

    const QString suffix    = file_info.suffix().isEmpty() ? QString() : QStringLiteral(".%1").arg(file_info.suffix());
    const QString stem      = file_info.completeBaseName().isEmpty() ? QString::number(stable_id)
                                                                      : file_info.completeBaseName();
    QString       candidate = QStringLiteral("%1_%2%3").arg(stem).arg(stable_id).arg(suffix);
    int           index     = 1;
    while (used_names.find(candidate) != used_names.end())
    {
        candidate = QStringLiteral("%1_%2_%3%4").arg(stem).arg(stable_id).arg(index++).arg(suffix);
    }
    return candidate;
}

bool DatasetIO::ensureDirectory(const QString &path, QString &err_msg)
{
    QDir dir(path);
    if (dir.exists())
    {
        return true;
    }

    if (!dir.mkpath(QStringLiteral(".")))
    {
        err_msg = QStringLiteral("无法创建目录: %1").arg(path);
        return false;
    }
    return true;
}

bool DatasetIO::copyFile(const QString &source_path, const QString &target_path, QString &err_msg)
{
    if (!QFile::exists(source_path))
    {
        err_msg = QStringLiteral("源文件不存在: %1").arg(source_path);
        return false;
    }

    QFileInfo target_info(target_path);
    if (!ensureDirectory(target_info.dir().path(), err_msg))
    {
        return false;
    }

    if (QFile::exists(target_path) && !QFile::remove(target_path))
    {
        err_msg = QStringLiteral("无法覆盖目标文件: %1").arg(target_path);
        return false;
    }

    if (!QFile::copy(source_path, target_path))
    {
        err_msg = QStringLiteral("复制文件失败: %1 -> %2").arg(source_path, target_path);
        return false;
    }
    return true;
}

} // namespace dltool::data
