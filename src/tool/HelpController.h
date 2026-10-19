#pragma once

#include "common/Singleton.h"

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>
#include <functional>

namespace dltool::tool {

class HelpController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QT_QML_SINGLETON(HelpController)

public:
    using UrlOpener = std::function<bool(const QUrl &)>;

    explicit HelpController(QObject *parent = nullptr);
    ~HelpController() override = default;

    Q_INVOKABLE bool openDocumentation();

    QUrl lastOpenedUrl() const { return s_lastOpenedUrl; }
    bool lastOpenSuccess() const { return s_lastOpenSuccess; }

    void setBaseDirForTesting(const QString &baseDir);
    void resetBaseDirForTesting();

    void setUrlOpenerForTesting(UrlOpener opener);
    void resetUrlOpenerForTesting();

private:
    static QString s_baseDirOverride;
    static UrlOpener s_urlOpener;
    static QUrl s_lastOpenedUrl;
    static bool s_lastOpenSuccess;
};

} // namespace dltool::tool
