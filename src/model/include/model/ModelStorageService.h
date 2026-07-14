#pragma once

#include "dltool/model/Export.h"

#include <QString>

namespace dltool::model {

/**
 * @brief 模型存储位置枚举
 */
enum class ModelStorageLocation
{
    ModelsRoot, ///< 模型根目录
    ModelRoot,  ///< 单个模型根目录
    Results,    ///< 结果目录
    Logs,       ///< 日志目录
    Weights,    ///< 权重目录
    Datasets,   ///< 数据集目录
    Configs,    ///< 配置目录
};

/**
 * @brief 清理模型路径
 * @param path 原始路径
 * @return 清理后的路径
 */
MODEL_API QString cleanModelPath(const QString &path);

/**
 * @brief 获取存储位置对应的目录名称
 * @param location 存储位置枚举
 * @return 目录名称
 */
MODEL_API QString modelStorageLocationName(ModelStorageLocation location);

/**
 * @brief 模型存储服务，管理项目下模型的目录结构（创建、删除、重命名）
 */
class MODEL_API ModelStorageService
{
public:
    /**
     * @brief 构造存储服务
     * @param project_dir 项目根目录
     */
    explicit ModelStorageService(QString project_dir = {});

    /**
     * @brief 设置项目目录
     * @param project_dir 项目根目录
     */
    void setProjectDirectory(const QString &project_dir);

    /**
     * @brief 获取项目目录
     * @return 项目根目录路径
     */
    QString projectDirectory() const;

    /**
     * @brief 获取指定位置路径
     * @param model_name 模型名称
     * @param location 存储位置
     * @return 完整路径
     */
    QString path(const QString &model_name, ModelStorageLocation location) const;

    /**
     * @brief 创建模型存储目录结构
     * @param model_name 模型名称
     * @param err_msg 错误信息输出
     * @return 创建成功返回 true
     */
    bool ensureModelStorage(const QString &model_name, QString *err_msg = nullptr) const;

    /**
     * @brief 删除模型存储目录
     * @param model_name 模型名称
     * @param err_msg 错误信息输出
     * @return 删除成功返回 true
     */
    bool removeModelStorage(const QString &model_name, QString *err_msg = nullptr) const;

    /**
     * @brief 重命名模型存储目录
     * @param old_model_name 旧模型名称
     * @param new_model_name 新模型名称
     * @param err_msg 错误信息输出
     * @return 重命名成功返回 true
     */
    bool renameModelStorage(const QString &old_model_name, const QString &new_model_name,
                            QString *err_msg = nullptr) const;

private:
    QString project_dir_; ///< 项目根目录
};

} // namespace dltool::model
