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
 * 
 * 职责：
 * - 扫描和解析 LabelMe JSON 文件
 * - 转换 LabelMe 形状为系统标注格式
 * - 生成标签类别的默认颜色
 * - 验证图像文件和尺寸
 * - 发射 dataReady 信号，提供完整的处理后数据
 * 
 * 该类封装了所有 LabelMe 特定的逻辑，DataManager 不需要了解 LabelMe 格式的细节。
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

    /**
     * @brief 将 LabelMe 形状转换为系统标注数据
     * 
     * 该方法封装了 LabelMe 特定的形状转换逻辑。
     * 支持的形状类型：rectangle、polygon
     * 
     * @param shape LabelMe 形状数据
     * @param image_width 图像宽度（用于验证坐标）
     * @param image_height 图像高度（用于验证坐标）
     * @return 标注数据映射（包含 x, y, width, height），如果转换失败则返回空映射
     */
    QVariantMap convertShapeToLabelData(const LabelMeShape &shape, int image_width, int image_height);

    /**
     * @brief 生成默认颜色
     * 
     * 使用黄金比例生成视觉上区分度高的颜色序列。
     * 
     * @param index 颜色索引
     * @return 十六进制颜色字符串（例如 "#FF5733"）
     */
    QString generateDefaultColor(int index);

    /**
     * @brief 处理解析的数据并发射 dataReady 信号
     * 
     * 该方法是重构的核心：将所有 LabelMe 特定的数据处理逻辑封装在导入器中。
     * DataManager 只需要接收处理后的数据并插入数据库。
     * 
     * @param dataset_id 数据集 ID
     * @param parsed_data 解析的 LabelMe 数据
     * @param label_class_names 标签类别名称集合
     * 
     * 该方法将：
     * 1. 转换形状为标注数据
     * 2. 为新标签类别生成颜色
     * 3. 准备所有数据结构
     * 4. 发射 dataReady 信号
     */
    void processAndEmitData(int64_t dataset_id, const std::vector<LabelMeData> &parsed_data,
                            const std::set<QString> &label_class_names);
};

} // namespace dltool::data
