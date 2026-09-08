#pragma once

#include "test_runner.h"

#include <QtTest>

namespace dltool::ui {

class ProgressManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void taskIdentityIsEnforcedOnStartUpdateComplete();
    void interleavedTasksDoNotOverwriteEachOther();
    void failureAndCancellationDoNotDisguiseAs100Percent();
    void duplicateTerminalCallsDoNotReNotify();
    void resetClearsStateAndInvalidatesPreviousTaskId();
    void messageFilteringByTaskId();
};

} // namespace dltool::ui
