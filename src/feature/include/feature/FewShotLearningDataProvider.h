#pragma once

#include "dltool/feature/Export.h"

#include <QMetaObject>
#include <QString>
#include <QVariantMap>
#include <cstdint>
#include <functional>
#include <vector>

class QObject;

namespace dltool::feature {

class FEATURE_API FewShotLearningDataProvider
{
public:
    using ImportFinishedHandler = std::function<void(bool, const QString &)>;

    virtual ~FewShotLearningDataProvider() = default;

    /**
     * @brief 获取深度学习方法类型
     * @return 方法类型（检测/分割）
     */
    virtual int method() const = 0;

    /**
     * @brief 获取数据库路径
     * @return 数据库文件路径
     */
    virtual QString databasePath() const = 0;

    /**
     * @brief 获取所有图像 ID
     * @return 图像 ID 列表
     */
    virtual std::vector<int64_t> allImageIds() const = 0;

    /**
     * @brief 获取指定图像的路径
     * @param image_id 图像 ID
     * @return 图像文件路径
     */
    virtual QString imagePath(int64_t image_id) const = 0;

    /**
     * @brief 获取图像所属的数据集 ID
     * @param image_id 图像 ID
     * @return 数据集 ID
     */
    virtual int64_t imageDatasetId(int64_t image_id) const = 0;

    /**
     * @brief 获取所有标注 ID
     * @return 标注 ID 列表
     */
    virtual std::vector<int64_t> allLabelIds() const = 0;

    /**
     * @brief 获取标注所属的图像 ID
     * @param label_id 标注 ID
     * @return 图像 ID
     */
    virtual int64_t labelImageId(int64_t label_id) const = 0;

    /**
     * @brief 获取标注的类别 ID
     * @param label_id 标注 ID
     * @return 类别 ID
     */
    virtual int64_t labelClassId(int64_t label_id) const = 0;

    /**
     * @brief 获取标注数据
     * @param label_id 标注 ID
     * @return 包含标注信息的 QVariantMap
     */
    virtual QVariantMap labelData(int64_t label_id) const = 0;

    /**
     * @brief 获取类别名称
     * @param label_class_id 类别 ID
     * @return 类别名称
     */
    virtual QString labelClassName(int64_t label_class_id) const = 0;

    /**
     * @brief 获取数据集名称
     * @param dataset_id 数据集 ID
     * @return 数据集名称
     */
    virtual QString datasetName(int64_t dataset_id) const = 0;

    /**
     * @brief 导入 Mask 数据
     * @param dataset_id 目标数据集 ID
     * @param image_manifest_path 图像清单文件路径
     * @param prediction_output_dir 预测输出目录
     */
    virtual void importMaskData(int64_t dataset_id, const QString &image_manifest_path,
                                const QString &prediction_output_dir) = 0;

    /**
     * @brief 连接导入完成信号
     * @param context 上下文 QObject
     * @param handler 导入完成回调
     * @return 连接句柄
     */
    virtual QMetaObject::Connection connectImportFinished(QObject *context, ImportFinishedHandler handler) = 0;

    /**
     * @brief 断开导入完成信号
     * @param connection 连接句柄
     */
    virtual void disconnectImportFinished(const QMetaObject::Connection &connection) = 0;
};

} // namespace dltool::feature
