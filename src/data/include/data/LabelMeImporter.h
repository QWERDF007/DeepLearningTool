#pragma once

#include "DataImporter.h"

#include <QPointF>
#include <QThread>
#include <QVariantMap>
#include <map>
#include <set>
#include <vector>

namespace dltool::data {

/**
 * @brief LabelMe 数据格式导入器
 * 
 * 负责解析 LabelMe JSON 格式的标注文件，并将数据导入到数据库中。
 * LabelMe 格式：每个图像对应一个 JSON 文件，包含图像路径、标注形状和标签类别信息。
 */
class DATA_API LabelMeImporter : public DataImporter
{
    Q_OBJECT

public:
    /**
     * @brief LabelMe 形状数据结构
     */
    struct LabelMeShape
    {
        QString              label;      // 标签类别名称
        QString              shape_type; // 形状类型: rectangle, polygon, circle 等
        std::vector<QPointF> points;     // 坐标点列表
    };

    /**
     * @brief LabelMe 数据结构
     */
    struct LabelMeData
    {
        QString                   image_path;   // 图像文件路径
        int                       image_width;  // 图像宽度
        int                       image_height; // 图像高度
        std::vector<LabelMeShape> shapes;       // 标注形状列表
    };

    explicit LabelMeImporter(ProjectDataBase *database, QObject *parent = nullptr);
    ~LabelMeImporter() override;

    /**
     * @brief 开始导入 LabelMe 数据
     * @param dataset_id 数据集 ID
     * @param image_dir 图像目录路径
     * @param data_dir LabelMe JSON 文件目录路径
     * 
     * 该方法会创建一个工作线程，在后台执行导入操作。
     * 导入完成后会发射 importFinished 信号。
     */
    void startImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir) override;

signals:
    /**
     * @brief 数据解析完成信号
     * @param success 是否成功
     * @param dataset_id 数据集 ID
     * @param parsed_data 解析的数据
     * @param label_class_names 标签类别名称集合
     */
    void dataParsed(bool success, int64_t dataset_id, std::vector<LabelMeData> parsed_data,
                    std::set<QString> label_class_names);

private:
    /**
     * @brief 扫描目录中的所有 JSON 文件
     */
    std::vector<QString> scanJsonFiles(const QString &data_dir);

    /**
     * @brief 解析单个 LabelMe JSON 文件
     */
    bool parseLabelMeJson(const QString &json_path, LabelMeData &data);

    /**
     * @brief 提取所有唯一的标签类别名称
     */
    std::set<QString> extractLabelClasses(const std::vector<LabelMeData> &all_data);

    /**
     * @brief 执行数据解析（在工作线程中运行）
     */
    void doImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir);
};

} // namespace dltool::data
