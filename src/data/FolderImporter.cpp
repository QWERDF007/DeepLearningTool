#include "data/FolderImporter.h"

#include "core/CoreDef.h"
#include "data/DataNameUtils.h"
#include "database/DataBase.h"

#include <spdlog/spdlog.h>

#include <QDir>
#include <QFileInfo>

namespace dltool::data {

FolderImporter::FolderImporter(dltool::database::ProjectDataBase *database, QObject *parent)
    : DataImporter(database, parent)
{
}

FolderImporter::~FolderImporter() {}

void FolderImporter::startImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir)
{
    Q_UNUSED(data_dir)

    QThread *worker_thread = new QThread();

    connect(
        worker_thread, &QThread::started, this,
        [this, dataset_id, image_dir]() { doImport(dataset_id, image_dir); }, Qt::DirectConnection);

    connect(this, &FolderImporter::importFinished, worker_thread, &QThread::quit);
    connect(worker_thread, &QThread::finished, worker_thread, &QThread::deleteLater);

    worker_thread->start();
}

void FolderImporter::doImport(int64_t dataset_id, const QString &image_dir)
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

        const QDir::Filters dir_filters = QDir::Dirs | QDir::NoDotAndDotDot;
        const QStringList    class_dirs = root_dir.entryList(dir_filters, QDir::Name);
        if (class_dirs.isEmpty())
        {
            updateProgress(100, QString("未找到任何类别子目录"));
            emit importFinished(false, {}, {});
            return;
        }

        // 收集所有图片: class_name -> image_paths
        struct ClassInfo
        {
            QString              name;
            std::vector<QString> image_paths;
        };
        std::vector<ClassInfo> classes;
        int                    total_images = 0;

        for (const QString &dir_name : class_dirs)
        {
            const QString class_name = sanitizeName(dir_name);
            if (class_name.isEmpty())
            {
                spdlog::warn("子目录名称无效，跳过: {}", dir_name.toUtf8().constData());
                continue;
            }

            const QDir                 class_dir(root_dir.filePath(dir_name));
            const std::vector<QString> images = DatasetIO::scanImageFiles(class_dir.absolutePath());
            if (images.empty())
            {
                spdlog::warn("类别目录中无图像，跳过: {}", class_name.toUtf8().constData());
                continue;
            }

            classes.push_back({class_name, images});
            total_images += static_cast<int>(images.size());
        }

        if (classes.empty())
        {
            updateProgress(100, QString("没有有效的类别目录"));
            emit importFinished(false, {}, {});
            return;
        }

        // 批次处理
        std::vector<QString>       batch_image_paths;
        std::vector<int64_t>       batch_image_widths;
        std::vector<int64_t>       batch_image_heights;
        std::map<QString, QString> batch_label_class_info;
        std::vector<ImportedLabel> batch_labels;
        batch_image_paths.reserve(DataImporter::ImportBatchImageCount);
        batch_image_widths.reserve(DataImporter::ImportBatchImageCount);
        batch_image_heights.reserve(DataImporter::ImportBatchImageCount);

        int                        color_index      = 0;
        int                        processed_images = 0;
        int                        valid_images     = 0;
        int                        skipped_images   = 0;
        std::map<QString, QString> class_colors;

        auto flush_batch = [&]() -> bool
        {
            if (batch_image_paths.empty() && batch_labels.empty())
            {
                return true;
            }

            emit dataBatchReady(dataset_id, std::move(batch_image_paths), std::move(batch_image_widths),
                                std::move(batch_image_heights), std::move(batch_label_class_info),
                                std::move(batch_labels), processed_images, total_images);

            batch_image_paths.clear();
            batch_image_widths.clear();
            batch_image_heights.clear();
            batch_label_class_info.clear();
            batch_labels.clear();
            batch_image_paths.reserve(DataImporter::ImportBatchImageCount);
            batch_image_widths.reserve(DataImporter::ImportBatchImageCount);
            batch_image_heights.reserve(DataImporter::ImportBatchImageCount);

            return !isCancelRequested();
        };

        for (const ClassInfo &cls : classes)
        {
            if (class_colors.find(cls.name) == class_colors.end())
            {
                class_colors[cls.name] = DatasetIO::generateDefaultColor(color_index++);
                batch_label_class_info[cls.name] = class_colors[cls.name];
            }

            for (const QString &image_path : cls.image_paths)
            {
                if (isCancelRequested())
                {
                    emit importFinished(false, {}, {});
                    return;
                }

                ++processed_images;

                int width  = 0;
                int height = 0;
                if (!DatasetIO::getImageDimensions(image_path, width, height))
                {
                    ++skipped_images;
                    continue;
                }

                ++valid_images;
                batch_image_paths.push_back(image_path);
                batch_image_widths.push_back(width);
                batch_image_heights.push_back(height);

                ImportedLabel label;
                label.label_class_name = cls.name;
                label.data             = DatasetIO::bboxToLabelData(0, 0, width, height, width, height);
                label.image_path       = image_path;
                batch_labels.push_back(label);

                if (processed_images % std::max(1, total_images / 10) == 0 || processed_images == total_images)
                {
                    const int progress = 10 + (processed_images * 80 / std::max(1, total_images));
                    updateProgress(progress,
                                   QString("已处理文件夹图像 %1/%2").arg(processed_images).arg(total_images));
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
        }

        if (!flush_batch())
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

        updateProgress(100, QString("文件夹导入完成: %1 个图像, %2 个类别，跳过图像 %3 个")
                                .arg(valid_images)
                                .arg(classes.size())
                                .arg(skipped_images));
        emit importFinished(true, {}, {});
    }
    catch (const std::exception &e)
    {
        spdlog::error("文件夹导入异常: {}", e.what());
        updateProgress(100, QString("导入失败: %1").arg(e.what()));
        emit importFinished(false, {}, {});
    }
}

} // namespace dltool::data
