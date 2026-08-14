#pragma once

#include "dltool/parameter/Export.h"
#include "parameter/ParameterTypes.h"

#include <QHash>
#include <QMutex>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <memory>

namespace dltool::parameter {

/** provider 查询结果，不包含注册表状态。 */
struct PARAMETER_API DynamicOptionsData
{
    QVector<ParameterOption> options;
    QVariantList             option_groups;
    QVariant                 recommended_value;
};

/** 带 provider 注册状态的查询结果。 */
struct PARAMETER_API DynamicOptionsResult
{
    bool                    provider_found{false};
    QVector<ParameterOption> options;
    QVariantList             option_groups;
    QVariant                recommended_value;
};

/**
 * @brief 动态选项 provider 抽象接口。
 * @details provider 只负责查询选项，不负责 YAML、QML 或持久化。
 */
class PARAMETER_API DynamicOptionsProvider
{
public:
    virtual ~DynamicOptionsProvider() = default;

    /** provider key，例如 inferrt.compute_devices。 */
    virtual QString key() const = 0;

    /** 查询当前可用选项和推荐实际值。 */
    virtual DynamicOptionsData query(const QVariantMap &context = {}) const = 0;
};

/** 进程级动态选项 provider 注册表。 */
class PARAMETER_API DynamicOptionsRegistry final
{
public:
    static DynamicOptionsRegistry &instance();

    /** 注册 provider；重复 key 注册失败。 */
    bool registerProvider(std::unique_ptr<DynamicOptionsProvider> provider);

    bool contains(const QString &backend_key) const;
    std::shared_ptr<const DynamicOptionsProvider> provider(const QString &backend_key) const;

    /** 查询 provider；未注册时 provider_found 为 false。 */
    DynamicOptionsResult resolve(const QString &backend_key, const QVariantMap &context = {}) const;

private:
    DynamicOptionsRegistry() = default;
    Q_DISABLE_COPY(DynamicOptionsRegistry)

    mutable QMutex mutex_;
    QHash<QString, std::shared_ptr<DynamicOptionsProvider>> providers_;
};

PARAMETER_API bool registerDynamicOptionsProvider(std::unique_ptr<DynamicOptionsProvider> provider);

} // namespace dltool::parameter
