#include "parameter/DynamicOptions.h"

#include <inferrt/util/Device.hpp>

#include <string>
#include <utility>
#include <vector>

namespace dltool::parameter {

namespace {

ParameterOption makeRuntimeOption(const QString &name, const QString &backend, const QString &device)
{
    const QString actualValue = QStringLiteral("%1:%2").arg(backend, device);
    const QString displayName = name.trimmed().isEmpty() ? device.toUpper() : name.trimmed();
    return {QStringLiteral("%1 (%2)").arg(displayName, actualValue), actualValue};
}

ParameterOption makeTrainingOption(const QString &name, const QString &device)
{
    const QString displayName = name.trimmed().isEmpty() ? device.toUpper() : name.trimmed();
    return {QStringLiteral("%1 (%2)").arg(displayName, device), device};
}

QVector<ParameterOption> cpuOptions()
{
    QVector<ParameterOption> options;
    const QString cpuName = QString::fromStdString(irt::util::getCPUDeviceName());
    for (const QString &backend : {QStringLiteral("onnxruntime"), QStringLiteral("openvino")})
        options.push_back(makeRuntimeOption(cpuName, backend, QStringLiteral("cpu")));
    return options;
}

QVector<ParameterOption> gpuOptions()
{
    QVector<ParameterOption> options;
    const std::vector<std::string> gpus = irt::util::getGPUDeviceNames();
    options.reserve(static_cast<qsizetype>(gpus.size() * 3));
    for (int index = 0; index < static_cast<int>(gpus.size()); ++index)
    {
        const QString gpuName = QString::fromStdString(gpus.at(static_cast<std::size_t>(index)));
        const QString device  = QString::number(index);
        for (const QString &backend : {QStringLiteral("tensorrt"), QStringLiteral("onnxruntime"),
                                       QStringLiteral("openvino")})
            options.push_back(makeRuntimeOption(gpuName, backend, device));
    }
    return options;
}

QVector<ParameterOption> trainingCpuOptions()
{
    const QString cpuName = QString::fromStdString(irt::util::getCPUDeviceName());
    return {makeTrainingOption(cpuName, QStringLiteral("cpu"))};
}

QVector<ParameterOption> trainingGpuOptions()
{
    QVector<ParameterOption> options;
    const std::vector<std::string> gpus = irt::util::getGPUDeviceNames();
    options.reserve(static_cast<qsizetype>(gpus.size()));
    for (int index = 0; index < static_cast<int>(gpus.size()); ++index)
    {
        const QString gpuName = QString::fromStdString(gpus.at(static_cast<std::size_t>(index)));
        options.push_back(makeTrainingOption(gpuName, QStringLiteral("cuda:%1").arg(index)));
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

class TrainingComputeDeviceProvider final : public DynamicOptionsProvider
{
public:
    QString key() const override { return QStringLiteral("training.compute_devices"); }

    DynamicOptionsData query(const QVariantMap &context) const override
    {
        Q_UNUSED(context);
        QVector<ParameterOption> options = trainingGpuOptions();
        options += trainingCpuOptions();
        return makeResult(std::move(options));
    }
};

[[maybe_unused]] const bool hardwareProvidersRegistered = []
{
    registerDynamicOptionsProvider(std::make_unique<CpuDeviceProvider>());
    registerDynamicOptionsProvider(std::make_unique<GpuDeviceProvider>());
    registerDynamicOptionsProvider(std::make_unique<ComputeDeviceProvider>());
    registerDynamicOptionsProvider(std::make_unique<TrainingComputeDeviceProvider>());
    return true;
}();

} // namespace

} // namespace dltool::parameter
