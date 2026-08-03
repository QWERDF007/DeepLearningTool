#pragma once

#include "dltool/model/Export.h"

#include <QString>
#include <QStringList>

namespace dltool::model {

/**
 * @brief 外部进程启动规格，描述启动一个外部 Python 训练/测试进程所需的全部参数
 */
struct MODEL_API ExternalProcessSpec
{
    int         task_id{-1};       ///< 关联的任务 ID
    QString     program;           ///< 可执行程序路径（Python 解释器）
    QStringList arguments;         ///< 命令行参数列表
    QString     working_directory; ///< 工作目录
    QStringList python_paths;      ///< 追加到 PYTHONPATH 的路径列表
    QString     log_path;          ///< 进程输出日志文件路径
    bool        evaluation_only{false}; ///< 仅执行 C++ 评估，不启动 Python
};

} // namespace dltool::model
