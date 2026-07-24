#pragma once

#include "dltool/data/Export.h"

#include <QObject>
#include <QString>
#include <functional>

namespace dltool::database {
class ProjectDataBase;
} // namespace dltool::database

namespace dltool::data {

/**
 * @brief 统一的数据操作生命周期。
 *
 * 所有跨线程的数据操作都遵循同一条流水线：
 *
 *   GUI 提交 ID/参数 -> 工作线程 const 读取和执行 -> GUI 一次提交结果 -> 完成进度
 *
 * GUI 线程不复制已有图像、标注或 Tag 实体到工作线程。数据操作运行期间，DataManager
 * 阻断所有内存写入口，因此 Work 可以通过只读接口读取稳定的内存实体，并访问自己的
 * 数据库连接或文件。新增实体等操作增量由 Work 返回，Completion 一定在 context 所在线程
 * 执行，因此可以安全地更新 QAbstractItemModel 和 QML 状态。
 */
class DATA_API DataOperationWorkflow final
{
public:
    struct Result
    {
        bool    success{false};
        QString error;
        qint64  elapsed_ms{0};
    };

    struct Options
    {
        QString title;
        QString start_message;
        int     initial_progress{5};
        bool    manage_progress{true};
    };

    using Work       = std::function<void(Result &)>;
    using DatabaseWork = std::function<void(dltool::database::ProjectDataBase &, Result &)>;
    using Completion = std::function<void(const Result &)>;

    /**
     * @brief 在后台线程执行任意工作，并在 context 线程回调完成阶段。
     */
    static void start(QObject *context, Options options, Work work, Completion completion = {});

    /**
     * @brief 在后台线程创建独立数据库连接后执行数据库工作。
     */
    static void startDatabase(QObject *context, const QString &database_path, Options options,
                              DatabaseWork work, Completion completion = {});

private:
    static void beginProgress(const Options &options);
    static void finishProgress(const Options &options, const Result &result);
};

} // namespace dltool::data
