#include "feature/SmartAnnotationController.h"

#include "settings/GlobalSettings.h"

#include <cuda_runtime_api.h>
#include <inferrt/model/IModel.h>
#include <inferrt/model/ModelFactory.hpp>
#include <spdlog/spdlog.h>

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QLineF>
#include <QMetaObject>
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dltool::feature {

namespace {

constexpr int         kSamMaxPoints        = 16;
constexpr const char *kDefaultModelName    = "edge_sam";
constexpr const char *kDefaultModelRoot    = "F:/models";
constexpr const char *kTensorRtBackendName = "tensorrt";

struct PromptPoint
{
    QPointF point;
    int     label{1};
};

struct PreprocessedImage
{
    std::vector<float> tensor;
    int                resized_h{0};
    int                resized_w{0};
};

class SmartModelConfig : public irt::model::IModelConfig
{
public:
    void setNumClasses(int num_classes) override
    {
        num_classes_ = num_classes;
    }

    void setInputShape(const nvinfer1::Dims4 &input_shape) override
    {
        if (input_shapes_.empty())
        {
            input_shapes_.push_back(input_shape);
        }
        else
        {
            input_shapes_.front() = input_shape;
        }
        syncDynamicBatch(input_shape.d[0]);
    }

    void setInputShapes(std::vector<nvinfer1::Dims4> input_shapes) override
    {
        input_shapes_ = std::move(input_shapes);
        if (!input_shapes_.empty())
        {
            syncDynamicBatch(input_shapes_.front().d[0]);
        }
    }

    void setInputTensorNames(std::vector<std::string> input_tensor_names) override
    {
        input_tensor_names_ = std::move(input_tensor_names);
    }

    void setOutputTensorNames(std::vector<std::string> output_tensor_names) override
    {
        output_tensor_names_ = std::move(output_tensor_names);
    }

    void setFeatureTensorNames(std::vector<std::string> feature_tensor_names) override
    {
        feature_tensor_names_ = std::move(feature_tensor_names);
    }

    void setFeatureOnly(bool feature_only) override
    {
        feature_only_ = feature_only;
    }

    void setDynamicBatch(bool dynamic_batch) noexcept override
    {
        dynamic_batch_ = dynamic_batch;
        if (!dynamic_batch_)
        {
            dynamic_batch_range_explicit_ = false;
            return;
        }
        if (!dynamic_batch_range_explicit_ && !input_shapes_.empty())
        {
            syncDynamicBatch(input_shapes_.front().d[0]);
        }
    }

    void setDynamicBatchRange(int min_batch, int opt_batch, int max_batch) noexcept override
    {
        dynamic_batch_                = true;
        dynamic_batch_range_explicit_ = true;
        min_batch_size_               = min_batch;
        opt_batch_size_               = opt_batch;
        max_batch_size_               = max_batch;
    }

    void setBackend(irt::model::ModelBackend backend) noexcept override
    {
        backend_ = backend;
    }

    void setDevice(irt::model::ModelDevice device) noexcept override
    {
        device_ = device;
    }

    int numClasses() const noexcept override
    {
        return num_classes_;
    }

    const nvinfer1::Dims4 &inputShape() const noexcept override
    {
        return input_shapes_.front();
    }

    const std::vector<nvinfer1::Dims4> &inputShapes() const noexcept override
    {
        return input_shapes_;
    }

    const std::vector<std::string> &inputTensorNames() const noexcept override
    {
        return input_tensor_names_;
    }

    const std::vector<std::string> &outputTensorNames() const noexcept override
    {
        return output_tensor_names_;
    }

    const std::vector<std::string> &featureTensorNames() const noexcept override
    {
        return feature_tensor_names_;
    }

    bool featureOnly() const noexcept override
    {
        return feature_only_;
    }

    bool dynamicBatch() const noexcept override
    {
        return dynamic_batch_;
    }

    int minBatchSize() const noexcept override
    {
        return min_batch_size_;
    }

    int optBatchSize() const noexcept override
    {
        return opt_batch_size_;
    }

    int maxBatchSize() const noexcept override
    {
        return max_batch_size_;
    }

    irt::model::ModelBackend backend() const noexcept override
    {
        return backend_;
    }

    irt::model::ModelDevice device() const noexcept override
    {
        return device_;
    }

private:
    void syncDynamicBatch(int64_t input_batch) noexcept
    {
        if (!dynamic_batch_ || dynamic_batch_range_explicit_ || input_batch <= 0
            || input_batch > std::numeric_limits<int>::max())
        {
            return;
        }

        const int batch = static_cast<int>(input_batch);
        min_batch_size_ = 1;
        opt_batch_size_ = batch;
        if (max_batch_size_ < opt_batch_size_)
        {
            max_batch_size_ = opt_batch_size_;
        }
    }
};

struct MaskPoint
{
    int x{0};
    int y{0};
};

bool operator==(const MaskPoint &left, const MaskPoint &right)
{
    return left.x == right.x && left.y == right.y;
}

struct BoundaryEdge
{
    MaskPoint from;
    MaskPoint to;
    int       direction{0};
    bool      used{false};
};

class GpuBuffer
{
public:
    explicit GpuBuffer(size_t bytes)
    {
        allocate(bytes);
    }

    ~GpuBuffer()
    {
        if (data_ != nullptr)
        {
            cudaFree(data_);
        }
    }

    GpuBuffer(const GpuBuffer &)            = delete;
    GpuBuffer &operator=(const GpuBuffer &) = delete;

    GpuBuffer(GpuBuffer &&other) noexcept
        : data_(other.data_)
        , bytes_(other.bytes_)
    {
        other.data_  = nullptr;
        other.bytes_ = 0;
    }

    GpuBuffer &operator=(GpuBuffer &&other) noexcept
    {
        if (this != &other)
        {
            if (data_ != nullptr)
            {
                cudaFree(data_);
            }
            data_        = other.data_;
            bytes_       = other.bytes_;
            other.data_  = nullptr;
            other.bytes_ = 0;
        }
        return *this;
    }

    void *data() const
    {
        return data_;
    }

    size_t sizeBytes() const
    {
        return bytes_;
    }

private:
    void allocate(size_t bytes)
    {
        if (bytes == 0)
        {
            return;
        }
        cudaError_t status = cudaMalloc(&data_, bytes);
        if (status != cudaSuccess)
        {
            throw std::runtime_error(std::string("cudaMalloc failed: ") + cudaGetErrorString(status));
        }
        bytes_ = bytes;
    }

    void  *data_{nullptr};
    size_t bytes_{0};
};

QString normalizedModelName(QString value)
{
    value = value.trimmed();
    return value.isEmpty() ? QString::fromLatin1(kDefaultModelName) : value;
}

QString normalizedBackend(QString value)
{
    value = value.trimmed().toLower();
    if (value == QStringLiteral("trt"))
    {
        return QStringLiteral("tensorrt");
    }
    if (value == QStringLiteral("onnx") || value == QStringLiteral("ort"))
    {
        return QStringLiteral("onnxruntime");
    }
    if (value == QStringLiteral("ov"))
    {
        return QStringLiteral("openvino");
    }
    if (value == QStringLiteral("openvino") || value == QStringLiteral("onnxruntime"))
    {
        return value;
    }
    return QStringLiteral("tensorrt");
}

QString normalizedDevice(QString value)
{
    value = value.trimmed().toLower();
    if (value == QStringLiteral("cpu"))
    {
        return QStringLiteral("cpu");
    }
    return QStringLiteral("gpu");
}

bool isTensorRtBackend(const QString &backend)
{
    return normalizedBackend(backend) == QString::fromLatin1(kTensorRtBackendName);
}

bool isSam2Model(const QString &model_name)
{
    return normalizedModelName(model_name).toLower().startsWith(QStringLiteral("sam2"));
}

irt::model::ModelBackend parseModelBackend(const QString &backend)
{
    const QString value = normalizedBackend(backend);
    if (value == QStringLiteral("openvino"))
    {
        return irt::model::ModelBackend::OpenVINO;
    }
    if (value == QStringLiteral("onnxruntime"))
    {
        return irt::model::ModelBackend::ONNXRuntime;
    }
    return irt::model::ModelBackend::TensorRT;
}

irt::model::ModelDevice parseModelDevice(const QString &device)
{
    return normalizedDevice(device) == QStringLiteral("cpu") ? irt::model::ModelDevice::CPU
                                                             : irt::model::ModelDevice::GPU;
}

struct SmartModelLoadRequest
{
    QString model_name;
    QString backend;
    QString device;
    QString absolute_model_path;
    QString runtime_model_name;
    QString key;
};

SmartModelLoadRequest buildSmartModelLoadRequest(const QString &model_name, const QString &model_path,
                                                 const QString &backend, const QString &device)
{
    const QFileInfo       model_info(model_path);
    SmartModelLoadRequest request;
    request.model_name          = normalizedModelName(model_name);
    request.backend             = normalizedBackend(backend);
    request.device              = normalizedDevice(device);
    request.absolute_model_path = model_info.absoluteFilePath();
    request.runtime_model_name  = isTensorRtBackend(request.backend) ? request.model_name : QStringLiteral("onnx");
    request.key                 = QString("%1|%2|%3|%4")
                      .arg(request.model_name.toLower(), request.backend, request.device,
                           QDir::cleanPath(request.absolute_model_path).toCaseFolded());
    return request;
}

std::unique_ptr<irt::model::IModel> loadSmartModel(const SmartModelLoadRequest &request)
{
    auto config = std::make_unique<SmartModelConfig>();
    config->setBackend(parseModelBackend(request.backend));
    config->setDevice(parseModelDevice(request.device));

    spdlog::info("Loading smart annotation model: model={}, runtime={}, backend={}, device={}, path={}",
                 request.model_name.toStdString(), request.runtime_model_name.toStdString(),
                 request.backend.toStdString(), request.device.toStdString(),
                 request.absolute_model_path.toStdString());
    auto model = irt::model::CreateModel(request.runtime_model_name.toStdString(), std::move(config));
    if (!model)
    {
        throw std::runtime_error(
            QString("Failed to create InferRT model: %1").arg(request.runtime_model_name).toStdString());
    }
    model->setLogLevel(nvinfer1::ILogger::Severity::kWARNING);
    model->buildOrLoad(request.absolute_model_path.toStdString());
    spdlog::info("Smart annotation model loaded: model={}, backend={}, device={}, path={}",
                 request.model_name.toStdString(), request.backend.toStdString(), request.device.toStdString(),
                 request.absolute_model_path.toStdString());
    return model;
}

void checkCuda(cudaError_t status, const char *operation)
{
    if (status != cudaSuccess)
    {
        throw std::runtime_error(std::string(operation) + " failed: " + cudaGetErrorString(status));
    }
}

size_t elementCount(const nvinfer1::Dims &dims)
{
    if (dims.nbDims <= 0)
    {
        throw std::runtime_error("Invalid tensor shape");
    }

    size_t count = 1;
    for (int i = 0; i < dims.nbDims; ++i)
    {
        if (dims.d[i] <= 0)
        {
            throw std::runtime_error("Dynamic or invalid tensor shape is not supported for smart annotation");
        }
        count *= static_cast<size_t>(dims.d[i]);
    }
    return count;
}

std::vector<PromptPoint> parsePromptPoints(const QVariantList &prompt_points)
{
    std::vector<PromptPoint> points;
    points.reserve(static_cast<size_t>(prompt_points.size()));

    int positive_count = 0;
    for (const QVariant &entry : prompt_points)
    {
        const QVariantMap map = entry.toMap();
        if (map.isEmpty())
        {
            continue;
        }

        PromptPoint point;
        point.point = QPointF(map.value(QStringLiteral("x")).toDouble(), map.value(QStringLiteral("y")).toDouble());
        point.label = map.value(QStringLiteral("label"), 1).toInt() > 0 ? 1 : 0;
        if (point.label == 1)
        {
            ++positive_count;
        }
        points.push_back(point);
    }

    if (points.empty())
    {
        throw std::runtime_error("请先添加智能标注提示点");
    }
    if (positive_count == 0)
    {
        throw std::runtime_error("智能标注至少需要一个 positive 点");
    }
    if (points.size() > static_cast<size_t>(kSamMaxPoints))
    {
        throw std::runtime_error("智能标注最多支持 16 个提示点");
    }
    return points;
}

QImage loadRgbImage(const QString &path)
{
    QImage image(path);
    if (image.isNull())
    {
        throw std::runtime_error(QString("图像加载失败: %1").arg(path).toStdString());
    }
    return image.convertToFormat(QImage::Format_RGB888);
}

std::pair<int, int> computeResizeShape(int original_h, int original_w, int target_size)
{
    const double scale = static_cast<double>(target_size) / static_cast<double>(std::max(original_h, original_w));
    return {std::max(1, static_cast<int>(std::floor(static_cast<double>(original_h) * scale + 0.5))),
            std::max(1, static_cast<int>(std::floor(static_cast<double>(original_w) * scale + 0.5)))};
}

PreprocessedImage preprocessImage(const QImage &rgb_image, int input_h, int input_w, bool use_sam2_preprocess)
{
    if (input_h <= 0 || input_w <= 0 || input_h != input_w)
    {
        throw std::runtime_error("SAM input must be a valid square tensor");
    }

    const int image_h = rgb_image.height();
    const int image_w = rgb_image.width();
    if (image_h <= 0 || image_w <= 0)
    {
        throw std::runtime_error("Invalid source image size");
    }

    if (use_sam2_preprocess)
    {
        static constexpr float kMean[3]{0.485F, 0.456F, 0.406F};
        static constexpr float kStd[3]{0.229F, 0.224F, 0.225F};

        const QImage resized = rgb_image.scaled(input_w, input_h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                   .convertToFormat(QImage::Format_RGB888);
        std::vector<float> tensor(static_cast<size_t>(3) * input_h * input_w);
        for (int y = 0; y < input_h; ++y)
        {
            const auto *row = resized.constScanLine(y);
            for (int x = 0; x < input_w; ++x)
            {
                const auto *pixel = row + x * 3;
                for (int c = 0; c < 3; ++c)
                {
                    const size_t offset
                        = static_cast<size_t>(c) * input_h * input_w + static_cast<size_t>(y) * input_w + x;
                    tensor[offset] = (static_cast<float>(pixel[c]) / 255.0F - kMean[c]) / kStd[c];
                }
            }
        }
        return {std::move(tensor), input_h, input_w};
    }

    static constexpr float kMean[3]{123.675F, 116.28F, 103.53F};
    static constexpr float kStd[3]{58.395F, 57.12F, 57.375F};

    const auto [resized_h, resized_w] = computeResizeShape(image_h, image_w, input_h);
    const QImage resized = rgb_image.scaled(resized_w, resized_h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                               .convertToFormat(QImage::Format_RGB888);

    std::vector<float> tensor(static_cast<size_t>(3) * input_h * input_w, 0.0F);
    for (int y = 0; y < resized_h; ++y)
    {
        const auto *row = resized.constScanLine(y);
        for (int x = 0; x < resized_w; ++x)
        {
            const auto *pixel = row + x * 3;
            for (int c = 0; c < 3; ++c)
            {
                const size_t offset = static_cast<size_t>(c) * input_h * input_w + static_cast<size_t>(y) * input_w + x;
                tensor[offset]      = (static_cast<float>(pixel[c]) - kMean[c]) / kStd[c];
            }
        }
    }
    return {std::move(tensor), resized_h, resized_w};
}

float scalePromptCoordinate(double value, int original_size, int resized_size)
{
    if (original_size <= 0 || resized_size <= 0)
    {
        return 0.0F;
    }
    const double clamped = std::clamp(value, 0.0, static_cast<double>(std::max(0, original_size - 1)));
    return static_cast<float>(clamped * static_cast<double>(resized_size) / static_cast<double>(original_size));
}

std::vector<float> resizeBilinear(const float *src, int src_w, int src_h, int dst_w, int dst_h)
{
    if (src == nullptr || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0)
    {
        return {};
    }

    std::vector<float> dst(static_cast<size_t>(dst_w) * dst_h, 0.0F);
    const double       x_scale = dst_w > 1 ? static_cast<double>(src_w - 1) / static_cast<double>(dst_w - 1) : 0.0;
    const double       y_scale = dst_h > 1 ? static_cast<double>(src_h - 1) / static_cast<double>(dst_h - 1) : 0.0;

    for (int y = 0; y < dst_h; ++y)
    {
        const double src_y = y * y_scale;
        const int    y0    = static_cast<int>(std::floor(src_y));
        const int    y1    = std::min(y0 + 1, src_h - 1);
        const double wy    = src_y - y0;

        for (int x = 0; x < dst_w; ++x)
        {
            const double src_x = x * x_scale;
            const int    x0    = static_cast<int>(std::floor(src_x));
            const int    x1    = std::min(x0 + 1, src_w - 1);
            const double wx    = src_x - x0;

            const float v00 = src[static_cast<size_t>(y0) * src_w + x0];
            const float v10 = src[static_cast<size_t>(y0) * src_w + x1];
            const float v01 = src[static_cast<size_t>(y1) * src_w + x0];
            const float v11 = src[static_cast<size_t>(y1) * src_w + x1];

            const double top                        = v00 * (1.0 - wx) + v10 * wx;
            const double bottom                     = v01 * (1.0 - wx) + v11 * wx;
            dst[static_cast<size_t>(y) * dst_w + x] = static_cast<float>(top * (1.0 - wy) + bottom * wy);
        }
    }
    return dst;
}

std::vector<float> cropTopLeft(const std::vector<float> &src, int src_w, int crop_w, int crop_h)
{
    if (src_w <= 0 || crop_w <= 0 || crop_h <= 0)
    {
        return {};
    }

    std::vector<float> dst(static_cast<size_t>(crop_w) * crop_h, 0.0F);
    for (int y = 0; y < crop_h; ++y)
    {
        const size_t src_offset = static_cast<size_t>(y) * src_w;
        const size_t dst_offset = static_cast<size_t>(y) * crop_w;
        std::copy_n(src.begin() + static_cast<std::ptrdiff_t>(src_offset), crop_w,
                    dst.begin() + static_cast<std::ptrdiff_t>(dst_offset));
    }
    return dst;
}

QRectF boundingBoxFromMask(const std::vector<uint8_t> &mask, int width, int height)
{
    int x_min = width;
    int y_min = height;
    int x_max = -1;
    int y_max = -1;

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            if (mask[static_cast<size_t>(y) * width + x] == 0)
            {
                continue;
            }
            x_min = std::min(x_min, x);
            y_min = std::min(y_min, y);
            x_max = std::max(x_max, x);
            y_max = std::max(y_max, y);
        }
    }

    if (x_max < x_min || y_max < y_min)
    {
        return {};
    }
    return QRectF(x_min, y_min, x_max - x_min + 1, y_max - y_min + 1);
}

double polygonArea(const std::vector<QPointF> &points)
{
    if (points.size() < 3)
    {
        return 0.0;
    }

    double area = 0.0;
    for (size_t i = 0; i < points.size(); ++i)
    {
        const QPointF &a = points[i];
        const QPointF &b = points[(i + 1) % points.size()];
        area += a.x() * b.y() - b.x() * a.y();
    }
    return std::abs(area) / 2.0;
}

double signedPolygonArea(const std::vector<QPointF> &points)
{
    if (points.size() < 3)
    {
        return 0.0;
    }

    double area = 0.0;
    for (size_t i = 0; i < points.size(); ++i)
    {
        const QPointF &a = points[i];
        const QPointF &b = points[(i + 1) % points.size()];
        area += a.x() * b.y() - b.x() * a.y();
    }
    return area / 2.0;
}

double distanceToSegment(const QPointF &point, const QPointF &a, const QPointF &b)
{
    const double dx   = b.x() - a.x();
    const double dy   = b.y() - a.y();
    const double len2 = dx * dx + dy * dy;
    if (len2 <= 0.000001)
    {
        return QLineF(point, a).length();
    }

    const double  t = std::clamp(((point.x() - a.x()) * dx + (point.y() - a.y()) * dy) / len2, 0.0, 1.0);
    const QPointF projection(a.x() + t * dx, a.y() + t * dy);
    return QLineF(point, projection).length();
}

std::vector<QPointF> normalizePolygon(std::vector<QPointF> points)
{
    std::vector<QPointF> normalized;
    normalized.reserve(points.size());
    for (const QPointF &point : points)
    {
        if (!normalized.empty() && QLineF(normalized.back(), point).length() < 0.001)
        {
            continue;
        }
        normalized.push_back(point);
    }

    if (normalized.size() > 1 && QLineF(normalized.front(), normalized.back()).length() < 0.001)
    {
        normalized.pop_back();
    }

    if (normalized.size() < 3 || polygonArea(normalized) <= 0.5)
    {
        return {};
    }
    return normalized;
}

std::vector<QPointF> removeCollinearPoints(const std::vector<QPointF> &points, double tolerance)
{
    if (points.size() < 4)
    {
        return points;
    }

    std::vector<QPointF> filtered;
    filtered.reserve(points.size());
    for (size_t i = 0; i < points.size(); ++i)
    {
        const QPointF &prev = points[(i + points.size() - 1) % points.size()];
        const QPointF &curr = points[i];
        const QPointF &next = points[(i + 1) % points.size()];
        if (distanceToSegment(curr, prev, next) <= tolerance)
        {
            continue;
        }
        filtered.push_back(curr);
    }

    return filtered.size() >= 3 ? filtered : points;
}

std::vector<QPointF> simplifyOpenPolyline(const std::vector<QPointF> &points, double epsilon)
{
    if (points.size() <= 2)
    {
        return points;
    }

    std::vector<uint8_t> keep(points.size(), 0);
    keep.front() = 1;
    keep.back()  = 1;

    std::vector<std::pair<size_t, size_t>> ranges;
    ranges.emplace_back(0, points.size() - 1);

    while (!ranges.empty())
    {
        const auto [first, last] = ranges.back();
        ranges.pop_back();

        if (last <= first + 1)
        {
            continue;
        }

        double max_distance = 0.0;
        size_t max_index    = first;
        for (size_t i = first + 1; i < last; ++i)
        {
            const double distance = distanceToSegment(points[i], points[first], points[last]);
            if (distance > max_distance)
            {
                max_distance = distance;
                max_index    = i;
            }
        }

        if (max_distance > epsilon)
        {
            keep[max_index] = 1;
            ranges.emplace_back(first, max_index);
            ranges.emplace_back(max_index, last);
        }
    }

    std::vector<QPointF> simplified;
    simplified.reserve(points.size());
    for (size_t i = 0; i < points.size(); ++i)
    {
        if (keep[i] != 0)
        {
            simplified.push_back(points[i]);
        }
    }
    return simplified;
}

std::vector<QPointF> simplifyClosedPolygon(const std::vector<QPointF> &points, double epsilon)
{
    if (points.size() < 6)
    {
        return points;
    }

    size_t split_index  = 1;
    double max_distance = 0.0;
    for (size_t i = 1; i < points.size(); ++i)
    {
        const double distance = QLineF(points.front(), points[i]).length();
        if (distance > max_distance)
        {
            max_distance = distance;
            split_index  = i;
        }
    }

    if (split_index == 0 || split_index >= points.size())
    {
        return points;
    }

    std::vector<QPointF> first_chain(points.begin(), points.begin() + static_cast<std::ptrdiff_t>(split_index) + 1);
    std::vector<QPointF> second_chain(points.begin() + static_cast<std::ptrdiff_t>(split_index), points.end());
    second_chain.push_back(points.front());

    first_chain  = simplifyOpenPolyline(first_chain, epsilon);
    second_chain = simplifyOpenPolyline(second_chain, epsilon);

    std::vector<QPointF> simplified;
    simplified.reserve(first_chain.size() + second_chain.size());
    simplified.insert(simplified.end(), first_chain.begin(), first_chain.end());
    for (size_t i = 1; i + 1 < second_chain.size(); ++i)
    {
        simplified.push_back(second_chain[i]);
    }

    return normalizePolygon(simplified);
}

int edgeDirection(const MaskPoint &from, const MaskPoint &to)
{
    if (to.x > from.x)
    {
        return 0;
    }
    if (to.y > from.y)
    {
        return 1;
    }
    if (to.x < from.x)
    {
        return 2;
    }
    return 3;
}

int64_t maskPointKey(const MaskPoint &point, int width)
{
    return static_cast<int64_t>(point.y) * static_cast<int64_t>(width + 1) + point.x;
}

int turnPriority(int previous_direction, int next_direction)
{
    const int turn = (next_direction - previous_direction + 4) % 4;
    if (turn == 1)
    {
        return 3;
    }
    if (turn == 0)
    {
        return 2;
    }
    if (turn == 3)
    {
        return 1;
    }
    return 0;
}

size_t chooseNextBoundaryEdge(const std::unordered_map<int64_t, std::vector<size_t>> &outgoing_edges,
                              const std::vector<BoundaryEdge> &edges, const MaskPoint &point, int width,
                              int previous_direction)
{
    const auto outgoing_it = outgoing_edges.find(maskPointKey(point, width));
    if (outgoing_it == outgoing_edges.end())
    {
        return std::numeric_limits<size_t>::max();
    }

    size_t best_index    = std::numeric_limits<size_t>::max();
    int    best_priority = -1;
    for (const size_t edge_index : outgoing_it->second)
    {
        const BoundaryEdge &edge = edges[edge_index];
        if (edge.used)
        {
            continue;
        }

        const int priority = turnPriority(previous_direction, edge.direction);
        if (priority > best_priority)
        {
            best_priority = priority;
            best_index    = edge_index;
        }
    }

    return best_index;
}

std::vector<std::vector<QPointF>> maskToPolygons(const std::vector<uint8_t> &mask, int width, int height)
{
    const int64_t total = static_cast<int64_t>(width) * static_cast<int64_t>(height);
    if (width <= 0 || height <= 0 || total <= 0 || mask.size() < static_cast<size_t>(total))
    {
        return {};
    }

    std::vector<BoundaryEdge>                        edges;
    std::unordered_map<int64_t, std::vector<size_t>> outgoing_edges;

    const auto is_foreground = [&](int x, int y) -> bool
    {
        return x >= 0 && y >= 0 && x < width && y < height && mask[static_cast<size_t>(y * width + x)] != 0;
    };

    const auto add_edge = [&](const MaskPoint &from, const MaskPoint &to)
    {
        const size_t edge_index = edges.size();
        edges.push_back(BoundaryEdge{from, to, edgeDirection(from, to), false});
        outgoing_edges[maskPointKey(from, width)].push_back(edge_index);
    };

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            if (!is_foreground(x, y))
            {
                continue;
            }

            if (!is_foreground(x, y - 1))
            {
                add_edge(MaskPoint{x, y}, MaskPoint{x + 1, y});
            }
            if (!is_foreground(x + 1, y))
            {
                add_edge(MaskPoint{x + 1, y}, MaskPoint{x + 1, y + 1});
            }
            if (!is_foreground(x, y + 1))
            {
                add_edge(MaskPoint{x + 1, y + 1}, MaskPoint{x, y + 1});
            }
            if (!is_foreground(x - 1, y))
            {
                add_edge(MaskPoint{x, y + 1}, MaskPoint{x, y});
            }
        }
    }

    if (edges.empty())
    {
        return {};
    }

    std::vector<std::vector<QPointF>> polygons;
    polygons.reserve(8);

    for (size_t start_edge_index = 0; start_edge_index < edges.size(); ++start_edge_index)
    {
        if (edges[start_edge_index].used)
        {
            continue;
        }

        const MaskPoint        start_point = edges[start_edge_index].from;
        size_t                 edge_index  = start_edge_index;
        bool                   closed      = false;
        std::vector<MaskPoint> loop;
        loop.reserve(256);

        for (size_t guard = 0; edge_index != std::numeric_limits<size_t>::max() && guard <= edges.size(); ++guard)
        {
            BoundaryEdge &edge = edges[edge_index];
            if (edge.used)
            {
                break;
            }

            edge.used = true;
            if (loop.empty())
            {
                loop.push_back(edge.from);
            }
            loop.push_back(edge.to);

            if (edge.to == start_point)
            {
                closed = true;
                break;
            }

            edge_index = chooseNextBoundaryEdge(outgoing_edges, edges, edge.to, width, edge.direction);
        }

        if (!closed || loop.size() < 4)
        {
            continue;
        }
        if (loop.back() == loop.front())
        {
            loop.pop_back();
        }

        std::vector<QPointF> points;
        points.reserve(loop.size());
        for (const MaskPoint &point : loop)
        {
            points.emplace_back(point.x, point.y);
        }

        points = normalizePolygon(std::move(points));
        if (points.empty() || signedPolygonArea(points) <= 0.5)
        {
            continue;
        }

        points = removeCollinearPoints(points, 0.001);
        points = normalizePolygon(std::move(points));
        if (points.empty() || signedPolygonArea(points) <= 0.5)
        {
            continue;
        }

        const double epsilon = std::clamp(std::sqrt(polygonArea(points)) * 0.01, 1.0, 3.0);
        points               = simplifyClosedPolygon(points, epsilon);
        points               = removeCollinearPoints(points, 0.01);
        points               = normalizePolygon(std::move(points));
        if (!points.empty() && signedPolygonArea(points) > 0.5)
        {
            polygons.push_back(std::move(points));
        }
    }

    std::sort(polygons.begin(), polygons.end(), [](const std::vector<QPointF> &left, const std::vector<QPointF> &right)
              { return polygonArea(left) > polygonArea(right); });
    return polygons;
}

QVariantList pointsToVariantList(const std::vector<QPointF> &points)
{
    QVariantList result;
    result.reserve(static_cast<int>(points.size()));
    for (const QPointF &point : points)
    {
        result.push_back(QVariantMap{
            {QStringLiteral("x"), point.x()},
            {QStringLiteral("y"), point.y()},
        });
    }
    return result;
}

QVariantList maskRunsToVariantList(const std::vector<uint8_t> &mask, int width, int height)
{
    QVariantList result;
    for (int y = 0; y < height; ++y)
    {
        const size_t row_offset = static_cast<size_t>(y) * width;
        int          x          = 0;
        while (x < width)
        {
            while (x < width && mask[row_offset + static_cast<size_t>(x)] == 0)
            {
                ++x;
            }
            const int run_start = x;
            while (x < width && mask[row_offset + static_cast<size_t>(x)] != 0)
            {
                ++x;
            }
            const int run_width = x - run_start;
            if (run_width > 0)
            {
                result.push_back(QVariantMap{
                    {    QStringLiteral("x"), run_start},
                    {    QStringLiteral("y"),         y},
                    {QStringLiteral("width"), run_width},
                });
            }
        }
    }
    return result;
}

std::vector<QPointF> rectanglePoints(const QRectF &rect)
{
    return {
        QPointF(rect.left(), rect.top()),
        QPointF(rect.right(), rect.top()),
        QPointF(rect.right(), rect.bottom()),
        QPointF(rect.left(), rect.bottom()),
    };
}

std::vector<QPointF> simplifyFinalPolygon(std::vector<QPointF> polygon, double epsilon)
{
    polygon = normalizePolygon(std::move(polygon));
    if (polygon.empty() || epsilon <= 0.0)
    {
        return polygon;
    }

    std::vector<QPointF> simplified = simplifyClosedPolygon(polygon, epsilon);
    simplified                      = removeCollinearPoints(simplified, epsilon);
    simplified                      = normalizePolygon(std::move(simplified));
    return simplified.empty() ? polygon : simplified;
}

int bestMaskIndex(const float *iou_values, size_t iou_count, int candidate_count)
{
    if (iou_values == nullptr || iou_count == 0 || candidate_count <= 1)
    {
        return 0;
    }

    int       best_index = 0;
    float     best_iou   = iou_values[0];
    const int count      = std::min(candidate_count, static_cast<int>(iou_count));
    for (int i = 1; i < count; ++i)
    {
        if (iou_values[i] > best_iou)
        {
            best_iou   = iou_values[i];
            best_index = i;
        }
    }
    return best_index;
}

size_t outputCandidateCount(const nvinfer1::Dims &dims)
{
    if (dims.nbDims >= 4)
    {
        return static_cast<size_t>(std::max<int64_t>(1, dims.d[1]));
    }
    if (dims.nbDims == 3)
    {
        return 1;
    }
    return 1;
}

} // namespace

SmartAnnotationController::SmartAnnotationController(QObject *parent)
    : QObject(parent)
{
}

SmartAnnotationController::~SmartAnnotationController() = default;

QStringList SmartAnnotationController::supportedModelPresets() const
{
    return {
        QStringLiteral("edge_sam"),
        QStringLiteral("sam"),
        QStringLiteral("sam_vit_b"),
        QStringLiteral("sam_vit_l"),
        QStringLiteral("sam_vit_h"),
        QStringLiteral("sam2"),
        QStringLiteral("sam2_hiera_tiny"),
        QStringLiteral("sam2_hiera_small"),
        QStringLiteral("sam2_hiera_base_plus"),
        QStringLiteral("sam2_hiera_large"),
        QStringLiteral("sam2_1_hiera_tiny"),
        QStringLiteral("sam2_1_hiera_small"),
        QStringLiteral("sam2_1_hiera_base_plus"),
        QStringLiteral("sam2_1_hiera_large"),
    };
}

QString SmartAnnotationController::suggestedModelPath(const QString &model_name, const QString &backend) const
{
    const QString model     = normalizedModelName(model_name);
    const QString extension = isTensorRtBackend(backend) ? QStringLiteral(".wts") : QStringLiteral(".onnx");
    return QDir(QString::fromLatin1(kDefaultModelRoot)).filePath(model + extension);
}

void SmartAnnotationController::clearCache()
{
    model_.reset();
    cached_model_key_.clear();
    loading_model_key_.clear();
    setLoadingModel(false);
    setRunning(false);
}

bool SmartAnnotationController::ensureModel(const QString &model_name, const QString &model_path,
                                            const QString &backend, const QString &device)
{
    const SmartModelLoadRequest request = buildSmartModelLoadRequest(model_name, model_path, backend, device);

    if (model_ != nullptr && cached_model_key_ == request.key)
    {
        return true;
    }

    model_            = loadSmartModel(request);
    cached_model_key_ = request.key;
    return true;
}

void SmartAnnotationController::startAsyncModelLoad(const QString &model_name, const QString &model_path,
                                                    const QString &backend, const QString &device)
{
    const SmartModelLoadRequest request = buildSmartModelLoadRequest(model_name, model_path, backend, device);
    if (loading_model_ && loading_model_key_ == request.key)
    {
        return;
    }

    loading_model_key_ = request.key;
    setLastError(QString());
    setLoadingModel(true);
    setRunning(true);

    QPointer<SmartAnnotationController> controller(this);
    QThread                            *work_thread = QThread::create(
        [controller, request]()
        {
            auto    model_holder = std::make_shared<std::unique_ptr<irt::model::IModel>>();
            QString error;
            bool    success = false;

            try
            {
                *model_holder = loadSmartModel(request);
                success       = true;
            }
            catch (const std::exception &e)
            {
                error = QString::fromUtf8(e.what());
                spdlog::error("Smart annotation model load failed: {}", error.toStdString());
            }
            catch (...)
            {
                error = QStringLiteral("Unknown smart annotation model load error");
                spdlog::error("Smart annotation model load failed with unknown error");
            }

            if (!controller)
            {
                return;
            }

            QMetaObject::invokeMethod(
                controller.data(),
                [controller, request, model_holder, error, success]() mutable
                {
                    if (!controller)
                    {
                        return;
                    }

                    if (controller->loading_model_key_ != request.key)
                    {
                        return;
                    }

                    controller->loading_model_key_.clear();
                    if (success)
                    {
                        controller->model_            = std::move(*model_holder);
                        controller->cached_model_key_ = request.key;
                        controller->setLastError(QString());
                    }
                    else
                    {
                        controller->model_.reset();
                        controller->cached_model_key_.clear();
                        controller->setLastError(error);
                    }

                    controller->setLoadingModel(false);
                    controller->setRunning(false);
                    emit controller->modelLoadFinished(success);
                },
                Qt::QueuedConnection);
        });

    connect(work_thread, &QThread::finished, work_thread, &QObject::deleteLater);
    work_thread->start();
}

QVariantMap SmartAnnotationController::infer(const QString &image_path, const QVariantList &prompt_points)
{
    QVariantMap result{
        {QStringLiteral("success"), false},
        {  QStringLiteral("error"),    {}},
        {QStringLiteral("loading"), false}
    };

    if (loading_model_)
    {
        result[QStringLiteral("loading")] = true;
        return result;
    }

    if (running_)
    {
        const QString error = QString("智能标注正在运行");
        setLastError(error);
        result[QStringLiteral("error")] = error;
        return result;
    }

    try
    {
        auto *settings = dltool::settings::GlobalSettings::getInstance()->settingsGroup(
            QStringLiteral("advanced.smartAnnotation"));
        if (settings == nullptr || !settings->valueOr(QStringLiteral("enabled"), false).toBool())
        {
            throw std::runtime_error("智能标注未启用");
        }

        const std::vector<PromptPoint> prompts = parsePromptPoints(prompt_points);

        const QString model_name = normalizedModelName(settings->valueOr(QStringLiteral("model")).toString());
        const QString backend    = normalizedBackend(settings->valueOr(QStringLiteral("modelBackend")).toString());
        const QString device     = normalizedDevice(settings->valueOr(QStringLiteral("modelDevice")).toString());
        QString       model_path = settings->valueOr(QStringLiteral("modelPath")).toString().trimmed();
        if (model_path.isEmpty())
        {
            model_path = suggestedModelPath(model_name, backend);
        }

        const QFileInfo model_info(model_path);
        if (!model_info.isFile())
        {
            throw std::runtime_error(QString("智能标注模型文件不存在: %1").arg(model_path).toStdString());
        }

        const SmartModelLoadRequest request = buildSmartModelLoadRequest(model_name, model_path, backend, device);
        if (model_ == nullptr || cached_model_key_ != request.key)
        {
            startAsyncModelLoad(model_name, model_path, backend, device);
            result[QStringLiteral("loading")] = true;
            return result;
        }

        const QImage image = loadRgbImage(image_path);

        setRunning(true);

        const auto input_names  = model_->ioTensorNames(nvinfer1::TensorIOMode::kINPUT);
        const auto output_names = model_->ioTensorNames(nvinfer1::TensorIOMode::kOUTPUT);
        if (input_names.size() != 5 || output_names.size() != 3)
        {
            throw std::runtime_error("InferRT SAM model must expose the 5-input/3-output contract");
        }

        const auto image_dims       = model_->tensorShape(input_names[0]);
        const auto prompt_mask_dims = model_->tensorShape(input_names[3]);
        const auto output_mask_dims = model_->tensorShape(output_names[0]);
        const auto iou_dims         = model_->tensorShape(output_names[1]);

        if (image_dims.nbDims != 4 || prompt_mask_dims.nbDims != 4 || output_mask_dims.nbDims != 4)
        {
            throw std::runtime_error("Invalid SAM tensor shape");
        }

        const int input_h       = static_cast<int>(image_dims.d[2]);
        const int input_w       = static_cast<int>(image_dims.d[3]);
        const int prompt_mask_h = static_cast<int>(prompt_mask_dims.d[2]);
        const int prompt_mask_w = static_cast<int>(prompt_mask_dims.d[3]);
        const int output_mask_h = static_cast<int>(output_mask_dims.d[2]);
        const int output_mask_w = static_cast<int>(output_mask_dims.d[3]);

        PreprocessedImage preprocessed = preprocessImage(image, input_h, input_w, isSam2Model(model_name));

        std::vector<float> point_coords(static_cast<size_t>(kSamMaxPoints) * 2, 0.0F);
        std::vector<float> point_labels(kSamMaxPoints, -1.0F);
        for (size_t i = 0; i < prompts.size(); ++i)
        {
            const PromptPoint &prompt = prompts[i];
            point_coords[i * 2]       = scalePromptCoordinate(prompt.point.x(), image.width(), preprocessed.resized_w);
            point_coords[i * 2 + 1]   = scalePromptCoordinate(prompt.point.y(), image.height(), preprocessed.resized_h);
            point_labels[i]           = static_cast<float>(prompt.label);
        }

        std::vector<float> mask_input(static_cast<size_t>(prompt_mask_h) * prompt_mask_w, 0.0F);
        std::vector<float> has_mask_input{0.0F};

        const std::vector<std::vector<float> *> input_vectors{
            &preprocessed.tensor, &point_coords, &point_labels, &mask_input, &has_mask_input,
        };

        const bool         uses_tensorrt = isTensorRtBackend(backend);
        const cudaStream_t stream        = model_->resolveExecutionStream();

        std::vector<GpuBuffer>          device_inputs;
        std::vector<GpuBuffer>          device_outputs;
        std::vector<std::vector<float>> host_outputs;
        std::vector<void *>             buffers;
        device_inputs.reserve(input_vectors.size());
        device_outputs.reserve(output_names.size());
        host_outputs.reserve(output_names.size());
        buffers.reserve(input_vectors.size() + output_names.size());

        for (const auto *values : input_vectors)
        {
            if (uses_tensorrt)
            {
                device_inputs.emplace_back(values->size() * sizeof(float));
                buffers.push_back(device_inputs.back().data());
            }
            else
            {
                buffers.push_back(const_cast<float *>(values->data()));
            }
        }

        for (const auto &output_name : output_names)
        {
            const auto dims  = model_->tensorShape(output_name);
            const auto count = elementCount(dims);
            host_outputs.emplace_back(count, 0.0F);
            if (uses_tensorrt)
            {
                device_outputs.emplace_back(count * sizeof(float));
                buffers.push_back(device_outputs.back().data());
            }
            else
            {
                buffers.push_back(host_outputs.back().data());
            }
        }

        if (uses_tensorrt)
        {
            for (size_t i = 0; i < input_vectors.size(); ++i)
            {
                const auto *values = input_vectors[i];
                checkCuda(cudaMemcpyAsync(device_inputs[i].data(), values->data(), values->size() * sizeof(float),
                                          cudaMemcpyHostToDevice, stream),
                          "cudaMemcpyAsync(SAM input)");
            }
            checkCuda(cudaStreamSynchronize(stream), "cudaStreamSynchronize(SAM input)");
        }

        model_->infer(buffers, stream, true);

        if (uses_tensorrt)
        {
            checkCuda(cudaStreamSynchronize(stream), "cudaStreamSynchronize(SAM inference)");
            for (size_t i = 0; i < host_outputs.size(); ++i)
            {
                checkCuda(cudaMemcpyAsync(host_outputs[i].data(), device_outputs[i].data(),
                                          host_outputs[i].size() * sizeof(float), cudaMemcpyDeviceToHost, stream),
                          "cudaMemcpyAsync(SAM output)");
            }
            checkCuda(cudaStreamSynchronize(stream), "cudaStreamSynchronize(SAM output)");
        }

        const auto   candidates = static_cast<int>(outputCandidateCount(output_mask_dims));
        const size_t iou_count  = host_outputs[1].size();
        const int    mask_index = bestMaskIndex(host_outputs[1].data(), iou_count, candidates);
        const float  selected_iou
            = (mask_index >= 0 && static_cast<size_t>(mask_index) < iou_count) ? host_outputs[1][mask_index] : 0.0F;

        const size_t single_mask_count = static_cast<size_t>(output_mask_h) * output_mask_w;
        const size_t mask_offset       = static_cast<size_t>(mask_index) * single_mask_count;
        if (mask_offset + single_mask_count > host_outputs[0].size())
        {
            throw std::runtime_error("SAM mask output shape does not match candidate count");
        }

        std::vector<float> resized_mask
            = resizeBilinear(host_outputs[0].data() + static_cast<std::ptrdiff_t>(mask_offset), output_mask_w,
                             output_mask_h, input_w, input_h);
        std::vector<float> cropped_mask
            = cropTopLeft(resized_mask, input_w, preprocessed.resized_w, preprocessed.resized_h);
        std::vector<float> original_mask = resizeBilinear(cropped_mask.data(), preprocessed.resized_w,
                                                          preprocessed.resized_h, image.width(), image.height());

        const double threshold = settings->valueOr(QStringLiteral("maskThreshold"), 0.0).toDouble();
        std::vector<uint8_t> binary_mask(static_cast<size_t>(image.width()) * image.height(), 0);
        int                  foreground_pixels = 0;
        for (size_t i = 0; i < binary_mask.size(); ++i)
        {
            if (original_mask[i] > threshold)
            {
                binary_mask[i] = 1;
                ++foreground_pixels;
            }
        }

        const QRectF bbox = boundingBoxFromMask(binary_mask, image.width(), image.height());
        if (bbox.isEmpty())
        {
            throw std::runtime_error("SAM 没有生成有效 mask，请调整提示点或阈值");
        }

        std::vector<QPointF>              polygon;
        std::vector<std::vector<QPointF>> polygons = maskToPolygons(binary_mask, image.width(), image.height());
        if (!polygons.empty())
        {
            polygon = std::move(polygons.front());
        }
        else
        {
            polygon = rectanglePoints(bbox);
        }

        polygon = simplifyFinalPolygon(std::move(polygon),
                                       settings->valueOr(QStringLiteral("polygonSimplifyEpsilon"), 2.0).toDouble());
        if (polygon.size() < 3)
        {
            polygon = rectanglePoints(bbox);
        }

        result[QStringLiteral("success")]          = true;
        result[QStringLiteral("error")]            = QString();
        result[QStringLiteral("model_name")]       = model_name;
        result[QStringLiteral("model_path")]       = model_info.absoluteFilePath();
        result[QStringLiteral("backend")]          = backend;
        result[QStringLiteral("device")]           = device;
        result[QStringLiteral("image_path")]       = image_path;
        result[QStringLiteral("image_width")]      = image.width();
        result[QStringLiteral("image_height")]     = image.height();
        result[QStringLiteral("x")]                = bbox.x();
        result[QStringLiteral("y")]                = bbox.y();
        result[QStringLiteral("width")]            = bbox.width();
        result[QStringLiteral("height")]           = bbox.height();
        result[QStringLiteral("points")]           = pointsToVariantList(polygon);
        result[QStringLiteral("point_count")]      = static_cast<int>(polygon.size());
        result[QStringLiteral("prompt_count")]     = static_cast<int>(prompts.size());
        result[QStringLiteral("mask_index")]       = mask_index;
        result[QStringLiteral("iou")]              = selected_iou;
        result[QStringLiteral("mask_pixel_count")] = foreground_pixels;
        result[QStringLiteral("mask_width")]       = image.width();
        result[QStringLiteral("mask_height")]      = image.height();
        result[QStringLiteral("mask_runs")]        = maskRunsToVariantList(binary_mask, image.width(), image.height());

        setLastError(QString());
    }
    catch (const std::exception &e)
    {
        const QString error               = QString::fromUtf8(e.what());
        result[QStringLiteral("success")] = false;
        result[QStringLiteral("error")]   = error;
        setLastError(error);
        spdlog::error("Smart annotation failed: {}", error.toStdString());
    }
    catch (...)
    {
        const QString error               = QString("未知智能标注错误");
        result[QStringLiteral("success")] = false;
        result[QStringLiteral("error")]   = error;
        setLastError(error);
        spdlog::error("Smart annotation failed with unknown error");
    }

    setRunning(false);
    return result;
}

void SmartAnnotationController::setRunning(bool running)
{
    if (running_ == running)
    {
        return;
    }
    running_ = running;
    emit runningChanged();
}

void SmartAnnotationController::setLoadingModel(bool loading_model)
{
    if (loading_model_ == loading_model)
    {
        return;
    }
    loading_model_ = loading_model;
    emit loadingModelChanged();
}

void SmartAnnotationController::setLastError(const QString &last_error)
{
    if (last_error_ == last_error)
    {
        return;
    }
    last_error_ = last_error;
    emit lastErrorChanged();
}

} // namespace dltool::feature
