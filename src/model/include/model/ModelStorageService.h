#pragma once

#include "dltool/model/Export.h"
#include "model/ModelLifecycle.h"

#include <QString>

namespace dltool::model {

/**
 * @brief 模型磁盘存储位置枚举。
 */
enum class ModelStorageLocation
{
    ModelsRoot, ///< 所有模型根目录（models/）
    ModelRoot,  ///< 单个模型根目录（models/<model_name>/）
    Train,      ///< 训练产物根目录（models/<model_name>/train/）
    Test,       ///< 测试产物根目录（models/<model_name>/test/）
    Logs,       ///< 运行日志目录
    Weights,    ///< 权重文件目录（models/<model_name>/train/weights/）
    Datasets,   ///< 导出数据集目录
};

/**
 * @brief 清理与规范化模型文件路径。
 * @param path 原始路径。
 * @return 清理后的规范化路径。
 */
MODEL_API QString cleanModelPath(const QString &path);

/**
 * @brief 获取存储位置对应的目录标准名称。
 * @param location 存储位置枚举。
 * @return 目录名称字符串。
 */
MODEL_API QString modelStorageLocationName(ModelStorageLocation location);

/**
 * @brief 任务文件与目录路径汇总结构体。
 */
struct MODEL_API ModelTaskPaths
{
    QString model_root;     ///< 模型根目录。
    QString task_root;      ///< 任务根目录。
    QString database_path;  ///< 任务数据库路径（task.db）。
    QString dataset_dir;    ///< 数据集导出目录。
    QString weight_dir;     ///< 权重输出目录。
    QString log_dir;        ///< 日志存放目录。
    QString log_path;       ///< 运行日志文件路径。
    QString prediction_dir; ///< 预测结果输出目录。
};

/**
 * @brief 模型磁盘存储服务，负责管理模型与测试任务的标准目录布局（创建、删除、重命名与路径解析）。
 */
class MODEL_API ModelStorageService final : public IModelStorageAdapter
{
public:
    /**
     * @brief 构造模型存储服务。
     * @param project_dir 项目根目录路径。
     */
    explicit ModelStorageService(QString project_dir = {});
    ~ModelStorageService() override = default;

    /**
     * @brief 设置当前项目根目录。
     * @param project_dir 项目根目录路径。
     */
    void setProjectDirectory(const QString &project_dir);

    /**
     * @brief 获取当前项目根目录。
     * @return 项目根目录路径。
     */
    QString projectDirectory() const;

    QString modelsRootPath() const override;
    QString modelRoot(const QString &model_name) const override;
    QString modelDatabasePathAt(const QString &model_root) const override;
    QString trainWeightsPathAt(const QString &model_root) const override;

    QString operationRoot() const override;
    QString operationStagingRoot(const QString &operation_id) const override;
    QString operationQuarantineRoot(const QString &operation_id) const override;
    QString operationJournalPath(const QString &operation_id) const override;

    /** @brief 获取模型主数据库路径（models/<model_name>/model.db）。 */
    QString modelDatabasePath(const QString &model_name) const;
    /** @brief 获取模型共享数据集清单路径。 */
    QString sharedDatasetPath(const QString &model_name) const;

    /**
     * @brief 获取指定存储位置的完整路径。
     * @param model_name 模型名称。
     * @param location 存储位置枚举。
     * @return 绝对路径字符串。
     */
    QString path(const QString &model_name, ModelStorageLocation location) const;

    // 训练相关路径
    QString trainRoot(const QString &model_name) const;
    QString trainWeightsPath(const QString &model_name) const;
    QString trainLogsPath(const QString &model_name) const;
    QString trainDatasetPath(const QString &model_name) const;
    QString trainLogPath(const QString &model_name) const;

    // 测试相关路径
    QString testRoot(const QString &model_name) const;
    QString testTaskRoot(const QString &model_name, const QString &task_directory) const;
    QString testTaskDatabasePath(const QString &model_name, const QString &task_directory) const;
    QString testTaskFileListPath(const QString &model_name, const QString &task_directory) const;
    QString testTaskPredictionPath(const QString &model_name, const QString &task_directory) const;
    QString testTaskLogPath(const QString &model_name, const QString &task_directory) const;

    /** @brief 获取训练任务的标准路径结构。 */
    ModelTaskPaths trainPaths(const QString &model_name) const;
    /** @brief 获取测试任务的标准路径结构。 */
    ModelTaskPaths testPaths(const QString &model_name, const QString &task_directory) const;

    /** @brief 确保训练所需目录已创建。 */
    bool ensureTrainStorage(const QString &model_name, QString *err_msg = nullptr) const;
    /** @brief 确保测试根目录已创建。 */
    bool ensureTestStorage(const QString &model_name, QString *err_msg = nullptr) const;
    /** @brief 确保指定测试任务的目录结构已创建。 */
    bool ensureTestTaskStorage(const QString &model_name, const QString &task_directory,
                               QString *err_msg = nullptr) const;
    /** @brief 在指定目录（如 staging 目录）创建测试任务目录结构。 */
    bool ensureTestTaskStorageAt(const QString &task_root, QString *err_msg = nullptr) const;

    // 测试任务操作与恢复路径
    QString testTaskOperationRoot(const QString &model_name) const;
    QString testTaskOperationJournalPath(const QString &model_name, const QString &operation_id) const;
    QString testTaskOperationStagingRoot(const QString &model_name, const QString &operation_id) const;
    QString testTaskOperationQuarantineRoot(const QString &model_name, const QString &operation_id) const;

    /**
     * @brief 创建模型完整的存储目录结构。
     * @param model_name 模型名称。
     * @param err_msg 错误信息输出。
     * @return 成功返回 true。
     */
    bool ensureModelStorage(const QString &model_name, QString *err_msg = nullptr) const;

    /** @brief 在模型根目录或 staging 根目录创建完整模型存储。 */
    bool ensureModelStorageAt(const QString &model_root, QString *err_msg = nullptr) const override;

    /** @brief 复制目录内容，不复制源目录本身。 */
    bool copyDirectoryContents(const QString &source, const QString &target,
                               QString *err_msg = nullptr,
                               std::function<bool()> is_cancelled = nullptr) const override;

    /** @brief 在模型存储根目录内移动目录。 */
    bool moveDirectory(const QString &source, const QString &target,
                       QString *err_msg = nullptr) override;

    /** @brief 删除模型存储根目录内的目录。 */
    bool removeDirectory(const QString &root, QString *err_msg = nullptr) const override;

    /**
     * @brief 递归删除模型的磁盘存储目录。
     * @param model_name 模型名称。
     * @param err_msg 错误信息输出。
     * @return 成功返回 true。
     */
    bool removeModelStorage(const QString &model_name, QString *err_msg = nullptr) const;

    /**
     * @brief 重命名模型存储目录。
     * @param old_model_name 旧模型名称。
     * @param new_model_name 新模型名称。
     * @param err_msg 错误信息输出。
     * @return 成功返回 true。
     */
    bool renameModelStorage(const QString &old_model_name, const QString &new_model_name,
                            QString *err_msg = nullptr) const;

private:
    QString project_dir_; ///< 项目根目录路径。
};

} // namespace dltool::model
