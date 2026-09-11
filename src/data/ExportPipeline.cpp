#include "ExportPipeline.h"

#include <spdlog/spdlog.h>

#include <exception>

namespace dltool::data::detail {

ExportRunResult runExportPipeline(const int data_format, const ExportDataset &dataset, const QString &output_dir,
                                  const QVariantMap &options, const QString &format_label,
                                  const std::function<bool(const QString &staging_dir, QString &error,
                                                           QString &summary, QString &done_hint)> &writer)
{
    ExportRunResult result;

    QString err_msg;
    if (!DataIO::checkExportSourceCollision(dataset, output_dir, data_format, err_msg))
    {
        result.message = err_msg;
        return result;
    }

    SafeExportScope scope(output_dir);
    if (!scope.isValid())
    {
        result.message = scope.error();
        return result;
    }

    try
    {
        QString summary;
        QString done_hint;
        if (!writer(scope.stagingDir(), err_msg, summary, done_hint))
        {
            result.message = err_msg;
            return result;
        }

        if (!DataIO::validateExportOutput(data_format, dataset, scope.stagingDir(), options, err_msg))
        {
            result.message = err_msg;
            return result;
        }

        if (!scope.publish(err_msg))
        {
            result.message = err_msg;
            return result;
        }

        result.success   = true;
        result.message   = summary;
        result.done_hint = done_hint;
        return result;
    }
    catch (const std::exception &e)
    {
        spdlog::error("{} 导出失败: {}", format_label.toUtf8().constData(), e.what());
        result.message = QString("%1 导出失败: %2").arg(format_label, QString::fromUtf8(e.what()));
        return result;
    }
}

} // namespace dltool::data::detail
