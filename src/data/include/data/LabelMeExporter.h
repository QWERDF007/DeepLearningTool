#pragma once

#include "DataExporter.h"

#include <QThread>

namespace dltool::data {

/**
 * @brief LabelMe 格式导出器
 *
 * 导出目录结构:
 * - images/ 保存复制后的图像
 * - annotations/ 保存每张图像对应的 LabelMe JSON
 */
class DATA_API LabelMeExporter : public DataExporter
{
    Q_OBJECT

public:
    explicit LabelMeExporter(QObject *parent = nullptr);
    ~LabelMeExporter() override;

    void startExport(const ExportDataset &dataset, const QString &output_dir, const QVariantMap &options = {}) override;

private:
    void doExport(ExportDataset dataset, QString output_dir);
};

} // namespace dltool::data
