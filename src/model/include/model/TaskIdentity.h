#pragma once

#include "dltool/model/Export.h"

#include <QString>
#include <QMetaType>
#include <QHashFunctions>

namespace dltool::model {

/**
 * @brief 标识一个逻辑任务的一次具体执行。
 *
 * project_id 标识当前项目运行实例，task_id 在该实例内标识逻辑任务，run_id
 * 标识该任务的一次执行。三个字段必须同时存在，外部消息和停止命令不得只
 * 使用其中的部分字段。
 */
struct MODEL_API TaskIdentity
{
    int     task_id{-1};
    QString run_id;
    QString project_id;

    bool isValid() const
    {
        return task_id >= 0 && !run_id.trimmed().isEmpty() && !project_id.trimmed().isEmpty();
    }
};

inline bool operator==(const TaskIdentity &left, const TaskIdentity &right)
{
    return left.task_id == right.task_id && left.run_id == right.run_id && left.project_id == right.project_id;
}

inline bool operator!=(const TaskIdentity &left, const TaskIdentity &right)
{
    return !(left == right);
}

inline size_t qHash(const TaskIdentity &identity, const size_t seed = 0) noexcept
{
    return ::qHash(identity.project_id, ::qHash(identity.run_id, ::qHash(identity.task_id, seed)));
}

} // namespace dltool::model

Q_DECLARE_METATYPE(dltool::model::TaskIdentity)
