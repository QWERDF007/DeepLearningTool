#include "test_HelpController.h"

#include "HelpController.h"
#include "test_runner.h"
#include "ui/SignalHelper.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace dltool::tool {

void HelpControllerTest::init()
{
    HelpController::getInstance()->resetBaseDirForTesting();
    HelpController::getInstance()->resetUrlOpenerForTesting();
}

void HelpControllerTest::cleanup()
{
    HelpController::getInstance()->resetBaseDirForTesting();
    HelpController::getInstance()->resetUrlOpenerForTesting();
}

void HelpControllerTest::test_openDocumentation_success()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());

    const QString doc_dir = temp_dir.path() + QStringLiteral("/doc");
    QVERIFY(QDir().mkpath(doc_dir));
    const QString index_path = doc_dir + QStringLiteral("/index.html");
    QFile file(index_path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("<!DOCTYPE html><html><body><h1>Help</h1></body></html>");
    file.close();

    HelpController *controller = HelpController::getInstance();
    controller->setBaseDirForTesting(temp_dir.path());

    QUrl captured_url;
    controller->setUrlOpenerForTesting([&captured_url](const QUrl &url) {
        captured_url = url;
        return true;
    });

    QSignalSpy warn_spy(dltool::ui::SignalHelper::getInstance(), &dltool::ui::SignalHelper::warn);

    controller->openDocumentation();

    QCOMPARE(warn_spy.count(), 0);
    QVERIFY(captured_url.isValid());
    QVERIFY(captured_url.isLocalFile());

    const QString opened_local_file = QDir::cleanPath(captured_url.toLocalFile());
    const QString expected_local_file = QDir::cleanPath(index_path);
    QCOMPARE(opened_local_file.toLower(), expected_local_file.toLower());
}

void HelpControllerTest::test_openDocumentation_missingFile()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());

    HelpController *controller = HelpController::getInstance();
    controller->setBaseDirForTesting(temp_dir.path());

    bool opener_called = false;
    controller->setUrlOpenerForTesting([&opener_called](const QUrl &) {
        opener_called = true;
        return true;
    });

    QSignalSpy warn_spy(dltool::ui::SignalHelper::getInstance(), &dltool::ui::SignalHelper::warn);

    controller->openDocumentation();

    QVERIFY(!opener_called);
    QCOMPARE(warn_spy.count(), 1);

    const QList<QVariant> arguments = warn_spy.takeFirst();
    const QString title = arguments.at(0).toString();
    const QString message = arguments.size() > 1 ? arguments.at(1).toString() : QString();
    const QString combined = title + message;
    QVERIFY2(combined.contains(QStringLiteral("帮助文档不可用，请重新安装完整程序。")),
             qPrintable(combined));
}

void HelpControllerTest::test_openDocumentation_directoryInsteadOfFile()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());

    // Create doc/index.html as a directory, not a regular file
    const QString fake_index_dir = temp_dir.path() + QStringLiteral("/doc/index.html");
    QVERIFY(QDir().mkpath(fake_index_dir));

    HelpController *controller = HelpController::getInstance();
    controller->setBaseDirForTesting(temp_dir.path());

    bool opener_called = false;
    controller->setUrlOpenerForTesting([&opener_called](const QUrl &) {
        opener_called = true;
        return true;
    });

    QSignalSpy warn_spy(dltool::ui::SignalHelper::getInstance(), &dltool::ui::SignalHelper::warn);

    controller->openDocumentation();

    QVERIFY(!opener_called);
    QCOMPARE(warn_spy.count(), 1);

    const QList<QVariant> arguments = warn_spy.takeFirst();
    const QString combined = arguments.at(0).toString() + (arguments.size() > 1 ? arguments.at(1).toString() : QString());
    QVERIFY2(combined.contains(QStringLiteral("帮助文档不可用，请重新安装完整程序。")),
             qPrintable(combined));
}

void HelpControllerTest::test_openDocumentation_systemRejected()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());

    const QString doc_dir = temp_dir.path() + QStringLiteral("/doc");
    QVERIFY(QDir().mkpath(doc_dir));
    const QString index_path = doc_dir + QStringLiteral("/index.html");
    QFile file(index_path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("<!DOCTYPE html><html><body><h1>Help</h1></body></html>");
    file.close();

    HelpController *controller = HelpController::getInstance();
    controller->setBaseDirForTesting(temp_dir.path());

    bool opener_called = false;
    controller->setUrlOpenerForTesting([&opener_called](const QUrl &) {
        opener_called = true;
        return false; // System rejects the request
    });

    QSignalSpy warn_spy(dltool::ui::SignalHelper::getInstance(), &dltool::ui::SignalHelper::warn);

    controller->openDocumentation();

    QVERIFY(opener_called);
    QCOMPARE(warn_spy.count(), 1);

    const QList<QVariant> arguments = warn_spy.takeFirst();
    const QString combined = arguments.at(0).toString() + (arguments.size() > 1 ? arguments.at(1).toString() : QString());
    QVERIFY2(combined.contains(QStringLiteral("无法打开帮助文档，请检查 HTML 文件的默认打开方式。")),
             qPrintable(combined));
}

void HelpControllerTest::test_openDocumentation_chineseAndSpacesPath()
{
    QTemporaryDir temp_root;
    QVERIFY(temp_root.isValid());

    // Create directory path with Chinese and spaces
    const QString chinese_spaced_base = temp_root.path() + QStringLiteral("/深度 学习 工具 2026/Release 安装包");
    const QString doc_dir = chinese_spaced_base + QStringLiteral("/doc");
    QVERIFY(QDir().mkpath(doc_dir));
    const QString index_path = doc_dir + QStringLiteral("/index.html");
    QFile file(index_path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("<!DOCTYPE html><html><body><h1>中文测试文档</h1></body></html>");
    file.close();

    HelpController *controller = HelpController::getInstance();
    controller->setBaseDirForTesting(chinese_spaced_base);

    QUrl captured_url;
    controller->setUrlOpenerForTesting([&captured_url](const QUrl &url) {
        captured_url = url;
        return true;
    });

    QSignalSpy warn_spy(dltool::ui::SignalHelper::getInstance(), &dltool::ui::SignalHelper::warn);

    controller->openDocumentation();

    QCOMPARE(warn_spy.count(), 0);
    QVERIFY(captured_url.isValid());
    QVERIFY(captured_url.isLocalFile());

    const QString opened_local_file = QDir::cleanPath(captured_url.toLocalFile());
    const QString expected_local_file = QDir::cleanPath(index_path);
    QCOMPARE(opened_local_file.toLower(), expected_local_file.toLower());
}

void HelpControllerTest::test_openDocumentation_workingDirChange()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());

    const QString doc_dir = temp_dir.path() + QStringLiteral("/doc");
    QVERIFY(QDir().mkpath(doc_dir));
    const QString index_path = doc_dir + QStringLiteral("/index.html");
    QFile file(index_path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("<!DOCTYPE html><html><body><h1>Help</h1></body></html>");
    file.close();

    HelpController *controller = HelpController::getInstance();
    controller->setBaseDirForTesting(temp_dir.path());

    QUrl captured_url;
    controller->setUrlOpenerForTesting([&captured_url](const QUrl &url) {
        captured_url = url;
        return true;
    });

    // Change current working directory to a different temporary directory
    QTemporaryDir other_dir;
    QVERIFY(other_dir.isValid());
    const QString original_cwd = QDir::currentPath();
    QVERIFY(QDir::setCurrent(other_dir.path()));

    QSignalSpy warn_spy(dltool::ui::SignalHelper::getInstance(), &dltool::ui::SignalHelper::warn);

    controller->openDocumentation();

    // Restore working directory immediately
    QDir::setCurrent(original_cwd);

    QCOMPARE(warn_spy.count(), 0);
    QVERIFY(captured_url.isValid());
    const QString opened_local_file = QDir::cleanPath(captured_url.toLocalFile());
    const QString expected_local_file = QDir::cleanPath(index_path);
    QCOMPARE(opened_local_file.toLower(), expected_local_file.toLower());
}

void HelpControllerTest::test_openDocumentation_singleNotificationPerFailure()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());

    HelpController *controller = HelpController::getInstance();
    controller->setBaseDirForTesting(temp_dir.path());

    QSignalSpy warn_spy(dltool::ui::SignalHelper::getInstance(), &dltool::ui::SignalHelper::warn);

    controller->openDocumentation();

    // Exactly one warning emitted, no double notifications
    QCOMPARE(warn_spy.count(), 1);
}

void HelpControllerTest::test_openDocumentation_unreadableFile()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());

    const QString doc_dir = temp_dir.path() + QStringLiteral("/doc");
    QVERIFY(QDir().mkpath(doc_dir));
    const QString index_path = doc_dir + QStringLiteral("/index.html");
    {
        QFile file(index_path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("<!DOCTYPE html><html><body><h1>Help</h1></body></html>");
        file.close();
    }

#if defined(Q_OS_WIN)
    struct NtfsGuard {
        NtfsGuard() { qt_ntfs_permission_lookup++; }
        ~NtfsGuard() { qt_ntfs_permission_lookup--; }
    } ntfs_guard;

    const QString username = qEnvironmentVariable("USERNAME");
    QVERIFY2(!username.isEmpty(), "USERNAME environment variable must be set");

    const QString native_path = QDir::toNativeSeparators(index_path);
    const int deny_code = QProcess::execute(
        QStringLiteral("icacls"),
        {native_path, QStringLiteral("/deny"), QStringLiteral("%1:(R)").arg(username)});
    QCOMPARE(deny_code, 0);

    struct PermGuard {
        QString path;
        QString user;
        ~PermGuard() {
            if (!user.isEmpty()) {
                QProcess::execute(
                    QStringLiteral("icacls"),
                    {path, QStringLiteral("/remove:d"), user});
            }
        }
    } perm_guard{native_path, username};
#else
    QVERIFY(QFile::setPermissions(index_path, QFileDevice::Permissions{}));
#endif

    const QFileInfo file_info(index_path);
    QVERIFY2(!file_info.isReadable(), "Target file must be unreadable");

    HelpController *controller = HelpController::getInstance();
    controller->setBaseDirForTesting(temp_dir.path());

    bool opener_called = false;
    controller->setUrlOpenerForTesting([&opener_called](const QUrl &) {
        opener_called = true;
        return true;
    });

    QSignalSpy warn_spy(dltool::ui::SignalHelper::getInstance(), &dltool::ui::SignalHelper::warn);

    const bool result = controller->openDocumentation();
    QVERIFY(!result);
    QVERIFY(!opener_called);
    QCOMPARE(warn_spy.count(), 1);

    const QList<QVariant> arguments = warn_spy.takeFirst();
    const QString title = arguments.at(0).toString();
    const QString message = arguments.size() > 1 ? arguments.at(1).toString() : QString();
    const QString combined = title + message;
    QVERIFY2(combined.contains(QStringLiteral("帮助文档不可用，请重新安装完整程序。")),
             qPrintable(combined));
}

void HelpControllerTest::test_openDocumentation_specialCharactersPath()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());

    // 路径包含空格、井号(#)、百分号(%)、与号(&)、加号(+)、中括号([])等特殊字符
    const QString special_dir = temp_dir.path() + QStringLiteral("/special #dir & path + [v1.0] %20");
    const QString doc_dir = special_dir + QStringLiteral("/doc");
    QVERIFY(QDir().mkpath(doc_dir));
    const QString index_path = doc_dir + QStringLiteral("/index.html");
    QFile file(index_path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("<!DOCTYPE html><html><body><h1>Special Characters Help</h1></body></html>");
    file.close();

    HelpController *controller = HelpController::getInstance();
    controller->setBaseDirForTesting(special_dir);

    QUrl captured_url;
    controller->setUrlOpenerForTesting([&captured_url](const QUrl &url) {
        captured_url = url;
        return true;
    });

    QSignalSpy warn_spy(dltool::ui::SignalHelper::getInstance(), &dltool::ui::SignalHelper::warn);

    const bool ok = controller->openDocumentation();
    QVERIFY(ok);
    QCOMPARE(warn_spy.count(), 0);
    QVERIFY(captured_url.isValid());
    QVERIFY(captured_url.isLocalFile());

    const QString opened_local_file = QDir::cleanPath(captured_url.toLocalFile());
    const QString expected_local_file = QDir::cleanPath(index_path);
    QCOMPARE(opened_local_file.toLower(), expected_local_file.toLower());
}

void HelpControllerTest::test_helpMenu_structureAndAction()
{
    QQmlEngine engine;
    engine.addImportPath(QCoreApplication::applicationDirPath());
    engine.addImportPath(QCoreApplication::applicationDirPath() + QStringLiteral("/qml"));
#ifdef DLTOOL_QML_BUILD_DIR
    engine.addImportPath(QStringLiteral(DLTOOL_QML_BUILD_DIR));
    engine.addImportPath(QStringLiteral(DLTOOL_QML_BUILD_DIR "/qml"));
#endif

    static bool s_registered = false;
    if (!s_registered)
    {
        qmlRegisterSingletonInstance<HelpController>("dltool.tool", 1, 0, "HelpController", HelpController::getInstance());
        s_registered = true;
    }

    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/dltool/tool/qml/header/Header.qml")));
    if (component.isError())
    {
        qWarning() << component.errors();
    }
    QVERIFY(!component.isError());

    QQuickWindow window;
    window.resize(1920, 1080);

    QScopedPointer<QObject> object(component.create());
    QVERIFY(object);

    QQuickItem *headerItem = qobject_cast<QQuickItem *>(object.get());
    QVERIFY(headerItem);
    headerItem->setParentItem(window.contentItem());
    headerItem->setWidth(1920);
    headerItem->setHeight(80);

    window.show();
    window.requestActivate();
    QCoreApplication::processEvents();

    QObject *helpButtonObj = object->findChild<QObject *>(QStringLiteral("helpButton"));
    QVERIFY(helpButtonObj);
    QQuickItem *helpButtonItem = qobject_cast<QQuickItem *>(helpButtonObj);
    QVERIFY(helpButtonItem);

    QObject *helpMenu = object->findChild<QObject *>(QStringLiteral("helpMenu"));
    QVERIFY(helpMenu);

    // Verify menu items count is strictly 1 (only "文档", no placeholders)
    int count = helpMenu->property("count").toInt();
    QCOMPARE(count, 1);

    // Verify first item text is "文档"
    QQuickItem *item = nullptr;
    QMetaObject::invokeMethod(helpMenu, "itemAt", Q_RETURN_ARG(QQuickItem *, item), Q_ARG(int, 0));
    if (!item)
    {
        item = helpMenu->findChild<QQuickItem *>(QStringLiteral("helpDocMenuItem"));
    }
    QVERIFY(item);
    QCOMPARE(item->property("text").toString(), QStringLiteral("文档"));

    // Verify initial closed state
    QVERIFY(!helpMenu->property("opened").toBool());

    // Disable menu transitions and animations for instantaneous, reliable test interaction
    helpMenu->setProperty("animationEnabled", false);

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString doc_dir = temp_dir.path() + QStringLiteral("/doc");
    QVERIFY(QDir().mkpath(doc_dir));
    const QString index_path = doc_dir + QStringLiteral("/index.html");
    QFile file(index_path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("<!DOCTYPE html><html><body><h1>Help</h1></body></html>");
    file.close();

    HelpController *controller = HelpController::getInstance();
    controller->setBaseDirForTesting(temp_dir.path());

    int open_call_count = 0;
    QUrl captured_url;
    controller->setUrlOpenerForTesting([&captured_url, &open_call_count](const QUrl &url) {
        captured_url = url;
        open_call_count++;
        return true;
    });

    const QPointF btnScenePos = helpButtonItem->mapToScene(QPointF(helpButtonItem->width() / 2.0, helpButtonItem->height() / 2.0));

    // 场景 1: 纯鼠标点击交互（点击按钮打开 -> 校验菜单定位 -> 鼠标点击“文档” -> 验证触发与关闭）
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, btnScenePos.toPoint());
    QTest::qWait(50);

    // 验证菜单成功弹出
    QVERIFY(helpMenu->property("opened").toBool());

    // 验证菜单定位：位于按钮下方且在窗口边界范围内
    const qreal menuX = helpMenu->property("x").toReal();
    const qreal menuY = helpMenu->property("y").toReal();
    const qreal menuW = helpMenu->property("width").toReal();
    const qreal menuH = helpMenu->property("height").toReal();
    const QPointF menuScenePos = helpButtonItem->mapToScene(QPointF(menuX, menuY));
    QVERIFY(menuScenePos.y() >= btnScenePos.y());
    QVERIFY(menuScenePos.x() >= 0 && menuScenePos.x() + menuW <= window.width());
    QVERIFY(menuScenePos.y() >= 0 && menuScenePos.y() + menuH <= window.height());

    // 纯鼠标点击“文档”菜单项，独立断言，无键盘回退
    QQuickItem *activeItem = nullptr;
    QMetaObject::invokeMethod(helpMenu, "itemAt", Q_RETURN_ARG(QQuickItem *, activeItem), Q_ARG(int, 0));
    if (!activeItem)
        activeItem = item;
    QVERIFY(activeItem);
    const QPointF itemScenePos = activeItem->mapToScene(QPointF(activeItem->width() / 2.0, activeItem->height() / 2.0));

    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, itemScenePos.toPoint());
    QTest::qWait(50);

    // 独立断言纯鼠标点击：触发次数严格为 1，URL 匹配，且菜单已自动关闭
    QCOMPARE(open_call_count, 1);
    QVERIFY(captured_url.isValid());
    QCOMPARE(QDir::cleanPath(captured_url.toLocalFile()).toLower(),
             QDir::cleanPath(index_path).toLower());
    QVERIFY(!helpMenu->property("opened").toBool());

    // 场景 2: Esc 按键关闭与焦点严格恢复测试
    captured_url.clear();
    helpButtonItem->forceActiveFocus();
    QTest::qWait(50);
    QCOMPARE(window.activeFocusItem(), helpButtonItem);

    // 再次点击打开菜单
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, btnScenePos.toPoint());
    QTest::qWait(50);
    QVERIFY(helpMenu->property("opened").toBool());

    // 验证关闭策略支持 Esc 键，且按 Esc 键关闭菜单
    const int closePolicy = helpMenu->property("closePolicy").toInt();
    QVERIFY(closePolicy > 0);
    QTest::keyClick(&window, Qt::Key_Escape);
    QTest::qWait(50);

    // 严格断言：菜单已关闭，活动焦点严格恢复到 helpButtonItem（移除任何宽松 OR）
    QVERIFY(!helpMenu->property("opened").toBool());
    QCOMPARE(window.activeFocusItem(), helpButtonItem);
    QCOMPARE(open_call_count, 1);

    // 场景 3: 纯键盘导航与触发测试（Space 展开 -> Down 聚焦 -> Return 触发）
    captured_url.clear();
    QCOMPARE(window.activeFocusItem(), helpButtonItem);

    // 键盘空格键打开菜单
    QTest::keyClick(&window, Qt::Key_Space);
    QTest::qWait(50);
    QVERIFY(helpMenu->property("opened").toBool());

    // 键盘下方向键聚焦到菜单项，回车键触发
    QTest::keyClick(&window, Qt::Key_Down);
    QTest::qWait(20);
    QTest::keyClick(&window, Qt::Key_Return);
    QTest::qWait(50);

    // 独立断言键盘触发：触发次数递增为 2，URL 匹配，且菜单已自动关闭
    QCOMPARE(open_call_count, 2);
    QVERIFY(captured_url.isValid());
    QCOMPARE(QDir::cleanPath(captured_url.toLocalFile()).toLower(),
             QDir::cleanPath(index_path).toLower());
    QVERIFY(!helpMenu->property("opened").toBool());
}

REGISTER_TEST(HelpControllerTest);

} // namespace dltool::tool
