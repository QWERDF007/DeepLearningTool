#pragma once

#include "DataExporter.h"

#include <QThread>

namespace dltool::data {

/**
 * @brief Mask 数据格式导出器
 *
 * 导出目录结构:
 * - images/ 保存复制后的图像
 * - masks/ 保存每张图像对应的 8-bit PNG Mask
 * - classes.json 保存类别值映射
 */
class DATA_API MaskExporter : public DataExporter
{
    Q_OBJECT

public:
    explicit MaskExporter(QObject *parent = nullptr);
    ~MaskExporter() override;

    void startExport(const ExportDataset &dataset, const QString &output_dir, const QVariantMap &options = {}) override;

private:
    void doExport(ExportDataset dataset, QString output_dir, QVariantMap options);
};

} // namespace dltool::data
