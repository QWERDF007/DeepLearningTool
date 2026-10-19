#pragma once

#include <QObject>

namespace dltool::tool {

class HelpControllerTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void test_openDocumentation_success();
    void test_openDocumentation_missingFile();
    void test_openDocumentation_directoryInsteadOfFile();
    void test_openDocumentation_systemRejected();
    void test_openDocumentation_chineseAndSpacesPath();
    void test_openDocumentation_workingDirChange();
    void test_openDocumentation_singleNotificationPerFailure();
    void test_openDocumentation_unreadableFile();
    void test_openDocumentation_specialCharactersPath();
    void test_helpMenu_structureAndAction();
};

} // namespace dltool::tool
