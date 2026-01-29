#pragma once

#include "DataExport.h"

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <map>
#include <vector>

namespace dltool::data {

class ProjectDataBase;

/**
 * @brief 导入的标注数据结构
 * 
 * 表示一个已处理的标注，准备插入数据库
 */
struct DATA_API ImportedLabel
{
    QString     label_class_name; // 标签类别名称
    QVariantMap data;             // 标注数据 (x, y, width, height, 等)
    QString     image_path;       // 关联的图像路径（用于匹配）
};

/**
 * @brief DataImporter 基类
 * 
 * 所有数据导入器的基类，定义了导入操作的通用接口。
 * 
 * 架构说明：
 * - 使用工厂模式创建具体的导入器实例（通过 createImporter 静态方法）
 * - 每个导入器负责处理特定格式的数据（如 LabelMe、COCO 等）
 * - 导入器在后台线程中执行，通过信号与主线程通信
 * - dataReady 信号提供完整的处理后数据，DataManager 只需插入数据库
 * 
 * 子类应该实现具体的导入逻辑。
 */
class DATA_API DataImporter : public QObject
{
    Q_OBJECT

public:
    explicit DataImporter(ProjectDataBase *database, QObject *parent = nullptr);
    virtual ~DataImporter();

    /**
     * @brief 工厂函数：根据数据格式创建相应的导入器
     * @param data_format 数据格式标识符
     * @param database 项目数据库指针
     * @param parent 父对象
     * @return 导入器实例，如果格式不支持则返回 nullptr
     */
    static DataImporter *createImporter(int data_format, ProjectDataBase *database, QObject *parent = nullptr);

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
     * @brief 数据准备完成信号
     * 
     * 当数据完全处理完成并准备插入数据库时发出此信号
     * 
     * 注意：进度更新现在由 DataImporter 内部处理，直接调用 ProgressManager。
     * 不再需要通过信号传递进度更新。
     * 
     * @param success 是否成功
     * @param dataset_id 数据集 ID
     * @param image_paths 图像路径列表
     * @param image_widths 图像宽度列表
     * @param image_heights 图像高度列表
     * @param label_class_info 标签类别信息映射 (名称 -> 颜色)
     * @param labels 导入的标注列表
     */
    void dataReady(bool success, int64_t dataset_id, std::vector<QString> image_paths,
                   std::vector<int64_t> image_widths, std::vector<int64_t> image_heights,
                   std::map<QString, QString> label_class_info, std::vector<ImportedLabel> labels);

protected:
    ProjectDataBase *database_;

    /**
     * @brief 更新进度信息到 UI
     * 
     * 该方法封装了进度更新逻辑，直接调用 ProgressManager。
     * 使用 Qt::QueuedConnection 确保线程安全的 UI 更新。
     * 
     * @param progress 进度百分比 (0-100)
     * @param message 状态消息
     */
    void updateProgress(int progress, const QString &message);
};

} // namespace dltool::data
