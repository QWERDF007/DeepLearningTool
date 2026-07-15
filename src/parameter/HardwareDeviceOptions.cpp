#include "parameter/DynamicOptions.h"

#include <hwinfo/cpu.h>
#include <hwinfo/gpu.h>

#include <utility>
#include <vector>

namespace dltool::parameter {

namespace {

QVector<ParameterOption> cpuOptions()
{
    QVector<ParameterOption> options;
    const std::vector<hwinfo::CPU> cpus = hwinfo::getAllCPUs();
    options.reserve(static_cast<qsizetype>(cpus.size()));
    for (const hwinfo::CPU &cpu : cpus)
    {
        const QString id    = QString::number(cpu.id());
        const QString model = QString::fromStdString(cpu.modelName());
        options.push_back({QStringLiteral("CPU %1 · %2").arg(id, model), QStringLiteral("cpu:%1").arg(id)});
    }
    return options;
}

QVector<ParameterOption> gpuOptions()
{
    QVector<ParameterOption> options;
    const std::vector<hwinfo::GPU> gpus = hwinfo::getAllGPUs();
    options.reserve(static_cast<qsizetype>(gpus.size()));
    for (const hwinfo::GPU &gpu : gpus)
    {
        const QString id   = QString::number(gpu.id());
        const QString name = QString::fromStdString(gpu.name());
        options.push_back({QStringLiteral("GPU %1 · %2").arg(id, name), QStringLiteral("gpu:%1").arg(id)});
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
    QString key() const override { return QStringLiteral("hwinfo.cpu_devices"); }

    DynamicOptionsData query(const QVariantMap &context) const override
    {
        Q_UNUSED(context);
        return makeResult(cpuOptions());
    }
};

class GpuDeviceProvider final : public DynamicOptionsProvider
{
public:
    QString key() const override { return QStringLiteral("hwinfo.gpu_devices"); }

    DynamicOptionsData query(const QVariantMap &context) const override
    {
        Q_UNUSED(context);
        return makeResult(gpuOptions());
    }
};

class ComputeDeviceProvider final : public DynamicOptionsProvider
{
public:
    QString key() const override { return QStringLiteral("hwinfo.compute_devices"); }

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
