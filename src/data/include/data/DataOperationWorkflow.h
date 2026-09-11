#pragma once

#include "common/AsyncOperationWorkflow.h"
#include "dltool/data/Export.h"

#include <QList>
#include <QObject>
#include <QString>

#include <functional>

namespace dltool::database {
class ProjectDataBase;
} // namespace dltool::database

namespace dltool::data {

/**
 * @brief 数据操作的领域适配层。
 *
 * 生命周期（句柄、取消、异常、完成投递、等待）全部由 common::AsyncOperationWorkflow
 * 提供；本层只保留领域结果类型、数据库资源适配和进度展示。
 *
 * 所有跨线程的数据操作都遵循同一条流水线：
 *
 *   提交纯值请求 -> 工作线程消费快照并执行 -> GUI 一次提交结果 -> 完成进度
 *
 * 工作函数捕获的数据必须在提交前完成快照，不能在工作线程回读 DataManager、Qt Model
 * 或 GUI 对象。需要访问数据库时应在工作线程创建归属正确的独立连接；Completion 一定
 * 在 context 所在线程执行，因此可以安全地更新 QAbstractItemModel 和 QML 状态。
 */
class DATA_API DataOperationWorkflow final
{
public:
    /** 数据操作的展示选项，字段定义见 common::AsyncOperationOptions。 */
    using Options = common::AsyncOperationOptions;

    /**
     * @brief 数据操作的领域结果。
     *
     * 提交语义由 worker 裁决：提交前取消回滚，提交后取消仍保留已提交成功。
     */
    struct DATA_API Result final : common::AsyncOperationResult
    {
    };

    using Handle    = common::AsyncOperationWorkflow::Handle;
    using HandlePtr = common::AsyncOperationWorkflow::HandlePtr;

    using Work         = std::function<void(Result &)>;
    using DatabaseWork = std::function<void(dltool::database::ProjectDataBase &, Result &)>;
    using Completion   = std::function<void(const Result &)>;

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
     * @brief 在后台线程创建独立数据库连接后执行数据库工作。
     * @param context 完成回调所属线程对象。
     * @param database_path 项目数据库路径。
     * @param options 进度与日志展示选项。
     * @param work 接收工作线程自有连接的数据库工作函数。
     * @param completion 完成回调，可为空。
     * @return 操作句柄；参数无效时返回空。
     */
    static HandlePtr startDatabase(QObject *context, const QString &database_path, Options options,
                                   DatabaseWork work, Completion completion = {});

    /**
     * @brief 在当前 context 线程排空一组数据操作的完成回调。
     *
     * 调用方必须运行在这些句柄 context 所属的 Qt 线程中。等待期间处理排除用户输入的
     * Qt 事件，因此 queued completion 可以更新其所属的模型；worker 退出但 completion
     * 尚未执行不视为完成。timeout_ms 小于 0 表示无限等待。
     *
     * @param handles 待等待的句柄列表。
     * @param timeout_ms 等待上限；负数表示无限等待。
     * @return 全部 worker 退出且完成回调已收敛返回 true。
     */
    static bool waitForCompletions(const QList<HandlePtr> &handles, int timeout_ms = -1);

private:
    static void beginProgress(const Options &options);
    static void finishProgress(const Options &options, const common::AsyncOperationResult &result);
};

} // namespace dltool::data
