#pragma once
#include "test_registry.h"

#include <QTest>

#define REGISTER_TEST(TestClass)                                                      \
    namespace {                                                                       \
    struct TestClass##Registrator                                                     \
    {                                                                                 \
        TestClass##Registrator()                                                      \
        {                                                                             \
            TestRegistry::getInstance().registerTest([]() { return new TestClass; }); \
        }                                                                             \
    };                                                                                \
    static TestClass##Registrator _registrator;                                       \
    }

inline int runAllTests(int argc, char **argv)
{
    int result = 0;
    for (const auto &factory : TestRegistry::getInstance().getTests())
    {
        QObject *testObj = factory();
        result |= QTest::qExec(testObj, argc, argv);
        delete testObj;
    }
    return result;
}
