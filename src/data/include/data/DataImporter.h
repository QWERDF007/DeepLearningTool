#pragma once

#include "DataExport.h"

#include <QObject>
#include <QString>
#include <vector>

namespace dltool::data {

class ProjectDataBase;

/**
 * @brief DataImporter 基类
 * 
 * 所有数据导入器的基类，定义了导入操作的通用接口。
 * 子类应该实现具体的导入逻辑。
 */
class DATA_API DataImporter : public QObject
{
    Q_OBJECT

public:
    explicit DataImporter(ProjectDataBase *database, QObject *parent = nullptr);
    virtual ~DataImporter();

    /**
     * @brief 开始导入数据
     * @param dataset_id 数据集 ID
     * @param image_dir 图像目录路径
     * @param data_dir 数据文件目录路径
     * 
     * 该方法应该在子类中实现，启动导入过程。
     * 导入应该在后台线程中执行，以避免阻塞 UI。
     */
    virtual void startImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir) = 0;

signals:
    /**
     * @brief 导入完成信号
     * @param success 是否成功
     * @param image_ids 导入的图像 ID 列表
     * @param label_class_ids 导入的标签类别 ID 列表
     */
    void importFinished(bool success, std::vector<int64_t> image_ids, std::vector<int64_t> label_class_ids);

    /**
     * @brief 进度更新信号
     * @param progress 进度百分比 (0-100)
     * @param message 状态消息
     */
    void progressUpdated(int progress, const QString &message);

protected:
    ProjectDataBase *database_;
};

} // namespace dltool::data
