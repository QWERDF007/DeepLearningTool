#pragma once
#include "test_registry.h"

#include <QTest>

#include <cstdio>

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

inline int runAllCppTests(int argc, char **argv)
{
    int result = 0;
    for (const auto &factory : TestRegistry::getInstance().getTests())
    {
        QObject *testObj = factory();
        std::fprintf(stderr, "[cpp-test] start %s\n", testObj->metaObject()->className());
        const int test_result = QTest::qExec(testObj, argc, argv);
        std::fprintf(stderr, "[cpp-test] finish %s: %d\n", testObj->metaObject()->className(), test_result);
        if (test_result != 0)
        {
            const QMetaObject *meta = testObj->metaObject();
            for (int method_index = meta->methodOffset(); method_index < meta->methodCount(); ++method_index)
            {
                const QMetaMethod method = meta->method(method_index);
                if (method.methodType() != QMetaMethod::Slot || method.access() != QMetaMethod::Private)
                    continue;
                QByteArray       method_name = method.name();
                if (method_name == "initTestCase" || method_name == "cleanupTestCase"
                    || method_name.startsWith("_q_"))
                    continue;
                QObject *single_test = factory();
                char application_name[] = "cpp-test";
                char *filter_argv[]     = {application_name, method_name.data(), nullptr};
                const int single_result     = QTest::qExec(single_test, 2, filter_argv);
                std::fprintf(stderr, "[cpp-test] case %s::%s: %d\n", meta->className(), method_name.constData(),
                             single_result);
                delete single_test;
            }
        }
        result |= test_result;
        delete testObj;
    }
    return result;
}
