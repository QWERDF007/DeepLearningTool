#pragma once

#include <QString>
#include <QVariantList>
#include <algorithm>
#include <cstdint>
#include <vector>

#include <inferrt/model/IModel.h>

namespace dltool::feature {

inline QString normalizedBackend(QString value)
{
    value = value.trimmed().toLower();
    if (value == QStringLiteral("trt"))
        return QStringLiteral("tensorrt");
    if (value == QStringLiteral("onnx") || value == QStringLiteral("ort"))
        return QStringLiteral("onnxruntime");
    if (value == QStringLiteral("ov"))
        return QStringLiteral("openvino");
    if (value == QStringLiteral("openvino") || value == QStringLiteral("onnxruntime"))
        return value;
    return QStringLiteral("tensorrt");
}

inline QString normalizedDevice(QString value)
{
    value = value.trimmed().toLower();
    return value == QStringLiteral("cpu") ? QStringLiteral("cpu") : QStringLiteral("gpu");
}

inline irt::model::ModelBackend parseModelBackend(const QString &backend)
{
    const QString value = normalizedBackend(backend);
    if (value == QStringLiteral("openvino"))
        return irt::model::ModelBackend::OpenVINO;
    if (value == QStringLiteral("onnxruntime"))
        return irt::model::ModelBackend::ONNXRuntime;
    return irt::model::ModelBackend::TensorRT;
}

inline irt::model::ModelDevice parseModelDevice(const QString &device)
{
    return normalizedDevice(device) == QStringLiteral("cpu") ? irt::model::ModelDevice::CPU
                                                             : irt::model::ModelDevice::GPU;
}

inline std::vector<int64_t> parseInt64Ids(const QVariantList &values, bool filterNonNegative = false,
                                          bool sortAndUnique = false)
{
    std::vector<int64_t> result;
    result.reserve(static_cast<size_t>(values.size()));
    for (const QVariant &value : values)
    {
        bool    ok = false;
        int64_t id = value.toLongLong(&ok);
        if (ok && (!filterNonNegative || id >= 0))
            result.push_back(id);
    }
    if (sortAndUnique)
    {
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
    }
    return result;
}

} // namespace dltool::feature
