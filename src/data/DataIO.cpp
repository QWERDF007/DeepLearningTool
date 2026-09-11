#include "data/DataIO.h"
#include "DataIOInternal.h"
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


// ============================================================================
// DataIO base class
// ============================================================================

DataIO::DataIO(QObject *parent)
    : QObject(parent)
{
}

DataIO::~DataIO()
{
    if (operation_handle_ != nullptr)
    {
        operation_handle_->requestCancel();
        operation_handle_->waitForDone();
    }
}

void DataIO::requestCancel()
{
    cancel_requested_.store(true, std::memory_order_relaxed);
    if (operation_handle_ != nullptr)
        operation_handle_->requestCancel();
}

bool DataIO::isCancelRequested() const
{
    return cancel_requested_.load(std::memory_order_relaxed);
}

bool DataIO::waitForDone(const int timeout_ms) const
{
    return operation_handle_ == nullptr || operation_handle_->waitForDone(timeout_ms);
}

DataIO *DataIO::createIO(int data_format, QObject *parent)
{
    if (!DataFormat::isDataFormatSupported(data_format))
    {
        spdlog::error("不支持的数据格式: {}", data_format);
        return nullptr;
    }

    switch (data_format)
    {
    case DataFormat::LabelMe:
        return new LabelMeIO(parent);
    case DataFormat::COCO:
        return new COCOIO(parent);
    case DataFormat::Mask:
        return new MaskIO(parent);
    case DataFormat::Folder:
        return new FolderIO(parent);
    default:
        spdlog::error("未实现的数据格式: {}", data_format);
        return nullptr;
    }
}



bool DataIO::checkExportSourceCollision(const ExportDataset &dataset, const QString &target_dir,
                                       const int data_format, QString &err_msg)
{
    const QString clean_target = common::cleanPath(target_dir);
    if (clean_target.isEmpty())
        return true;

    for (const ExportImage &image : dataset.images)
    {
        if (image.path.isEmpty())
            continue;

        if (DatasetIO::isSameFileOrAlias(image.path, clean_target))
        {
            err_msg = QStringLiteral("导出目标路径与源图像文件冲突（同一文件或别名），拒绝覆盖: %1").arg(image.path);
            return false;
        }

        if (DatasetIO::isPathInsideDirectory(image.path, clean_target))
        {
            err_msg = QStringLiteral("导出目标目录与源图像冲突（包含源图像文件），拒绝覆盖源文件目录: %1").arg(image.path);
            return false;
        }

        QString planned_target_path;
        const QString file_name = QFileInfo(image.path).fileName();
        switch (data_format)
        {
        case DataFormat::COCO:
        case DataFormat::LabelMe:
        case DataFormat::Mask:
            planned_target_path = QDir(clean_target).filePath(QStringLiteral("images/") + file_name);
            break;
        case DataFormat::Folder:
            planned_target_path = QDir(clean_target).filePath(file_name);
            break;
        default:
            break;
        }

        if (!planned_target_path.isEmpty() && DatasetIO::isSameFileOrAlias(image.path, planned_target_path))
        {
            err_msg = QStringLiteral("导出目标图像与源图像文件冲突（同一文件或别名），拒绝覆盖: %1").arg(image.path);
            return false;
        }
    }
    return true;
}

void DataIO::startImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir)
{
    Q_UNUSED(dataset_id)
    Q_UNUSED(image_dir)
    Q_UNUSED(data_dir)
}

void DataIO::startScanLabelClasses(const QString &image_dir, const QString &data_dir)
{
    Q_UNUSED(image_dir)
    Q_UNUSED(data_dir)
    emit labelClassesScanned(true, {}, QString());
}

void DataIO::startExport(ExportDataset dataset, const QString &output_dir, const QVariantMap &options)
{
    Q_UNUSED(dataset)
    Q_UNUSED(output_dir)
    Q_UNUSED(options)
}

void DataIO::updateProgress(int progress, const QString &message)
{
    const int clamped_progress  = std::clamp(progress, 0, 100);
    const int effective_progress = min_progress_percent_
                                 + (clamped_progress * (max_progress_percent_ - min_progress_percent_) / 100);

    if (!task_id_.isEmpty())
    {
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "updateProgress", Qt::QueuedConnection,
                                  Q_ARG(int, effective_progress), Q_ARG(QString, task_id_));
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection,
                                  Q_ARG(int, spdlog::level::info), Q_ARG(QString, message), Q_ARG(QString, task_id_));
    }
    else
    {
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "updateProgress", Qt::QueuedConnection,
                                  Q_ARG(int, effective_progress));
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection,
                                  Q_ARG(int, spdlog::level::info), Q_ARG(QString, message));
    }
}

void DataIO::runInThread(std::function<void()> work, std::function<void(const QString &error)> on_failure)
{
    if (operation_handle_ != nullptr && !operation_handle_->isFinished())
    {
        spdlog::error("DataIO 后台操作仍在运行，拒绝启动并发操作");
        return;
    }

    // A DataIO instance can be reused after a completed cancellation.  The
    // cancellation flag belongs to the current operation, not to the object.
    cancel_requested_.store(false, std::memory_order_relaxed);

    DataOperationWorkflow::Options options;
    options.manage_progress = false;

    operation_handle_ = DataOperationWorkflow::start(
        this, std::move(options),
        [work = std::move(work)](DataOperationWorkflow::Result &result) mutable
        {
            work();
            result.success = true;
        },
        [on_failure = std::move(on_failure)](const DataOperationWorkflow::Result &result)
        {
            if (!result.success)
            {
                spdlog::error("DataIO 后台任务失败: {}", result.error.toUtf8().constData());
                if (on_failure)
                {
                    on_failure(result.error);
                }
            }
        });
}

bool DataIO::importImagesOnly(int64_t dataset_id, const QString &image_dir, const QString &format_name,
                              const int thread_count)
{
    updateProgress(0, QString("正在扫描图像文件..."));
    const std::vector<QString> image_files = DatasetIO::scanImageFiles(image_dir);
    if (image_files.empty())
    {
        updateProgress(100, QString("未找到任何图像文件"));
        emit importFinished(false, {}, {});
        return false;
    }

    ImportBatchEmitter emitter(*this, dataset_id);

    const int total_images = static_cast<int>(image_files.size());

    struct ImageDimensionResult
    {
        int  width{0};
        int  height{0};
        bool valid{false};
    };

    std::vector<ImageDimensionResult> results(image_files.size());
    parallelFor(image_files.size(), thread_count, cancel_requested_,
                [&](const std::size_t index)
                {
                    results[index].valid = DatasetIO::getImageDimensions(image_files[index], results[index].width,
                                                                         results[index].height);
                },
                [this](const std::size_t completed, const std::size_t total)
                {
                    const int progress = 10 + static_cast<int>(completed * 80 / std::max<std::size_t>(1, total));
                    updateProgress(progress, QString("已并行处理图像 %1/%2").arg(completed).arg(total));
                });

    int processed    = 0;
    int valid_images = 0;
    int skipped      = 0;

    for (std::size_t index = 0; index < image_files.size(); ++index)
    {
        if (isCancelRequested())
        {
            emit importFinished(false, {}, {});
            return false;
        }

        ++processed;
        const QString &image_path = image_files[index];
        if (!results[index].valid)
        {
            ++skipped;
            continue;
        }

        ++valid_images;
        emitter.pushImage(image_path, results[index].width, results[index].height);

        if (!emitter.flushIfFullByImages(processed, total_images))
        {
            emit importFinished(false, {}, {});
            return false;
        }

        if (processed % std::max(1, total_images / 10) == 0 || processed == total_images)
        {
            const int progress = 10 + processed * 80 / std::max(1, total_images);
            updateProgress(progress, QString("已处理图像 %1/%2").arg(processed).arg(total_images));
        }
    }

    if (!emitter.flush(processed, total_images))
    {
        emit importFinished(false, {}, {});
        return false;
    }

    if (valid_images == 0)
    {
        updateProgress(100, QString("没有有效的图像可导入"));
        emit importFinished(false, {}, {});
        return false;
    }

    updateProgress(
        100, QString("%1 导入完成: %2 个图像，无标注，跳过图像 %3 个").arg(format_name).arg(valid_images).arg(skipped));
    emit importFinished(true, {}, {});
    return true;
}


} // namespace dltool::data
