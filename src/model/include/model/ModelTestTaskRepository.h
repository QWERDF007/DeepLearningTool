#pragma once

#include "dltool/model/Export.h"
#include "model/ModelTestTask.h"

#include <QList>
#include <QString>
#include <QVariantMap>

namespace dltool::model {

/**
 * @brief 测试任务定义的持久化仓储入口。
 *
 * 负责测试任务元信息的 CRUD 操作：
 * 索引保存于模型目录下的 model.db，任务超参数和数据集选择保存于各自任务目录下的 task.db。
 * 仓储不维护运行时任务状态，也不依赖 QObject。
 */
class MODEL_API ModelTestTaskRepository
{
public:
    explicit ModelTestTaskRepository(QString project_dir = {});

    /** @brief 设置当前项目根目录。 */
    void    setProjectDirectory(const QString &project_dir);
    /** @brief 获取当前项目根目录。 */
    QString projectDirectory() const;

    /** @brief 设置项目主数据库文件路径（project.db）。 */
    void setProjectDatabasePath(const QString &project_database_path);

    /**
     * @brief 列出指定模型下的所有测试任务定义。
     * @param model_name 模型名称。
     * @param err_msg 可选错误信息输出。
     * @return 测试任务定义列表。
     */
    QList<ModelTestTaskDefinition> listTasks(const QString &model_name, QString *err_msg = nullptr) const;

    /**
     * @brief 从数据库加载单个测试任务的完整定义。
     * @param model_name 模型名称。
     * @param uuid 任务 UUID。
     * @param task 输出加载的任务定义。
     * @param err_msg 可选错误信息输出。
     * @return 成功返回 true。
     */
    bool loadTask(const QString &model_name, const QString &uuid, ModelTestTaskDefinition &task,
                  QString *err_msg = nullptr) const;

    /**
     * @brief 保存/更新测试任务定义。
     * @param model_name 模型名称。
     * @param task 待保存的任务定义。
     * @param persist_selection 是否同步写入测试数据集选择。参数自动保存时传 false，
     *        避免用内存中的编辑快照覆盖任务数据库中的已提交选择。
     * @param err_msg 可选错误信息输出。
     * @return 成功返回 true。
     */
    bool saveTask(const QString &model_name, const ModelTestTaskDefinition &task, bool persist_selection,
                  QString *err_msg = nullptr) const;

    /**
     * @brief 创建并持久化一个新的测试任务。
     * @param model_name 模型名称。
     * @param model_uuid 模型 UUID。
     * @param name 新任务名称。
     * @param test_params 初始测试参数。
     * @param dataset_selection 初始数据集选择。
     * @param task 输出创建的任务定义。
     * @param err_msg 可选错误信息输出。
     * @return 成功返回 true。
     */
    bool createTask(const QString &model_name, const QString &model_uuid, const QString &name,
                    const QVariantMap &test_params, const ModelDatasetSelection &dataset_selection,
                    ModelTestTaskDefinition &task, QString *err_msg = nullptr) const;

    /**
     * @brief 重命名指定测试任务。
     * @param model_name 模型名称。
     * @param uuid 任务 UUID。
     * @param name 新名称。
     * @param err_msg 可选错误信息输出。
     * @return 成功返回 true。
     */
    bool renameTask(const QString &model_name, const QString &uuid, const QString &name,
                    QString *err_msg = nullptr) const;

    /**
     * @brief 彻底删除测试任务（从数据库清理并删除磁盘目录）。
     * @param model_name 模型名称。
     * @param uuid 任务 UUID。
     * @param err_msg 可选错误信息输出。
     * @return 成功返回 true。
     */
    bool removeTask(const QString &model_name, const QString &uuid, QString *err_msg = nullptr) const;

    /**
     * @brief 获取指定模型对应的 SQLite 数据库路径（models/<name>/model.db）。
     * @param model_name 模型名称。
     * @return 数据库绝对路径。
     */
    QString modelDatabasePath(const QString &model_name) const;

    /**
     * @brief 校验任务名称合法性（静态实用方法）。
     * @param name 待校验名称。
     * @return 错误信息，合法返回空字符串。
     */
    static QString validateTaskName(const QString &name);

    /**
     * @brief 根据任务名称推导磁盘工作目录名称。
     * @param name 任务名称。
     * @return 目录名称。
     */
    static QString directoryNameForTask(const QString &name);

private:
    bool ensureTaskRoot(const QString &model_name, const ModelTestTaskDefinition &task,
                        QString *err_msg = nullptr) const;

    QString project_dir_;           ///< 项目根目录。
    QString project_database_path_; ///< 项目数据库路径。
};

} // namespace dltool::model
