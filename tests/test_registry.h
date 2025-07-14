#pragma once
#include <QObject>
#include <functional>
#include <vector>

class TestRegistry
{
public:
    using Factory = std::function<QObject *()>;

    static TestRegistry &getInstance()
    {
        static TestRegistry registry;
        return registry;
    }

    void registerTest(const Factory &factory)
    {
        factories.push_back(factory);
    }

    const std::vector<Factory> &getTests() const
    {
        return factories;
    }

private:
    std::vector<Factory> factories;
};
