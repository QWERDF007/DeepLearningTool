#include "data/FolderExporter.h"

#include <spdlog/spdlog.h>

#include <QDir>
#include <QFileInfo>
#include <map>

namespace dltool::data {

FolderExporter::FolderExporter(QObject *parent)
    : DataExporter(parent)
{
}

FolderExporter::~FolderExporter() {}

void FolderExporter::startExport(const ExportDataset &dataset, const QString &output_dir, const QVariantMap &options)
{
    Q_UNUSED(options)

    QThread *worker_thread = new QThread();

    connect(
        worker_thread, &QThread::started, this, [this, dataset, output_dir]() { doExport(dataset, output_dir); },
        Qt::DirectConnection);

    connect(this, &FolderExporter::exportFinished, worker_thread, &QThread::quit);
    connect(worker_thread, &QThread::finished, worker_thread, &QThread::deleteLater);

    worker_thread->start();
}

void FolderExporter::doExport(ExportDataset dataset, QString output_dir)
{
    try
    {
        QString err_msg;
        if (!DatasetIO::ensureDirectory(output_dir, err_msg))
        {
            emit exportFinished(false, err_msg);
            return;
        }

        // label_class_id -> class_name
        std::map<int64_t, QString> class_name_by_id;
        for (const ExportLabelClass &label_class : dataset.label_classes)
        {
            class_name_by_id[label_class.id] = label_class.name;
        }

        // image_id -> class_name (取第一个标注的类别)
        std::map<int64_t, std::vector<ExportLabel>> labels_by_image;
        for (const ExportLabel &label : dataset.labels)
        {
            labels_by_image[label.image_id].push_back(label);
        }

        const int image_count = static_cast<int>(dataset.images.size());
        int       exported    = 0;

        for (int i = 0; i < image_count; ++i)
        {
            const ExportImage &image       = dataset.images[i];
            const auto         label_it    = labels_by_image.find(image.image_id);
            QString            class_name  = "unknown";

            if (label_it != labels_by_image.end() && !label_it->second.empty())
            {
                const auto name_it = class_name_by_id.find(label_it->second[0].label_class_id);
                if (name_it != class_name_by_id.end())
                {
                    class_name = name_it->second;
                }
            }

            const QString class_dir = QDir(output_dir).filePath(class_name);
            if (!DatasetIO::ensureDirectory(class_dir, err_msg))
            {
                emit exportFinished(false, err_msg);
                return;
            }

            const QString file_name  = QFileInfo(image.path).fileName();
            const QString target_path = QDir(class_dir).filePath(file_name);
            if (!DatasetIO::copyFile(image.path, target_path, err_msg))
            {
                emit exportFinished(false, err_msg);
                return;
            }

            ++exported;

            if ((i + 1) % std::max(1, image_count / 10) == 0 || i + 1 == image_count)
            {
                updateProgress((i + 1) * 100 / std::max(1, image_count),
                               QString("已导出图像 %1/%2").arg(i + 1).arg(image_count));
            }
        }

        emit exportFinished(
            true, QString("文件夹导出完成: %1 个图像").arg(exported));
    }
    catch (const std::exception &e)
    {
        spdlog::error("文件夹导出失败: {}", e.what());
        emit exportFinished(false, QString("文件夹导出失败: %1").arg(e.what()));
    }
}

} // namespace dltool::data
