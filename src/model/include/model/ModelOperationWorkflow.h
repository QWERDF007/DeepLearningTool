#pragma once

#include "common/AsyncOperationWorkflow.h"
#include "dltool/model/Export.h"
#include "model/ModelLifecycle.h"

#include <QList>
#include <QObject>
#include <QString>

#include <functional>

namespace dltool::model {

/**
 * @brief 模型操作的领域适配层。
 *
 * 生命周期（句柄、取消、异常、完成投递、等待）全部由 common::AsyncOperationWorkflow
 * 提供；本层只保留模型领域结果、生命周期资源适配和进度展示。
 *
 * 所有耗时的模型操作（如模型复制、恢复扫描等）遵循流水线：
 *   提交纯值快照 -> 工作线程自有数据库与文件服务 -> GUI 线程一次性提交结果 -> 进度收尾
 *
 * 工作线程禁止回读 ModelManager 或 GUI 对象。
 */
class MODEL_API ModelOperationWorkflow final
{
public:
    /** 模型操作的展示选项，字段定义见 common::AsyncOperationOptions。 */
    using Options = common::AsyncOperationOptions;

    /**
     * @brief 模型操作的领域结果。
     *
     * 提交语义由 worker 裁决：提交前取消回滚，提交后取消仍保留已提交成功。
     */
    struct MODEL_API Result final : common::AsyncOperationResult
    {
        ModelLifecycleResult lifecycle_result; ///< 生命周期执行结果。
        qint64               model_id{-1};     ///< 受影响的模型 ID。
        QString              uuid;             ///< 受影响的模型身份。
        QString              name;             ///< 受影响的模型名称。
    };

    using Handle    = common::AsyncOperationWorkflow::Handle;
    using HandlePtr = common::AsyncOperationWorkflow::HandlePtr;

    using Work          = std::function<void(Result &)>;
    using LifecycleWork = std::function<void(ModelLifecycle &lifecycle, Result &result)>;
    using Completion    = std::function<void(const Result &)>;

    /**
     * @brief 在后台线程执行任意工作，并在 context 线程回调完成阶段。
     * @param context 完成回调所属线程对象；为空时不执行操作。
     * @param options 进度与日志展示选项。
     * @param work 后台工作函数，必须使用冻结的纯值输入。
     * @param completion 完成回调，可为空。
     * @return 操作句柄；参数无效时返回空。
     */
    static HandlePtr start(QObject *context, Options options, Work work, Completion completion = {});

    /**
     * @brief 在后台线程创建独立的数据库连接与存储服务，执行 ModelLifecycle 相关操作。
     * @param context 完成回调所属线程对象。
     * @param project_database_path 项目数据库路径。
     * @param project_dir 项目目录。
     * @param options 进度与日志展示选项。
     * @param work 接收工作线程自有生命周期对象的执行函数。
     * @param completion 完成回调，可为空。
     * @return 操作句柄；参数无效时返回空。
     */
    static HandlePtr startLifecycle(QObject *context, const QString &project_database_path,
                                    const QString &project_dir, Options options,
                                    LifecycleWork work, Completion completion = {});

    /**
     * @brief 异步复制模型。
     * @param context 完成回调所属线程对象。
     * @param project_database_path 项目数据库路径。
     * @param project_dir 项目目录。
     * @param source 源模型记录。
     * @param target 目标模型记录。
     * @param copy_train_weights 是否复制训练权重。
     * @param options 进度与日志展示选项。
     * @param completion 完成回调，可为空。
     * @return 操作句柄；参数无效时返回空。
     */
    static HandlePtr startCopy(QObject *context, const QString &project_database_path,
                               const QString &project_dir, const ModelLifecycleRecord &source,
                               const ModelLifecycleRecord &target, bool copy_train_weights,
                               Options options, Completion completion = {});

    /**
     * @brief 异步恢复未完成的模型操作。
     * @param context 完成回调所属线程对象。
     * @param project_database_path 项目数据库路径。
     * @param project_dir 项目目录。
     * @param options 进度与日志展示选项。
     * @param completion 完成回调，可为空。
     * @return 操作句柄；参数无效时返回空。
     */
    static HandlePtr startRecovery(QObject *context, const QString &project_database_path,
                                   const QString &project_dir, Options options,
                                   Completion completion = {});

    /**
     * @brief 在当前 context 线程排空一组模型操作的完成回调。
     * @param handles 待等待的句柄列表。
     * @param timeout_ms 等待上限；负数表示无限等待。
     * @return 全部 worker 退出且完成通知已收敛返回 true。
     */
    static bool waitForCompletions(const QList<HandlePtr> &handles, int timeout_ms = -1);

private:
    static void beginProgress(const Options &options);
    static void finishProgress(const Options &options, const common::AsyncOperationResult &result);
};

} // namespace dltool::model
