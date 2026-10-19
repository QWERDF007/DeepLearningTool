#include "HelpController.h"

#include "ui/SignalHelper.h"

#include <spdlog/spdlog.h>

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>

namespace dltool::tool {
 
QString HelpController::s_baseDirOverride;
HelpController::UrlOpener HelpController::s_urlOpener = nullptr;
QUrl HelpController::s_lastOpenedUrl;
bool HelpController::s_lastOpenSuccess{false};

HelpController::HelpController(QObject *parent)
    : QObject(parent)
{
}

void HelpController::setBaseDirForTesting(const QString &baseDir)
{
    s_baseDirOverride = baseDir;
}

void HelpController::resetBaseDirForTesting()
{
    s_baseDirOverride.clear();
}

void HelpController::setUrlOpenerForTesting(UrlOpener opener)
{
    s_urlOpener = std::move(opener);
}

void HelpController::resetUrlOpenerForTesting()
{
    s_urlOpener = nullptr;
}

#ifndef DLTOOL_DOC_REL_PATH
#error "DLTOOL_DOC_REL_PATH must be defined by build configuration from config/doc_contract.json!"
#endif

bool HelpController::openDocumentation()
{
    s_lastOpenSuccess = false;
    const QString base_dir = s_baseDirOverride.isEmpty()
                                 ? QCoreApplication::applicationDirPath()
                                 : s_baseDirOverride;
    const QString target_path = QDir::cleanPath(base_dir + QStringLiteral("/") + QStringLiteral(DLTOOL_DOC_REL_PATH));
    const QFileInfo file_info(target_path);

    if (!file_info.exists() || !file_info.isFile() || !file_info.isReadable())
    {
        spdlog::warn("Help document unavailable or not readable at: {}", target_path.toStdString());
        spdlog::warn("帮助文档不可用，请重新安装完整程序。");
        dltool::ui::SignalHelper::notifyWarn(QStringLiteral("帮助文档不可用，请重新安装完整程序。"));
        return false;
    }

    const QString canonical_or_abs = file_info.canonicalFilePath().isEmpty()
                                         ? file_info.absoluteFilePath()
                                         : file_info.canonicalFilePath();
    const QUrl local_url = QUrl::fromLocalFile(canonical_or_abs);
    s_lastOpenedUrl = local_url;

    const bool opened = s_urlOpener ? s_urlOpener(local_url)
                                    : QDesktopServices::openUrl(local_url);
    if (!opened)
    {
        spdlog::warn("System rejected opening help document URL: {}", local_url.toString().toStdString());
        spdlog::warn("无法打开帮助文档，请检查 HTML 文件的默认打开方式。");
        dltool::ui::SignalHelper::notifyWarn(QStringLiteral("无法打开帮助文档，请检查 HTML 文件的默认打开方式。"));
        return false;
    }


    spdlog::info("Opened help documentation: {}", local_url.toString().toStdString());
    s_lastOpenSuccess = true;
    return true;
}

} // namespace dltool::tool
