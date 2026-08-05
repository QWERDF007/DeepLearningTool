#pragma once

#include "dltool/model/Export.h"

#include <QPair>
#include <QString>
#include <atomic>
#include <memory>

namespace dltool::model {

/**
 * @brief 读取测试任务图像文件列表。
 *
 * 文件为 CSV 格式（image_id,image_path），支持 UTF-8 BOM 与可选表头行；
 * 对文档大小与记录数量设上限，避免异常文件耗尽 GUI 进程内存。
 * @param path 文件列表路径。
 * @param rows 输出行记录（image_id -> 图像路径）。
 * @param cancel_token 协作取消令牌，置位后提前失败；可为空。
 * @param err_msg 失败时输出错误信息，可为 nullptr。
 * @return 读取成功返回 true。
 */
MODEL_API bool readEvaluationImageList(const QString &path, QList<QPair<qint64, QString>> &rows,
                                       const std::shared_ptr<std::atomic_bool> &cancel_token = {},
                                       QString                                 *err_msg      = nullptr);

} // namespace dltool::model
