#include "parameter/DynamicOptions.h"

#include <inferrt/util/Device.hpp>

#include <string>
#include <utility>
#include <vector>

namespace dltool::parameter {

namespace {

ParameterOption makeDeviceOption(const QString &name, const QString &type, const int index)
{
    const QString actualValue = QStringLiteral("%1:%2").arg(type).arg(index);
    const QString displayName = name.trimmed().isEmpty() ? type.toUpper() : name.trimmed();
    return {QStringLiteral("%1 (%2)").arg(displayName, actualValue), actualValue};
}

QVector<ParameterOption> cpuOptions()
{
    QVector<ParameterOption> options;
    options.push_back(makeDeviceOption(QString::fromStdString(irt::util::getCPUDeviceName()), QStringLiteral("cpu"),
                                       0));
    return options;
}

QVector<ParameterOption> gpuOptions()
{
    QVector<ParameterOption> options;
    const std::vector<std::string> gpus = irt::util::getGPUDeviceNames();
    options.reserve(static_cast<qsizetype>(gpus.size()));
    for (int index = 0; index < static_cast<int>(gpus.size()); ++index)
    {
        options.push_back(makeDeviceOption(QString::fromStdString(gpus.at(static_cast<std::size_t>(index))),
                                            QStringLiteral("cuda"), index));
    }
    return options;
}

DynamicOptionsData makeResult(QVector<ParameterOption> options)
{
    DynamicOptionsData result;
    result.recommended_value = options.isEmpty() ? QVariant{} : options.front().actual_value;
    result.options           = std::move(options);
    return result;
}

class CpuDeviceProvider final : public DynamicOptionsProvider
{
public:
    QString key() const override { return QStringLiteral("inferrt.cpu_devices"); }

    DynamicOptionsData query(const QVariantMap &context) const override
    {
        Q_UNUSED(context);
        return makeResult(cpuOptions());
    }
};

class GpuDeviceProvider final : public DynamicOptionsProvider
{
public:
    QString key() const override { return QStringLiteral("inferrt.gpu_devices"); }

    DynamicOptionsData query(const QVariantMap &context) const override
    {
        Q_UNUSED(context);
        return makeResult(gpuOptions());
    }
};

class ComputeDeviceProvider final : public DynamicOptionsProvider
{
public:
    QString key() const override { return QStringLiteral("inferrt.compute_devices"); }

    DynamicOptionsData query(const QVariantMap &context) const override
    {
        Q_UNUSED(context);
        QVector<ParameterOption> options = gpuOptions();
        options += cpuOptions();
        return makeResult(std::move(options));
    }
};

[[maybe_unused]] const bool hardwareProvidersRegistered = []
{
    registerDynamicOptionsProvider(std::make_unique<CpuDeviceProvider>());
    registerDynamicOptionsProvider(std::make_unique<GpuDeviceProvider>());
    registerDynamicOptionsProvider(std::make_unique<ComputeDeviceProvider>());
    return true;
}();

} // namespace

} // namespace dltool::parameter
