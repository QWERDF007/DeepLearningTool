#include "parameter/DynamicOptions.h"

#include <spdlog/spdlog.h>

#include <exception>

namespace dltool::parameter {

namespace {

QString normalizedKey(const QString &key)
{
    return key.trimmed().toLower();
}

} // namespace

DynamicOptionsRegistry &DynamicOptionsRegistry::instance()
{
    static DynamicOptionsRegistry registry;
    return registry;
}

bool DynamicOptionsRegistry::registerProvider(std::unique_ptr<DynamicOptionsProvider> provider)
{
    if (!provider)
        return false;

    const QString key = normalizedKey(provider->key());
    if (key.isEmpty())
        return false;

    QMutexLocker locker(&mutex_);
    if (providers_.contains(key))
    {
        spdlog::warn("动态选项 provider 重复注册: key={}", key.toUtf8().constData());
        return false;
    }

    providers_.insert(key, std::shared_ptr<DynamicOptionsProvider>(std::move(provider)));
    return true;
}

bool DynamicOptionsRegistry::contains(const QString &backend_key) const
{
    QMutexLocker locker(&mutex_);
    return providers_.contains(normalizedKey(backend_key));
}

std::shared_ptr<const DynamicOptionsProvider> DynamicOptionsRegistry::provider(const QString &backend_key) const
{
    QMutexLocker locker(&mutex_);
    return providers_.value(normalizedKey(backend_key));
}

DynamicOptionsResult DynamicOptionsRegistry::resolve(const QString &backend_key, const QVariantMap &context) const
{
    DynamicOptionsResult result;
    const auto selected_provider = provider(backend_key);
    if (!selected_provider)
        return result;

    result.provider_found = true;
    try
    {
        const DynamicOptionsData data = selected_provider->query(context);
        result.options               = data.options;
        result.option_groups         = data.option_groups;
        result.recommended_value     = data.recommended_value;
    }
    catch (const std::exception &error)
    {
        spdlog::warn("动态选项 provider 查询失败: key={}, error={}", backend_key.toUtf8().constData(), error.what());
    }
    return result;
}

bool registerDynamicOptionsProvider(std::unique_ptr<DynamicOptionsProvider> provider)
{
    return DynamicOptionsRegistry::instance().registerProvider(std::move(provider));
}

} // namespace dltool::parameter
