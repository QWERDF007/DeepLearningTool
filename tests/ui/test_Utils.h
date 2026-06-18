#pragma once

#include "test_runner.h"

#include <QtTest>

namespace dltool::ui {

class UtilsTest : public QObject
{
    Q_OBJECT

private slots:
    void withOpacityTestCase_data();
    void withOpacityTestCase();
    void scalarValueHelpersTestCase();
    void valueRangeAtTestCase();
    void paramDecimalsTestCase();
};

REGISTER_TEST(UtilsTest);

} // namespace dltool::ui
