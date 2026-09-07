#pragma once

#include "dltool/model/Export.h"

#include <QString>
#include <QMetaType>

namespace dltool::model {

/**
 * @brief 标识一个逻辑任务的一次具体执行。
 *
 * task_id 在当前任务中心内标识逻辑任务，run_id 标识该任务的一次执行。
 * 两者必须同时存在，外部消息和停止命令不得只使用 task_id。
 */
struct MODEL_API TaskIdentity
{
    int     task_id{-1};
    QString run_id;

    bool isValid() const
    {
        return task_id >= 0 && !run_id.trimmed().isEmpty();
    }
};

inline bool operator==(const TaskIdentity &left, const TaskIdentity &right)
{
    return left.task_id == right.task_id && left.run_id == right.run_id;
}

inline bool operator!=(const TaskIdentity &left, const TaskIdentity &right)
{
    return !(left == right);
}

} // namespace dltool::model

Q_DECLARE_METATYPE(dltool::model::TaskIdentity)
