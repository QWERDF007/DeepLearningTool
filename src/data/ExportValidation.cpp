#include "data/DataIO.h"
#include "data/DataFormat.h"

#include "DataIOInternal.h"

#include <json.hpp>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include <exception>

namespace dltool::data {

namespace {

using detail::imageNameFilters;

bool validateExportDirectory(const QString &directory_path, const QStringList &filters, const int expected_count,
                            const QString &kind, QString &err_msg)
{
    const QFileInfo directory_info(directory_path);
    if (!directory_info.isDir())
    {
        err_msg = QString("导出产物目录不存在: %1").arg(directory_path);
        return false;
    }

    const QDir       directory(directory_path);
    const QFileInfoList files = directory.entryInfoList(filters, QDir::Files | QDir::Readable, QDir::Name);
    if (files.size() != expected_count)
    {
        err_msg = QString("导出产物数量不完整: %1，期望 %2，实际 %3")
                      .arg(kind)
                      .arg(expected_count)
                      .arg(files.size());
        return false;
    }

    for (const QFileInfo &file : files)
    {
        if (!file.isFile() || file.size() <= 0)
        {
            err_msg = QString("导出产物无效: %1").arg(file.absoluteFilePath());
            return false;
        }
    }
    return true;
}

bool validateJsonFile(const QString &path, QString &err_msg)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        err_msg = QString("无法读取导出 JSON: %1").arg(path);
        return false;
    }

    try
    {
        const QByteArray contents = file.readAll();
        const auto       document
            = nlohmann::json::parse(contents.constData(), contents.constData() + contents.size());
        if (!document.is_object())
        {
            err_msg = QString("导出 JSON 不是对象: %1").arg(path);
            return false;
        }
    }
    catch (const std::exception &e)
    {
        err_msg = QString("导出 JSON 无法解析: %1，%2").arg(path, QString::fromUtf8(e.what()));
        return false;
    }
    return true;
}

bool validateRecursiveImageDirectory(const QString &directory_path, const int expected_count, QString &err_msg)
{
    const QFileInfo directory_info(directory_path);
    if (!directory_info.isDir())
    {
        err_msg = QString("导出产物目录不存在: %1").arg(directory_path);
        return false;
    }

    int          actual_count = 0;
    QDirIterator iterator(directory_path, imageNameFilters(), QDir::Files | QDir::Readable,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext())
    {
        const QFileInfo file(iterator.next());
        if (file.size() <= 0)
        {
            err_msg = QString("导出产物无效: %1").arg(file.absoluteFilePath());
            return false;
        }
        ++actual_count;
    }

    if (actual_count != expected_count)
    {
        err_msg = QString("导出图像数量不完整，期望 %1，实际 %2").arg(expected_count).arg(actual_count);
        return false;
    }
    return true;
}


} // namespace

bool DataIO::validateExportOutput(const int data_format, const ExportDataset &dataset, const QString &output_dir,
                                  const QVariantMap &options, QString &err_msg)
{
    Q_UNUSED(options)

    const int expected_images = static_cast<int>(dataset.images.size());
    switch (data_format)
    {
    case DataFormat::COCO:
    {
        if (!validateExportDirectory(QDir(output_dir).filePath(QStringLiteral("images")), imageNameFilters(),
                                     expected_images,
                                     QStringLiteral("COCO 图像"), err_msg))
            return false;

        const QString annotation_path
            = QDir(output_dir).filePath(QStringLiteral("annotations/instances.json"));
        if (!validateJsonFile(annotation_path, err_msg))
            return false;

        QFile annotation_file(annotation_path);
        if (!annotation_file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            err_msg = QString("无法读取 COCO 标注文件: %1").arg(annotation_path);
            return false;
        }
        try
        {
            const QByteArray contents = annotation_file.readAll();
            const auto       document
                = nlohmann::json::parse(contents.constData(), contents.constData() + contents.size());
            if (!document.contains("images") || !document["images"].is_array()
                || document["images"].size() != dataset.images.size() || !document.contains("annotations")
                || !document["annotations"].is_array() || !document.contains("categories")
                || !document["categories"].is_array())
            {
                err_msg = QStringLiteral("COCO 标注文件内容不完整");
                return false;
            }

            const QString images_dir = QDir(output_dir).filePath(QStringLiteral("images"));
            for (const auto &img_entry : document["images"])
            {
                if (!img_entry.contains("file_name") || !img_entry["file_name"].is_string())
                {
                    err_msg = QStringLiteral("COCO 标注文件图像清单缺少 file_name 字段");
                    return false;
                }
                const QString   file_name = QString::fromStdString(img_entry["file_name"].get<std::string>());
                const QFileInfo img_fi(QDir(images_dir).filePath(file_name));
                if (!img_fi.isFile() || img_fi.size() <= 0)
                {
                    err_msg = QStringLiteral("COCO 导出清单中的图像文件不存在或无效: %1").arg(file_name);
                    return false;
                }
            }
        }
        catch (const std::exception &e)
        {
            err_msg = QString("COCO 标注文件无法解析: %1").arg(QString::fromUtf8(e.what()));
            return false;
        }
        return true;
    }
    case DataFormat::LabelMe:
        if (!validateExportDirectory(QDir(output_dir).filePath(QStringLiteral("images")), imageNameFilters(),
                                     expected_images,
                                     QStringLiteral("LabelMe 图像"), err_msg))
            return false;
        if (!validateExportDirectory(QDir(output_dir).filePath(QStringLiteral("annotations")),
                                     {QStringLiteral("*.json")}, expected_images,
                                     QStringLiteral("LabelMe 标注"), err_msg))
            return false;
        for (const QFileInfo &file : QDir(QDir(output_dir).filePath(QStringLiteral("annotations")))
                                          .entryInfoList({QStringLiteral("*.json")}, QDir::Files | QDir::Readable,
                                                         QDir::Name))
        {
            if (!validateJsonFile(file.absoluteFilePath(), err_msg))
                return false;
        }
        return true;
    case DataFormat::Mask:
        if (!validateExportDirectory(QDir(output_dir).filePath(QStringLiteral("images")), imageNameFilters(),
                                     expected_images,
                                     QStringLiteral("Mask 图像"), err_msg))
            return false;
        if (!validateExportDirectory(QDir(output_dir).filePath(QStringLiteral("masks")), imageNameFilters(),
                                     expected_images,
                                     QStringLiteral("Mask 文件"), err_msg))
            return false;
        if (!validateJsonFile(QDir(output_dir).filePath(QStringLiteral("classes.json")), err_msg))
            return false;
        return true;
    case DataFormat::Folder:
        return validateRecursiveImageDirectory(output_dir, expected_images, err_msg);
    default:
        err_msg = QString("不支持校验的数据格式: %1").arg(data_format);
        return false;
    }
}

} // namespace dltool::data
