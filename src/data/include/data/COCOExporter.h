#pragma once

#include "DataExporter.h"

#include <QThread>

namespace dltool::data {

/**
 * @brief COCO 数据格式导出器
 *
 * 导出目录结构:
 * - images/ 保存复制后的图像
 * - annotations/instances.json 保存 COCO instances 标注
 */
class DATA_API COCOExporter : public DataExporter
{
    Q_OBJECT

public:
    explicit COCOExporter(QObject *parent = nullptr);
    ~COCOExporter() override;

    void startExport(const ExportDataset &dataset, const QString &output_dir) override;

private:
    void doExport(ExportDataset dataset, QString output_dir);
};

} // namespace dltool::data
