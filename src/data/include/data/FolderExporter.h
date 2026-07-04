#pragma once

#include "DataExporter.h"

#include <QThread>

namespace dltool::data {

/**
 * @brief 文件夹分类格式导出器
 *
 * 导出目录结构: output_dir/类别名/图片
 * 每张图片根据其标注类别放入对应子目录，用于图像分类项目。
 */
class DATA_API FolderExporter : public DataExporter
{
    Q_OBJECT

public:
    explicit FolderExporter(QObject *parent = nullptr);
    ~FolderExporter() override;

    void startExport(const ExportDataset &dataset, const QString &output_dir, const QVariantMap &options = {}) override;

private:
    void doExport(ExportDataset dataset, QString output_dir);
};

} // namespace dltool::data
