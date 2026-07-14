#pragma once

#include "dltool/model/Export.h"
#include "model/ModelDatasetSelection.h"
#include "model/ModelTaskTypes.h"

#include <QString>
#include <QVariantMap>
#include <vector>

namespace dltool::model {

/**
 * @brief 模型数据集源抽象接口，提供图像和标签数据的只读访问
 */
class MODEL_API IModelDatasetSource
{
public:
    virtual ~IModelDatasetSource() = default;

    /**
     * @brief 获取所有图像 ID
     * @return 图像 ID 列表
     */
    virtual std::vector<int64_t> allImageIds() const = 0;

    /**
     * @brief 获取图像所属数据集 ID
     * @param image_id 图像 ID
     * @return 数据集 ID
     */
    virtual qint64 imageDatasetId(qint64 image_id) const = 0;

    /**
     * @brief 获取图像文件路径
     * @param image_id 图像 ID
     * @return 图像路径
     */
    virtual QString imagePath(qint64 image_id) const = 0;

    /**
     * @brief 获取图像级别标签数据
     * @param image_id 图像 ID
     * @return 标签数据键值对
     */
    virtual QVariantMap imageLevelLabelData(qint64 image_id) const = 0;

    /**
     * @brief 获取图像的所有标签 ID
     * @param image_id 图像 ID
     * @return 标签 ID 列表
     */
    virtual std::vector<int64_t> imageLabelIds(qint64 image_id) const = 0;

    /**
     * @brief 获取标签的类别 ID
     * @param label_id 标签 ID
     * @return 标签类别 ID
     */
    virtual qint64 labelClassId(qint64 label_id) const = 0;

    /**
     * @brief 获取标签数据
     * @param label_id 标签 ID
     * @return 标签数据键值对
     */
    virtual QVariantMap labelData(qint64 label_id) const = 0;

    /**
     * @brief 获取标签类别名称
     * @param label_class_id 标签类别 ID
     * @return 类别名称
     */
    virtual QString labelClassName(qint64 label_class_id) const = 0;

    /**
     * @brief 获取标签类别分组
     * @param label_class_id 标签类别 ID
     * @return 分组名称
     */
    virtual QString labelClassGroup(qint64 label_class_id) const = 0;

    /**
     * @brief 获取数据集名称
     * @param dataset_id 数据集 ID
     * @return 数据集名称
     */
    virtual QString datasetName(qint64 dataset_id) const = 0;
};

/**
 * @brief 模型数据集导出请求，包含导出所需的所有上下文信息
 */
struct MODEL_API ModelDatasetExportRequest
{
    int                        method{-1};                        ///< 深度学习方法
    QString                    framework_name;                    ///< 框架名称
    QString                    model_architecture;                ///< 模型架构
    QString                    model_uuid;                        ///< 模型 UUID
    ModelTaskType              task_type{ModelTaskType::Unknown}; ///< 任务类型
    QString                    dataset_dir;                       ///< 数据集输出目录
    ModelDatasetSelections     selections;                        ///< 数据集选择
    const IModelDatasetSource *source{nullptr};                   ///< 数据源
};

/**
 * @brief 模型数据集组织器，将选中的数据集按框架要求的格式导出到磁盘
 */
class MODEL_API ModelDatasetOrganizer
{
public:
    /**
     * @brief 执行数据集组织导出
     * @param request 导出请求
     * @param err_msg 错误信息输出
     * @return 数据集配置键值对
     */
    static QVariantMap organize(const ModelDatasetExportRequest &request, QString *err_msg = nullptr);
};

} // namespace dltool::model
