#pragma once

#include "dltool/model/Export.h"
#include "model/ModelStorageService.h"
#include "model/ModelTaskTypes.h"

#include <QString>
#include <QVariantMap>

namespace dltool::model {

class IModel;

/**
 * @brief 模型任务配置文件类型枚举
 */
enum class ModelTaskConfigFile
{
    Train, ///< 训练配置
    Test,  ///< 测试配置
};

/**
 * @brief 模型任务配置字段枚举
 */
enum class ModelTaskConfigField
{
    ModelUuid,
    ModelName,
    TaskType,
    Framework,
    ModelArchitecture,
    ModelDir,
    ResultDir,
    LogDir,
    WeightDir,
    Datasets,
    TrainParams,
    TestParams,
    Trainer,
    Inference,
    OutputDir,
};

/**
 * @brief 已加载的模型任务配置
 */
struct MODEL_API LoadedModelTaskConfigs
{
    QString     model_uuid;   ///< 模型 UUID
    QVariantMap train_params; ///< 训练参数
    QVariantMap test_params;  ///< 测试参数
};

/**
 * @brief 获取任务配置文件名
 * @param file 配置文件类型
 * @return 文件名
 */
MODEL_API QString modelTaskConfigFileName(ModelTaskConfigFile file);

/**
 * @brief 获取任务配置字段名
 * @param field 配置字段
 * @return 字段名
 */
MODEL_API QString modelTaskConfigFieldName(ModelTaskConfigField field);

/**
 * @brief 模型任务配置服务，负责构建、写入和加载任务的 YAML 配置文件
 */
class MODEL_API ModelTaskConfigService
{
public:
    /**
     * @brief 构造任务配置服务
     * @param project_dir 项目根目录
     */
    explicit ModelTaskConfigService(QString project_dir = {});

    /**
     * @brief 设置项目目录
     * @param project_dir 项目根目录
     */
    void setProjectDirectory(const QString &project_dir);

    /**
     * @brief 获取配置文件完整路径
     * @param model_name 模型名称
     * @param file 配置文件类型
     * @return 完整路径
     */
    QString configPath(const QString &model_name, ModelTaskConfigFile file) const;

    /**
     * @brief 加载模型任务配置
     * @param model_uuid 模型 UUID
     * @param model_name 模型名称
     * @return 已加载的配置
     */
    LoadedModelTaskConfigs load(const QString &model_uuid, const QString &model_name) const;

    /**
     * @brief 构建任务配置
     * @param model 模型实例
     * @param model_name 模型名称
     * @param task_type 任务类型
     * @param datasets 数据集配置
     * @return 配置键值对
     */
    QVariantMap build(IModel *model, const QString &model_name, ModelTaskType task_type,
                      const QVariantMap &datasets) const;

    /**
     * @brief 写入任务配置文件
     * @param model_name 模型名称
     * @param task_type 任务类型
     * @param config 配置内容
     * @param err_msg 错误信息输出
     * @return 写入的配置文件路径
     */
    QString write(const QString &model_name, ModelTaskType task_type, const QVariantMap &config,
                  QString *err_msg = nullptr) const;

private:
    /**
     * @brief 从 YAML 文件读取参数
     * @param path 文件路径
     * @param field 参数字段
     * @return 参数键值对
     */
    QVariantMap readParams(const QString &path, ModelTaskConfigField field) const;

    ModelStorageService storage_; ///< 模型存储服务
};

} // namespace dltool::model
