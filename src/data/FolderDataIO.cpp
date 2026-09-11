#include "data/DataIO.h"
#include "DataIOInternal.h"
#include "ExportPipeline.h"
#include "data/ParallelFor.h"

#include "common/GeometryKernel.h"
#include "common/MaskPolygonUtils.h"
#include "common/Utils.h"
#include "core/CoreDef.h"
#include "data/DataFormat.h"
#include "data/DataNameUtils.h"
#include "data/DataOperationWorkflow.h"
#include "settings/GlobalSettings.h"
#include "settings/SettingsValue.h"
#include "ui/ProgressManager.h"

#include <json.hpp>
#include <spdlog/spdlog.h>

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPolygonF>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <type_traits>
#include <utility>

using dltool::common::ensureDirectory;
using dltool::core::DeepLearningMethod;


namespace dltool::data {

namespace {

std::vector<QString> scanImmediateImageFiles(const QString &image_dir)
{
    std::vector<QString> image_files;
    const QDir           dir(image_dir);
    if (!dir.exists())
        return image_files;

    const QFileInfoList files = dir.entryInfoList(detail::imageNameFilters(), QDir::Files, QDir::Name);
    image_files.reserve(static_cast<size_t>(files.size()));
    for (const QFileInfo &file : files)
    {
        image_files.push_back(file.absoluteFilePath());
    }
    return image_files;
}

struct FolderClassInfo
{
    QString              name;
    std::vector<QString> image_paths;
};

std::vector<FolderClassInfo> collectFolderClasses(const QDir &root_dir)
{
    std::vector<FolderClassInfo> classes;
    std::map<QString, size_t>    class_index_by_name;

    auto append_class = [&](QString raw_name, std::vector<QString> images)
    {
        if (images.empty())
            return;

        QString class_name = sanitizeName(raw_name);
        if (class_name.isEmpty())
            class_name = QStringLiteral("root");

        const auto found = class_index_by_name.find(class_name);
        if (found != class_index_by_name.end())
        {
            auto &existing_images = classes[found->second].image_paths;
            existing_images.insert(existing_images.end(), images.begin(), images.end());
            return;
        }

        class_index_by_name[class_name] = classes.size();
        classes.push_back({class_name, std::move(images)});
    };

    append_class(root_dir.dirName(), scanImmediateImageFiles(root_dir.absolutePath()));

    const QStringList class_dirs = root_dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &dir_name : class_dirs)
    {
        const QDir class_dir(root_dir.filePath(dir_name));
        append_class(dir_name, DatasetIO::scanImageFiles(class_dir.absolutePath()));
    }

    return classes;
}

} // namespace

// ============================================================================
// FolderIO
// ============================================================================

void FolderIO::startImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir)
{
    Q_UNUSED(data_dir)
    const int thread_count = detail::dataIOThreadCount();
    runInThread([this, dataset_id, image_dir, thread_count]() { doImport(dataset_id, image_dir, thread_count); },
                [this](const QString &) { emit importFinished(false, {}, {}); });
}

void FolderIO::startScanLabelClasses(const QString &image_dir, const QString &data_dir)
{
    Q_UNUSED(data_dir)
    runInThread([this, image_dir]() { doScanLabelClasses(image_dir); },
                [this](const QString &error) { emit labelClassesScanned(false, {}, error); });
}

void FolderIO::startExport(ExportDataset dataset, const QString &output_dir, const QVariantMap &options)
{
    Q_UNUSED(options)
    const int thread_count = detail::dataIOThreadCount();
    runInThread([this, dataset = std::move(dataset), output_dir, thread_count]()
                { doExport(std::move(dataset), output_dir, thread_count); },
                [this](const QString &error) { emit exportFinished(false, error); });
}

void FolderIO::doScanLabelClasses(const QString &image_dir)
{
    try
    {
        updateProgress(0, QString("正在扫描类别目录..."));

        const QDir root_dir(image_dir);
        if (!root_dir.exists())
        {
            emit labelClassesScanned(false, {}, QString("图像目录不存在: %1").arg(image_dir));
            return;
        }

        std::map<QString, QString>         label_class_info;
        int                                color_index = 0;
        const std::vector<FolderClassInfo> classes     = collectFolderClasses(root_dir);
        for (const FolderClassInfo &cls : classes)
        {
            if (isCancelRequested())
            {
                emit labelClassesScanned(false, {}, QString("类别扫描已取消"));
                return;
            }

            detail::addScannedLabelClass(label_class_info, cls.name, color_index);
        }

        if (label_class_info.empty())
        {
            emit labelClassesScanned(false, {}, QString("未找到包含图像的目录"));
            return;
        }

        updateProgress(100, QString("类别目录扫描完成: %1 个类别").arg(label_class_info.size()));
        emit labelClassesScanned(true, label_class_info, QString());
    }
    catch (const std::exception &e)
    {
        spdlog::error("类别目录扫描失败: {}", e.what());
        emit labelClassesScanned(false, {}, QString("类别目录扫描失败: %1").arg(e.what()));
    }
}

void FolderIO::doImport(int64_t dataset_id, const QString &image_dir, const int thread_count)
{
    spdlog::info("开始解析文件夹分类数据: dataset_id={}, image_dir={}", dataset_id, image_dir.toUtf8().constData());

    try
    {
        updateProgress(0, QString("正在扫描类别目录..."));

        const QDir root_dir(image_dir);
        if (!root_dir.exists())
        {
            updateProgress(100, QString("图像目录不存在: %1").arg(image_dir));
            emit importFinished(false, {}, {});
            return;
        }

        std::vector<FolderClassInfo> classes;
        int                          total_images = 0;

        for (FolderClassInfo &cls : collectFolderClasses(root_dir))
        {
            if (cls.name.isEmpty())
            {
                spdlog::warn("类别名称无效，跳过");
                continue;
            }
            if (cls.image_paths.empty())
            {
                spdlog::warn("类别目录中无图像，跳过: {}", cls.name.toUtf8().constData());
                continue;
            }

            total_images += static_cast<int>(cls.image_paths.size());
            classes.push_back(std::move(cls));
        }

        if (classes.empty())
        {
            updateProgress(100, QString("没有有效的类别目录或根目录图像"));
            emit importFinished(false, {}, {});
            return;
        }

        ImportBatchEmitter emitter(*this, dataset_id);

        int color_index      = 0;
        int processed_images = 0;
        int valid_images     = 0;

        struct FolderImportItem
        {
            QString image_path;
            QString class_name;
        };

        std::vector<FolderImportItem> items;
        items.reserve(static_cast<std::size_t>(total_images));
        for (const FolderClassInfo &cls : classes)
        {
            emitter.pushLabelClass(cls.name, DatasetIO::generateDefaultColor(color_index++));
            for (const QString &image_path : cls.image_paths) items.push_back({image_path, cls.name});
        }

        struct FolderImportResult
        {
            ImportedLabel label;
            bool          valid{false};
        };

        std::vector<FolderImportResult> results(items.size());
        parallelFor(items.size(), thread_count, cancel_requested_,
                    [&](const std::size_t index)
                    {
                        const auto &item              = items[index];
                        auto       &result            = results[index];
                        result.label.label_class_name = item.class_name;
                        result.label.image_path       = item.image_path;
                        result.valid                  = true;
                    },
                    [this](const std::size_t completed, const std::size_t total)
                    {
                        const int progress = 10 + static_cast<int>(completed * 80 / std::max<std::size_t>(1, total));
                        updateProgress(progress, QString("已并行处理文件夹图像 %1/%2").arg(completed).arg(total));
                    });

        for (std::size_t index = 0; index < results.size(); ++index)
        {
            if (isCancelRequested())
            {
                emit importFinished(false, {}, {});
                return;
            }

            ++processed_images;
            if (!results[index].valid)
                continue;
            ++valid_images;
            emitter.pushImage(results[index].label.image_path);
            emitter.pushLabel(std::move(results[index].label));

            if (processed_images % std::max(1, total_images / 10) == 0 || processed_images == total_images)
            {
                const int progress = 10 + (processed_images * 80 / std::max(1, total_images));
                updateProgress(progress, QString("已处理文件夹图像 %1/%2").arg(processed_images).arg(total_images));
            }

            if (!emitter.flushIfFullByImages(processed_images, total_images))
            {
                emit importFinished(false, {}, {});
                return;
            }
        }

        if (!emitter.flush(processed_images, total_images))
        {
            emit importFinished(false, {}, {});
            return;
        }

        if (valid_images == 0)
        {
            updateProgress(100, QString("没有有效的图像可导入"));
            emit importFinished(false, {}, {});
            return;
        }

        updateProgress(
            95, QString("文件夹解析完成，等待写入数据: %1 个图像, %2 个类别").arg(valid_images).arg(classes.size()));
        emit importFinished(true, {}, {});
    }
    catch (const std::exception &e)
    {
        spdlog::error("文件夹导入异常: {}", e.what());
        updateProgress(100, QString("导入失败: %1").arg(e.what()));
        emit importFinished(false, {}, {});
    }
}

void FolderIO::doExport(ExportDataset dataset, QString output_dir, const int thread_count)
{
    const auto result = detail::runExportPipeline(
        DataFormat::Folder, dataset, output_dir, {}, QStringLiteral("文件夹"),
        [this, &dataset, thread_count](const QString &staging_dir, QString &error, QString &summary,
                                       QString &done_hint) -> bool
        {

            std::map<int64_t, QString> class_name_by_id;
            for (const ExportLabelClass &label_class : dataset.label_classes)
                class_name_by_id[label_class.id] = label_class.name;

            std::map<int64_t, std::vector<ExportLabel>> labels_by_image;
            for (const ExportLabel &label : dataset.labels) labels_by_image[label.image_id].push_back(label);

            const int            image_count = static_cast<int>(dataset.images.size());
            std::vector<QString> target_paths(image_count);
            std::set<QString>    class_dirs;
            std::set<QString>    used_target_paths;
            for (int i = 0; i < image_count; ++i)
            {
                const ExportImage &image      = dataset.images[i];
                const auto         label_it   = labels_by_image.find(image.image_id);
                QString            class_name = "unknown";

                if (label_it != labels_by_image.end() && !label_it->second.empty())
                {
                    const auto name_it = class_name_by_id.find(label_it->second[0].label_class_id);
                    if (name_it != class_name_by_id.end())
                        class_name = name_it->second;
                }

                const QString class_dir = QDir(staging_dir).filePath(class_name);
                class_dirs.insert(class_dir);
                target_paths[i] = QDir(class_dir).filePath(QFileInfo(image.path).fileName());
                if (!used_target_paths.insert(target_paths[i]).second)
                {
    error = QString("文件夹导出目标文件名冲突: %1").arg(target_paths[i]);
                return false;
                }
            }

            for (const QString &class_dir : class_dirs)
            {
                if (!ensureDirectory(class_dir, error))
                {
                return false;
                }
            }

            struct FolderExportResult
            {
                bool    success{true};
                QString error;
            };

            if (isCancelRequested())
            {
    error = QStringLiteral("导出已取消");
                return false;
            }

            std::vector<FolderExportResult> results(image_count);
            parallelFor(static_cast<std::size_t>(image_count), thread_count, cancel_requested_,
                        [&](const std::size_t index)
                        {
                            QString task_error;
                            if (!DatasetIO::copyFile(dataset.images[index].path, target_paths[index], task_error))
                            {
                                results[index].success = false;
                                results[index].error   = task_error;
                            }
                        },
                        [this](const std::size_t completed, const std::size_t total)
                        {
                            updateProgress(5 + static_cast<int>(completed * 90 / std::max<std::size_t>(1, total)),
                                           QString("已导出图像 %1/%2").arg(completed).arg(total));
                        });

            if (isCancelRequested())
            {
    error = QStringLiteral("导出已取消");
                return false;
            }

            int exported = 0;
            for (int i = 0; i < image_count; ++i)
            {
                if (!results[i].success)
                {
    error = results[i].error;
                return false;
                }
                ++exported;
            }

            summary   = QString("文件夹导出完成: %1 个图像").arg(exported);
            done_hint = QStringLiteral("文件夹导出完成");
            return true;
        });

    if (result.success)
        updateProgress(100, result.done_hint);
    emit exportFinished(result.success, result.message);
}

} // namespace dltool::data
