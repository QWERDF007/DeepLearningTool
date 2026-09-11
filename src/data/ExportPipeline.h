#pragma once

/**
 * @file ExportPipeline.h
 * @brief data 模块私有：统一导出骨架。
 *
 * 只供 src/data 内部实现使用，不进入消费者 include 路径。
 */

#include "data/DataIO.h"

#include <functional>

namespace dltool::data::detail {

/// 导出运行结果：成功时 message 为摘要、done_hint 为终态进度文案；失败时 message 为错误。
struct ExportRunResult
{
    bool    success{false};
    QString message;
    QString done_hint;
};

/**
 * @brief 统一导出骨架：同源冲突检测 → 安全发布作用域 → staging 写入 → 产物校验 → 原子发布。
 *
 * writer 负责把冻结数据集写入 staging_dir（含格式子目录创建与取消检查）；
 * 失败时置 error 并返回 false，成功时填 summary 与 done_hint 并返回 true。
 * writer 抛出的异常统一折叠为失败结果，文案为「<format_label> 导出失败: <what>」。
 */
ExportRunResult runExportPipeline(int data_format, const ExportDataset &dataset, const QString &output_dir,
                                  const QVariantMap &options, const QString &format_label,
                                  const std::function<bool(const QString &staging_dir, QString &error,
                                                           QString &summary, QString &done_hint)> &writer);

} // namespace dltool::data::detail
