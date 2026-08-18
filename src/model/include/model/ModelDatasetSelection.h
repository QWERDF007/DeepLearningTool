#pragma once

#include "database/ModelDatabaseTypes.h"
#include "dltool/model/Export.h"

#include <QList>
#include <QVariantMap>
#include <functional>
#include <set>
#include <utility>
#include <vector>

namespace dltool::model {

class IModel;

/**
 * @brief 单个数据集划分（训练/验证/测试）中选中的数据集 ID 和标签类别集合。
 */
struct MODEL_API ModelDatasetSelection
{
    std::set<qint64>                    dataset_ids;   ///< 选中的数据集 ID 集合。
    std::set<std::pair<qint64, qint64>> label_classes; ///< 选中的 (数据集 ID, 标签类别 ID) 集合。

    /**
     * @brief 检查选择是否为空。
     * @return 为空返回 true。
     */
    bool isEmpty() const;

    /**
     * @brief 检查是否包含指定数据集。
     * @param dataset_id 数据集 ID。
     * @return 包含返回 true。
     */
    bool containsDataset(qint64 dataset_id) const;

    /**
     * @brief 检查是否包含指定标签类别。
     * @param dataset_id 数据集 ID。
     * @param label_class_id 标签类别 ID。
     * @return 包含返回 true。
     */
    bool containsLabelClass(qint64 dataset_id, qint64 label_class_id) const;

    /**
     * @brief 检查是否包含指定数据集或标签类别。
     * @param dataset_id 数据集 ID。
     * @param label_class_id 标签类别 ID。
     * @return 包含返回 true。
     */
    bool contains(qint64 dataset_id, qint64 label_class_id) const;
};

/**
 * @brief 模型数据集选择汇总结构体，包含训练集、验证集、测试集三个独立划分。
 */
struct MODEL_API ModelDatasetSelections
{
    ModelDatasetSelection train;      ///< 训练集选择。
    ModelDatasetSelection validation; ///< 验证集选择。
    ModelDatasetSelection test;       ///< 测试集选择。
};

/**
 * @brief 从模型实例读取当前的数据集选择。
 * @param model 模型实例指针。
 * @return 数据集选择集合。
 */
MODEL_API ModelDatasetSelections modelDatasetSelections(IModel *model);

/**
 * @brief 汇总训练、验证、测试划分中涉及的所有去重数据集 ID。
 * @param selections 数据集选择汇总。
 * @return 数据集 ID 向量。
 */
MODEL_API std::vector<int64_t> selectedDatasetIds(const ModelDatasetSelections &selections);

/**
 * @brief 将完整数据集选择转换为规范化的 QVariantMap 字典。
 * @param selections 数据集选择汇总。
 * @return 字典结构。
 */
MODEL_API QVariantMap modelDatasetSelectionsMap(const ModelDatasetSelections &selections);

/**
 * @brief 将数据集选择字典反序列化应用到模型对象中。
 * @param model 目标模型实例。
 * @param dataset_selections 数据集选择字典。
 */
MODEL_API void applyModelDatasetSelections(IModel *model, const QVariantMap &dataset_selections);

/**
 * @brief 将内存数据集选择模型转换为可持久化入库的数据库记录列表。
 * @param selections 内存数据集选择汇总。
 * @param dataset_class_ids_resolver 可选的数据集类别解析回调（用于整数据集展开）。
 * @return 数据库记录列表。
 */
MODEL_API QList<dltool::database::DatasetSelectionRecord> databaseDatasetSelections(
    const ModelDatasetSelections                          &selections,
    const std::function<QList<qint64>(qint64 dataset_id)> &dataset_class_ids_resolver = {});

/**
 * @brief 从数据库记录反序列化还原数据集选择模型。
 * @param records 数据库记录列表。
 * @return 数据集选择汇总结构。
 */
MODEL_API ModelDatasetSelections
    modelDatasetSelectionsFromDatabase(const QList<dltool::database::DatasetSelectionRecord> &records);

} // namespace dltool::model
