#pragma once

#include "dltool/model/Export.h"

#include <QVariantMap>
#include <set>
#include <utility>
#include <vector>

namespace dltool::model {

class IModel;

/**
 * @brief 数据集选择，记录单个划分（训练/验证/测试）中选中的数据集 ID 和标签类别
 */
struct MODEL_API ModelDatasetSelection
{
    std::set<qint64>                    dataset_ids;   ///< 选中的数据集 ID 集合
    std::set<std::pair<qint64, qint64>> label_classes; ///< 选中的 (数据集ID, 标签类别ID) 对集合

    /**
     * @brief 检查选择是否为空
     * @return 为空返回 true
     */
    bool isEmpty() const;

    /**
     * @brief 检查是否包含指定数据集
     * @param dataset_id 数据集 ID
     * @return 包含返回 true
     */
    bool containsDataset(qint64 dataset_id) const;

    /**
     * @brief 检查是否包含指定标签类别
     * @param dataset_id 数据集 ID
     * @param label_class_id 标签类别 ID
     * @return 包含返回 true
     */
    bool containsLabelClass(qint64 dataset_id, qint64 label_class_id) const;

    /**
     * @brief 检查是否包含指定数据集或标签类别
     * @param dataset_id 数据集 ID
     * @param label_class_id 标签类别 ID
     * @return 包含返回 true
     */
    bool contains(qint64 dataset_id, qint64 label_class_id) const;
};

/**
 * @brief 模型数据集选择集合，包含训练、验证、测试三个划分的选择
 */
struct MODEL_API ModelDatasetSelections
{
    ModelDatasetSelection train;      ///< 训练集选择
    ModelDatasetSelection validation; ///< 验证集选择
    ModelDatasetSelection test;       ///< 测试集选择
};

/**
 * @brief 读取模型当前的数据集选择。
 * @param model 模型实例
 * @return 数据集选择集合
 */
MODEL_API ModelDatasetSelections modelDatasetSelections(IModel *model);

/**
 * @brief 汇总训练、验证、测试选择中涉及的数据集 ID。
 */
MODEL_API std::vector<int64_t> selectedDatasetIds(const ModelDatasetSelections &selections);

/**
 * @brief 将完整数据集选择转换为稳定的 QVariant map。
 *
 * 该表示同时用于持久化选择和生成当前测试的图像列表，避免任务准备阶段
 * 再维护一份不同的选择转换逻辑。
 */
MODEL_API QVariantMap modelDatasetSelectionsMap(const ModelDatasetSelections &selections);

/**
 * @brief 获取数据集选择配置文件路径
 * @param dataset_dir 数据集目录
 * @return 配置文件路径
 */
MODEL_API QString modelDatasetSelectionsPath(const QString &dataset_dir);

/**
 * @brief 写入数据集选择配置文件
 * @param dataset_dir 数据集目录
 * @param selections 数据集选择
 * @param err_msg 错误信息输出
 * @return 写入成功返回 true
 */
MODEL_API bool writeModelDatasetSelectionsFile(const QString &dataset_dir, const ModelDatasetSelections &selections,
                                               QString *err_msg = nullptr);

/**
 * @brief 读取数据集选择配置文件
 * @param dataset_dir 数据集目录
 * @return 数据集选择键值对
 */
MODEL_API QVariantMap readModelDatasetSelectionsFile(const QString &dataset_dir);

/**
 * @brief 将数据集选择应用到模型
 * @param model 模型实例
 * @param dataset_selections 数据集选择键值对
 */
MODEL_API void applyModelDatasetSelections(IModel *model, const QVariantMap &dataset_selections);

} // namespace dltool::model
