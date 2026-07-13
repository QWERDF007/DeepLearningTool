#include "common/CrashHandler.h"
#include "common/Logger.h"
#include "data/Logger.h"
#include "feature/Logger.h"
#include "model/Logger.h"
#include "project/Logger.h"
#include "project/Projects.h"
#include "settings/Logger.h"
#include "ui/Logger.h"
#include "ui/UILogger.h"

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

void InitLogger()
{
    try
    {
        auto sinks = dltool::common::defaultSinks();
        // 对qml引用的指针 设置所属关系 由c++管理释放
        // warn: 不会释放 UILogger, 需要手动释放
        QQmlEngine::setObjectOwnership(dltool::ui::UILogger::getInstance(), QQmlEngine::CppOwnership);
        sinks.push_back(
            std::make_shared<spdlog::sinks::qt_logger_sink<std::mutex>>(dltool::ui::UILogger::getInstance()));
        auto logger = dltool::common::setupLogger(sinks);
        logger->set_level(spdlog::level::debug);
        logger->set_pattern("[%Y/%m/%d %T.%e] [%n] [%^%L%$] [%t] %v");
        spdlog::set_default_logger(logger);
        dltool::data::registerLogger(logger);
        dltool::feature::registerLogger(logger);
        dltool::model::registerLogger(logger);
        dltool::project::registerLogger(logger);
        dltool::settings::registerLogger(logger);
        dltool::ui::registerLogger(logger);
    }
    catch (const std::exception &e)
    {
        qInfo() << __FUNCTION__ << e.what();
    }
}

int main(int argc, char *argv[])
{
    dltool::common::CrashHandler crash_handler;
    crash_handler.setup();

    QApplication          app(argc, argv);
    QtWebEngineQuick::initialize();

    InitLogger();
    spdlog::info("Welcome to dltool!");

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral(DLTOOL_QML_BUILD_DIR "/qml"));
    engine.addImportPath(QCoreApplication::applicationDirPath() + QStringLiteral("/../qml"));

    // 将 QML 引擎设置到 ProjectManager（通过 QML 上下文属性）
    // 注意：ProjectManager 是 QML 单例，会在 QML 加载时自动创建
    // 我们需要在 QML 加载后设置引擎引用

    qDebug() << "qml import path list" << engine.importPathList();
    const QUrl url(QStringLiteral("qrc:/qt/qml/dltool/tool/qml/Main.qml"));
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated, &app,
        [url, &engine](QObject *obj, const QUrl &objUrl)
        {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
            else if (obj && url == objUrl)
            {
                // QML 加载成功后，将引擎传递给 ProjectManager
                dltool::project::ProjectManager::getInstance()->setQmlEngine(&engine);
                qInfo() << "QML engine set to ProjectManager:" << (void *)&engine;
            }
        },
        Qt::QueuedConnection);
    engine.load(url);
    const int exec = QGuiApplication::exec();
    spdlog::info("Bye!");
    return exec;
}
