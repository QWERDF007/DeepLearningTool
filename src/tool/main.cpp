#include "common/Logger.h"
#include "common/CrashHandler.h"
#include "project/Logger.h"

#include <QApplication>
#include <QQmlApplicationEngine>

void InitLogger()
{
    try
    {
        auto logger = dltool::common::setupLogger(dltool::common::defaultSinks());
        logger->set_level(spdlog::level::debug);
        logger->set_pattern("[%Y/%m/%d %T.%e] [%n] [%^%L%$] [%t] %v");
        spdlog::set_default_logger(logger);
        dltool::project::registerLogger(logger);
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

    InitLogger();
    spdlog::info("Welcome to dltool!");

    QApplication          app(argc, argv);
    QQmlApplicationEngine engine;

    qDebug() << "qml import path list" << engine.importPathList();
    const QUrl url(QStringLiteral("qrc:/qt/qml/dltool/tool/qml/Main.qml"));
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated, &app,
        [url](QObject *obj, const QUrl &objUrl)
        {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);
    engine.load(url);
    const int exec = QGuiApplication::exec();
    spdlog::info("Bye!");
    return exec;
}
