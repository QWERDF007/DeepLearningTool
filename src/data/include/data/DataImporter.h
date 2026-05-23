#pragma once

#include "DataExport.h"

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <atomic>
#include <cstddef>
#include <map>
#include <vector>

namespace dltool::database {
class ProjectDataBase;
} // namespace dltool::database

namespace dltool::data {

/**
 * @brief 导入器输出的单条标注数据
 *
 * 导入器只负责把外部格式解析成统一结构，真正的数据库写入由 DataManager 完成。
 */
struct DATA_API ImportedLabel
{
    QString     label_class_name; // 标签类别名称
    QVariantMap data;             // 标注数据，例如 bbox 或 polygon points
    QString     image_path;       // 标注关联的图像路径
};

/**
 * @brief 数据导入器基类
 *
 * 子类负责在后台线程解析外部数据格式，并通过 dataBatchReady 按批次把结果交给 DataManager。
 * 批次信号由 DataManager 使用阻塞队列连接接收，确保写库完成后再继续解析，降低大数据集导入时的内存占用。
 */
class DATA_API DataImporter : public QObject
{
    Q_OBJECT

public:
    static constexpr std::size_t ImportBatchImageCount = 1000;

    explicit DataImporter(dltool::database::ProjectDataBase *database, QObject *parent = nullptr);
    virtual ~DataImporter();

    /**
     * @brief 根据数据格式创建对应导入器
     * @param data_format 数据格式标识
     * @param database 项目数据库指针
     * @param parent 父对象
     * @return 导入器实例，不支持时返回 nullptr
     */
    static DataImporter *createImporter(int data_format, dltool::database::ProjectDataBase *database,
                                        QObject *parent = nullptr);

    void setTargetMethod(int method)
    {
        target_method_ = method;
    }

    void requestCancel();
    bool isCancelRequested() const;

    /**
     * @brief 开始导入数据
     * @param dataset_id 数据集 ID
     * @param image_dir 图像目录
     * @param data_dir 标注数据路径
     */
    virtual void startImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir) = 0;

signals:
    /**
     * @brief 导入流程结束信号
     */
    void importFinished(bool success, std::vector<int64_t> image_ids, std::vector<int64_t> label_class_ids);

    /**
     * @brief 数据批次准备完成信号
     *
     * @param dataset_id 数据集 ID
     * @param image_paths 本批新增图像路径；纯标注批次可以为空
     * @param image_widths 本批新增图像宽度
     * @param image_heights 本批新增图像高度
     * @param label_class_info 本批涉及的标签类别信息，名称到颜色
     * @param labels 本批标注数据，image_path 指向已导入或本批导入的图像
     * @param processed_images 当前导入器已经处理过的图像数量
     * @param total_images 导入器预计处理的图像总数；未知时为 0
     */
    void dataBatchReady(int64_t dataset_id, std::vector<QString> image_paths, std::vector<int64_t> image_widths,
                        std::vector<int64_t> image_heights, std::map<QString, QString> label_class_info,
                        std::vector<ImportedLabel> labels, int64_t processed_images, int64_t total_images);

protected:
    dltool::database::ProjectDataBase *database_;
    int                                target_method_{-1};
    std::atomic_bool                   cancel_requested_{false};

    /**
     * @brief 更新 UI 进度信息
     */
    void updateProgress(int progress, const QString &message);
};

} // namespace dltool::data
