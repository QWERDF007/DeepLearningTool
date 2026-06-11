#pragma once

#include "dltool/data/Export.h"
#include "DatasetIO.h"

#include <QObject>
#include <QString>

namespace dltool::data {

/**
 * @brief 数据集导出器基类
 *
 * DataManager 负责从模型收集统一的 ExportDataset，具体格式类只关心如何落盘。
 * 新增格式时实现一个子类，并在 createExporter 中注册即可。
 */
class DATA_API DataExporter : public QObject
{
    Q_OBJECT

public:
    explicit DataExporter(QObject *parent = nullptr);
    ~DataExporter() override;

    static DataExporter *createExporter(int data_format, QObject *parent = nullptr);

    virtual void startExport(const ExportDataset &dataset, const QString &output_dir) = 0;

signals:
    void exportFinished(bool success, const QString &message);

protected:
    void updateProgress(int progress, const QString &message);
};

} // namespace dltool::data
