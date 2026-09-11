#pragma once

/**
 * @file DataImportService.h
 * @brief data 模块私有：数据导入用例服务。
 *
 * 独占导入编排：路径校验、数据库完整性预检、导入器与写入器的装配、
 * 类别扫描会话与导入会话的终态收敛。取消身份为导入会话的 DataIO
 * 工作句柄；事务边界由 ImportDatabaseWriter 独占（批次提交 + 失败回滚）。
 */

#include "DataManagerServices.h"

#include "data/ImportDatabaseWriter.h"

#include <QElapsedTimer>
#include <QString>

#include <map>
#include <string>

namespace dltool::data {

class DataImportService
{
public:
    explicit DataImportService(DataManagerServices services);
    ~DataImportService();

    DataImportService(const DataImportService &) = delete;
    DataImportService &operator=(const DataImportService &) = delete;

    /// 当前是否处于批量导入会话中（GUI 线程批次写入期间为真）。
    bool importRunning() const
    {
        return import_running_;
    }

    /// 关闭路径：放弃会话状态（不取消已提交数据，仅复位运行标记）。
    void resetImportSession()
    {
        import_running_ = false;
    }

    /// 启动一次导入；label_class_groups 为「类别名 -> 组名」的预映射。
    void importData(int64_t dataset_id, int data_format, const QString &image_dir, const QString &data_dir,
                    const std::map<QString, QString> &label_class_groups);

    /// 扫描数据源中的标签类别（不落库，结果经 importLabelClassesScanned 返回）。
    void scanImportLabelClasses(int data_format, const QString &image_dir, const QString &data_dir);

private:
    void startImportData(int64_t dataset_id, int data_format, const QString &image_dir, const QString &data_dir,
                         const std::map<QString, QString> &label_class_groups);
    void handleImportSessionFinished(bool success, const QString &message,
                                     const ImportDatabaseWriter::Stats &stats, ImportDatabaseWriter *writer);

    DataManagerServices s_;
    bool                import_running_{false};
    QString             current_import_task_id_;
    QElapsedTimer       import_elapsed_timer_;
};

} // namespace dltool::data
