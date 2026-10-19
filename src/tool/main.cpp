#include "HelpController.h"
#include "common/CrashHandler.h"
#include "common/Logger.h"
#include "data/DataManager.h"
#include "data/DataViewModels.h"
#include "data/Datasets.h"
#include "data/Logger.h"
#include "feature/Logger.h"
#include "model/EvaluationThumbnailImageProvider.h"
#include "model/Logger.h"
#include "project/Logger.h"
#include "project/Projects.h"
#include "settings/Logger.h"
#include "ui/Logger.h"
#include "ui/SignalHelper.h"
#include "ui/UILogger.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QQmlApplicationEngine>
#include <QStringList>
#include <QTimer>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#include <functional>
#include <memory>

namespace {

bool isWslEnvironment()
{
    if (qEnvironmentVariableIsSet("WSL_DISTRO_NAME") || qEnvironmentVariableIsSet("WSL_INTEROP"))
        return true;

    QFile proc_version(QStringLiteral("/proc/version"));
    if (!proc_version.open(QIODevice::ReadOnly))
        return false;

    const QString version = QString::fromLocal8Bit(proc_version.readAll());
    return version.contains(QStringLiteral("microsoft"), Qt::CaseInsensitive)
        || version.contains(QStringLiteral("wsl"), Qt::CaseInsensitive);
}

void configureWslQtWebEngine()
{
    if (!isWslEnvironment())
        return;

    QStringList chromium_flags
        = QString::fromLocal8Bit(qgetenv("QTWEBENGINE_CHROMIUM_FLAGS"))
              .split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    const QStringList required_flags = {QStringLiteral("--disable-gpu"),
                                        QStringLiteral("--disable-gpu-compositing"),
                                        QStringLiteral("--disable-gpu-rasterization")};
    for (const QString &flag : required_flags)
    {
        if (!chromium_flags.contains(flag))
            chromium_flags.append(flag);
    }
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", chromium_flags.join(QLatin1Char(' ')).toLocal8Bit());

    // WSLg exposes both Wayland and X11 in many installations.  Prefer the
    // X11 plugin when DISPLAY is available because it avoids the unstable
    // Wayland/EGL path used by the WebEngine view on this environment.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM") && qEnvironmentVariableIsSet("DISPLAY"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("xcb"));
}

} // namespace

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

    configureWslQtWebEngine();
    QtWebEngineQuick::initialize();
    QApplication          app(argc, argv);

    InitLogger();
    spdlog::info("Welcome to dltool!");

    QQmlApplicationEngine engine;
    engine.addImageProvider("evaluationthumbnail", new dltool::model::EvaluationThumbnailImageProvider());
    // qt_add_qml_module outputs the build-time modules below the build root
    // (for example, build/dltool/core), while quickui is emitted under build/qml.
    engine.addImportPath(QCoreApplication::applicationDirPath());
    engine.addImportPath(QCoreApplication::applicationDirPath() + QStringLiteral("/qml"));
#ifdef DLTOOL_QML_BUILD_DIR
    engine.addImportPath(QStringLiteral(DLTOOL_QML_BUILD_DIR));
    engine.addImportPath(QStringLiteral(DLTOOL_QML_BUILD_DIR "/qml"));
#endif
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

    const int capture_idx = app.arguments().indexOf(QStringLiteral("--capture-screenshots"));
    if (capture_idx >= 0 && capture_idx + 1 < app.arguments().size())
    {
        const QString out_dir = app.arguments().at(capture_idx + 1);
        QDir().mkpath(out_dir);

        const int proj_idx = app.arguments().indexOf(QStringLiteral("--project"));
        QString proj_path = (proj_idx >= 0 && proj_idx + 1 < app.arguments().size())
                                ? app.arguments().at(proj_idx + 1)
                                : QString();

        if (proj_path.isEmpty())
        {
            const QString default_candidate
                = QDir(QDir::tempPath()).filePath(QStringLiteral("dlt_test_pro/测试项目.dlpro"));
            if (QFile::exists(default_candidate))
            {
                proj_path = default_candidate;
            }
        }

        QObject::connect(
            &engine, &QQmlApplicationEngine::objectCreated, &app,
            [out_dir, proj_path](QObject *obj, const QUrl &)
            {
                auto *window = qobject_cast<QQuickWindow *>(obj);
                if (!window)
                    return;

                window->showNormal();
                window->resize(1440, 900);

                if (!proj_path.isEmpty() && QFile::exists(proj_path))
                {
                    dltool::project::ProjectManager::getInstance()->openProject(proj_path);
                }

                struct CaptureTask
                {
                    int tab_index;
                    QString filename;
                };

                auto tasks = std::make_shared<QList<CaptureTask>>(QList<CaptureTask>{
                    {0, QStringLiteral("screenshot_project.png")},
                    {1, QStringLiteral("screenshot_gallery.png")},
                    {2, QStringLiteral("screenshot_annotation.png")},
                    {3, QStringLiteral("screenshot_inspection.png")},
                    {4, QStringLiteral("screenshot_training.png")},
                    {5, QStringLiteral("screenshot_evaluation.png")}
                });

                auto current_step = std::make_shared<int>(0);
                auto runner = std::make_shared<std::function<void()>>();
                *runner = [window, out_dir, tasks, current_step, runner]()
                {
                    if (*current_step < tasks->size())
                    {
                        const auto &task = (*tasks)[*current_step];
                        emit dltool::ui::SignalHelper::getInstance()->changeTabBarIndex(task.tab_index);

                        // If switching to Gallery or Annotation, ensure first dataset and image are selected
                        if (task.tab_index == 1 || task.tab_index == 2)
                        {
                            auto *proj = dltool::project::ProjectManager::getInstance()->currentProject();
                            if (proj && proj->dataManager())
                            {
                                auto *dm = proj->dataManager();
                                if (dm->datasets() && dm->datasets()->rowCount() > 0)
                                {
                                    dm->datasets()->selection()->select(dm->datasets()->index(0, 0),
                                                                        QItemSelectionModel::ClearAndSelect);
                                    dm->datasets()->selection()->setCurrentIndex(dm->datasets()->index(0, 0),
                                                                                 QItemSelectionModel::Current);
                                }
                                if (dm->imageInstances() && dm->imageInstances()->rowCount() > 0)
                                {
                                    dm->imageInstances()->selection()->select(dm->imageInstances()->index(0, 0),
                                                                              QItemSelectionModel::ClearAndSelect);
                                    dm->imageInstances()->selection()->setCurrentIndex(dm->imageInstances()->index(0, 0),
                                                                                       QItemSelectionModel::Current);
                                }
                            }
                        }

                        QTimer::singleShot(800, [window, out_dir, tasks, current_step, runner]()
                        {
                            const auto &task = (*tasks)[*current_step];
                            QImage image = window->grabWindow();
                            const QString save_path = QDir(out_dir).filePath(task.filename);
                            if (image.save(save_path))
                            {
                                spdlog::info("Saved screenshot: {}", save_path.toStdString());
                            }
                            else
                            {
                                spdlog::error("Failed to save screenshot: {}", save_path.toStdString());
                            }
                            (*current_step)++;
                            (*runner)();
                        });
                    }
                    else
                    {
                        spdlog::info("All screenshots captured successfully.");
                        QTimer::singleShot(200, []() { QCoreApplication::quit(); });
                    }
                };

                QTimer::singleShot(1000, [runner]() { (*runner)(); });
            },
            Qt::QueuedConnection);
    }

    const bool simulate_reject = (app.arguments().contains(QStringLiteral("--test-reject-open"))
                                  || qEnvironmentVariableIntValue("DLTOOL_SIMULATE_SYSTEM_REJECT_OPEN") == 1);
    if (simulate_reject)
    {
        dltool::tool::HelpController::getInstance()->setUrlOpenerForTesting([](const QUrl &) { return false; });
    }

    const int verify_menu_idx = app.arguments().indexOf(QStringLiteral("--verify-help-menu"));
    const bool is_verify_help_menu = verify_menu_idx >= 0;
    QString verify_menu_out_file;
    if (is_verify_help_menu && verify_menu_idx + 1 < app.arguments().size())
    {
        verify_menu_out_file = app.arguments().at(verify_menu_idx + 1);
    }

    if (is_verify_help_menu)
    {
        QObject::connect(
            &engine, &QQmlApplicationEngine::objectCreated, &app,
            [verify_menu_out_file](QObject *obj, const QUrl &)
            {
                auto *window = qobject_cast<QQuickWindow *>(obj);
                if (!window)
                {
                    spdlog::error("Verify help menu: root window is null");
                    QCoreApplication::exit(1);
                    return;
                }

                window->requestActivate();

                // 等待 500ms 使窗口与 QML 布局渲染就绪
                QTimer::singleShot(500, [window, verify_menu_out_file]()
                {
                    auto *helpButtonItem = window->findChild<QQuickItem *>(QStringLiteral("helpButton"));
                    QObject *helpMenu = window->findChild<QObject *>(QStringLiteral("helpMenu"));

                    if (!helpButtonItem || !helpMenu)
                    {
                        spdlog::error("Verify help menu: helpButton or helpMenu not found in QML tree");
                        QCoreApplication::exit(2);
                        return;
                    }

                    // 1. 发送真实鼠标点击事件触发 helpButton (验证按钮点击能否正常弹出菜单)
                    const QPointF btnScenePos = helpButtonItem->mapToScene(
                        QPointF(helpButtonItem->width() / 2.0, helpButtonItem->height() / 2.0));
                    const QPoint btnGlobalPos = window->mapToGlobal(btnScenePos.toPoint());

                    QMouseEvent pressBtn(QEvent::MouseButtonPress, btnScenePos, btnScenePos, btnGlobalPos,
                                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    QCoreApplication::sendEvent(window, &pressBtn);

                    QMouseEvent releaseBtn(QEvent::MouseButtonRelease, btnScenePos, btnScenePos, btnGlobalPos,
                                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                    QCoreApplication::sendEvent(window, &releaseBtn);

                    // 2. 等待菜单完成展开并验证其真实 opened 状态
                    QTimer::singleShot(250, [window, helpMenu, verify_menu_out_file]()
                    {
                        const bool menuOpened = helpMenu->property("opened").toBool();
                        if (!menuOpened)
                        {
                            spdlog::error("Verify help menu FAILED: helpMenu was not opened after mouse click on helpButton");
                            QCoreApplication::exit(2);
                            return;
                        }

                        // 获取展开的菜单项 (通过 itemAt 或 findChild)
                        QQuickItem *menuItem = nullptr;
                        QMetaObject::invokeMethod(helpMenu, "itemAt", Q_RETURN_ARG(QQuickItem *, menuItem), Q_ARG(int, 0));
                        if (!menuItem)
                        {
                            menuItem = helpMenu->findChild<QQuickItem *>(QStringLiteral("helpDocMenuItem"));
                        }

                        if (!menuItem)
                        {
                            spdlog::error("Verify help menu FAILED: helpDocMenuItem not found in opened helpMenu");
                            QCoreApplication::exit(2);
                            return;
                        }

                        // 3. 发送真实鼠标点击事件触发“文档”菜单项 (验证菜单项能否正常接受实际输入并触发文档打开)
                        const QPointF itemScenePos = menuItem->mapToScene(
                            QPointF(menuItem->width() / 2.0, menuItem->height() / 2.0));
                        const QPoint itemGlobalPos = window->mapToGlobal(itemScenePos.toPoint());

                        QMouseEvent pressItem(QEvent::MouseButtonPress, itemScenePos, itemScenePos, itemGlobalPos,
                                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                        QCoreApplication::sendEvent(window, &pressItem);

                        QMouseEvent releaseItem(QEvent::MouseButtonRelease, itemScenePos, itemScenePos, itemGlobalPos,
                                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                        QCoreApplication::sendEvent(window, &releaseItem);

                        // 4. 等待触发完成，验证真实文档打开结果及菜单关闭状态
                        QTimer::singleShot(250, [helpMenu, verify_menu_out_file]()
                        {
                            auto *controller = dltool::tool::HelpController::getInstance();
                            const bool success = controller->lastOpenSuccess() && !controller->lastOpenedUrl().isEmpty();
                            const bool menuClosed = !helpMenu->property("opened").toBool();

                            if (success && menuClosed)
                            {
                                const QString url_str = controller->lastOpenedUrl().toString();
                                spdlog::info("Verify help menu SUCCESS: real mouse interaction triggered doc at {}", url_str.toStdString());
                                if (!verify_menu_out_file.isEmpty())
                                {
                                    QFile out_file(verify_menu_out_file);
                                    if (out_file.open(QIODevice::WriteOnly | QIODevice::Text))
                                    {
                                        out_file.write(url_str.toUtf8());
                                        out_file.close();
                                    }
                                }
                                QCoreApplication::exit(0);
                            }
                            else
                            {
                                spdlog::error("Verify help menu FAILED: success={}, menuClosed={}", success, menuClosed);
                                QCoreApplication::exit(2);
                            }

                        });
                    });
                });
            },
            Qt::QueuedConnection);
    }

    engine.load(url);

    const bool is_test_open_doc = app.arguments().contains(QStringLiteral("--test-open-doc"));
    if (is_test_open_doc)
    {
        QTimer::singleShot(250, &app, [&app]() {
            bool ok = dltool::tool::HelpController::getInstance()->openDocumentation();
            spdlog::info("Test open documentation result: {}", ok);
            QCoreApplication::exit(ok ? 0 : 1);
        });
    }

    const bool is_smoke_test
        = (app.arguments().contains(QStringLiteral("--smoke-test")) || qEnvironmentVariableIsSet("DLT_SMOKE_TEST"))
          && capture_idx < 0 && !is_test_open_doc && !is_verify_help_menu;
    if (is_smoke_test)
    {
        bool ok = false;
        int delay = qEnvironmentVariableIntValue("DLT_SMOKE_TEST_MS", &ok);
        if (!ok || delay <= 0)
            delay = 250;
        QTimer::singleShot(delay, &app, &QCoreApplication::quit);
    }

    const int exec = QGuiApplication::exec();
    spdlog::info("Bye!");
    return exec;
}
