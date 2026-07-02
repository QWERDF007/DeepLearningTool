#include "feature/SmartAnnotationController.h"

#include "settings/GlobalSettings.h"
#include "settings/SettingsValue.h"

#include <inferrt/features/SAMImagePredictor.hpp>
#include <opencv2/imgproc.hpp>
#include <spdlog/spdlog.h>

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QLineF>
#include <QMetaObject>
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QSize>
#include <QTemporaryFile>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>


namespace dltool::feature {

namespace {

constexpr int kSamMaxPoints = 16; ///< SAM 模型最大提示点数

using dltool::settings::settingBool;
using dltool::settings::settingDouble;
using dltool::settings::settingInt;
using dltool::settings::settingString;

/// 提示点结构
struct PromptPoint
{
    QPointF point;    ///< 坐标
    int     label{1}; ///< 标签（1=前景，0=背景）
};

struct InferenceImageInput
{
    QString                         path;                  ///< 实际送入 SAM 的图像路径
    QRectF                          source_rect;           ///< 输入图像对应的原图区域
    QSize                           source_size;           ///< 原图尺寸
    QSize                           input_size;            ///< 输入图像尺寸
    double                          scale_x{1.0};          ///< 原图到输入图像的 X 缩放
    double                          scale_y{1.0};          ///< 原图到输入图像的 Y 缩放
    bool                            viewport_input{false}; ///< 是否使用可视窗口输入
    std::unique_ptr<QTemporaryFile> temporary_file;        ///< 临时输入图文件
};

/**
 * @brief 规范化模型名称（去除首尾空白）
 * @param value 模型名称
 * @return 规范化后的名称
 */
QString normalizedModelName(QString value)
{
    return value.trimmed();
}

std::filesystem::path toFilesystemPath(const QString &path)
{
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}

/// 智能标注模型加载请求
struct SmartModelLoadRequest
{
    QString                  model_name;                                  ///< 模型名称
    irt::model::ModelBackend backend{irt::model::ModelBackend::TensorRT}; ///< 推理后端
    irt::model::ModelDevice  device{irt::model::ModelDevice::GPU};        ///< 推理设备
    QString                  absolute_model_path;                         ///< 模型文件绝对路径
    QString                  key;                                         ///< 缓存唯一标识
};

/**
 * @brief 构建模型加载请求
 * @param model_name 模型名称
 * @param model_path 模型文件路径
 * @param backend 推理后端
 * @param device 推理设备
 * @return 模型加载请求
 */
SmartModelLoadRequest buildSmartModelLoadRequest(const QString &model_name, const QString &model_path,
                                                 const irt::model::ModelBackend backend,
                                                 const irt::model::ModelDevice  device)
{
    const QFileInfo       model_info(model_path);
    SmartModelLoadRequest request;
    request.model_name          = normalizedModelName(model_name);
    request.backend             = backend;
    request.device              = device;
    request.absolute_model_path = model_info.absoluteFilePath();
    const QString backend_name  = QString::fromLatin1(irt::model::modelBackendName(request.backend));
    const QString device_name   = QString::fromLatin1(irt::model::modelDeviceName(request.device));
    request.key                 = QString("%1|%2|%3|%4")
                      .arg(request.model_name.toLower(), backend_name, device_name,
                           QDir::cleanPath(request.absolute_model_path).toCaseFolded());
    return request;
}

irt::features::SAMImagePredictorConfig buildPredictorConfig(const SmartModelLoadRequest &request)
{
    irt::features::SAMImagePredictorConfig config;
    config.model_name    = request.model_name.toStdString();
    config.model_backend = request.backend;
    config.model_device  = request.device;
    return config;
}

std::unique_ptr<irt::features::SAMImagePredictor> loadSmartPredictor(const SmartModelLoadRequest &request)
{
    spdlog::info("加载智能标注 SAM 预测器，模型: {}, 后端: {}, 设备: {}, 模型路径: {}",
                 request.model_name.toUtf8().constData(), irt::model::modelBackendName(request.backend),
                 irt::model::modelDeviceName(request.device), request.absolute_model_path.toUtf8().constData());

    auto predictor = std::make_unique<irt::features::SAMImagePredictor>(buildPredictorConfig(request));
    predictor->load(toFilesystemPath(request.absolute_model_path));
    spdlog::info("加载智能标注 SAM 预测器完成");
    return predictor;
}

/**
 * @brief 解析 QML 传入的提示点列表
 * @param prompt_points QVariantList 格式的提示点
 * @return 解析后的提示点列表
 * @throws std::runtime_error 提示点无效时抛出
 */
std::vector<PromptPoint> parsePromptPoints(const QVariantList &prompt_points)
{
    std::vector<PromptPoint> points;
    points.reserve(static_cast<size_t>(prompt_points.size()));

    int positive_count = 0;
    for (const QVariant &entry : prompt_points)
    {
        const QVariantMap map = entry.toMap();
        if (map.isEmpty())
            continue;

        PromptPoint point;
        point.point = QPointF(map.value(QStringLiteral("x")).toDouble(), map.value(QStringLiteral("y")).toDouble());
        point.label = map.value(QStringLiteral("label"), 1).toInt() > 0 ? 1 : 0;
        if (point.label == 1)
            ++positive_count;
        points.push_back(point);
    }

    if (points.empty())
        throw std::runtime_error("请先添加智能标注提示点");
    if (positive_count == 0)
        throw std::runtime_error("智能标注至少需要一个 positive 点");
    if (points.size() > static_cast<size_t>(kSamMaxPoints))
        throw std::runtime_error("智能标注最多支持 16 个提示点");
    return points;
}

irt::features::SAMImagePrompt buildImagePrompt(const std::vector<PromptPoint> &prompts)
{
    irt::features::SAMImagePrompt prompt;
    prompt.coordinate_mode = irt::features::SAMPromptCoordinateMode::ImagePixels;
    prompt.points.reserve(prompts.size());
    for (const PromptPoint &point : prompts)
    {
        prompt.points.push_back(irt::features::SAMPromptPoint{static_cast<float>(point.point.x()),
                                                              static_cast<float>(point.point.y()), point.label});
    }
    return prompt;
}

irt::features::SAMImagePredictOptions buildPredictOptions(dltool::settings::GlobalSettings *settings)
{
    namespace generated_field = dltool::settings::generated::field;

    irt::features::SAMImagePredictOptions options;
    options.return_logits    = false;
    options.mask_output_mode = irt::features::SAMMaskOutputMode::Single;
    options.mask_threshold
        = static_cast<float>(settingDouble(settings, generated_field::SmartAnnotation::MaskThreshold, 0.0));
    options.max_hole_area = settingInt(settings, generated_field::SmartAnnotation::MaxHoleArea, 0);
    options.max_sprinkle_area = settingInt(settings, generated_field::SmartAnnotation::MaxSprinkleArea, 0);
    return options;
}

bool optionBool(const QVariantMap &options, const QString &key, bool fallback)
{
    if (!options.contains(key))
        return fallback;
    return options.value(key).toBool();
}

double optionDouble(const QVariantMap &options, const QString &key, double fallback)
{
    bool       ok    = false;
    const auto value = options.value(key).toDouble(&ok);
    return ok && std::isfinite(value) ? value : fallback;
}

int optionInt(const QVariantMap &options, const QString &key, int fallback)
{
    bool      ok    = false;
    const int value = options.value(key).toInt(&ok);
    return ok ? value : fallback;
}

QRect viewportSourceRect(const QVariantMap &viewport, const QSize &source_size)
{
    const int x      = static_cast<int>(std::floor(optionDouble(viewport, QStringLiteral("x"), 0.0)));
    const int y      = static_cast<int>(std::floor(optionDouble(viewport, QStringLiteral("y"), 0.0)));
    const int width  = static_cast<int>(std::ceil(optionDouble(viewport, QStringLiteral("width"), 0.0)));
    const int height = static_cast<int>(std::ceil(optionDouble(viewport, QStringLiteral("height"), 0.0)));

    if (width <= 0 || height <= 0)
        return {};
    return QRect(x, y, width, height).intersected(QRect(QPoint(0, 0), source_size));
}

InferenceImageInput prepareInferenceImageInput(const QString &image_path, const QVariantMap &options)
{
    InferenceImageInput input;
    input.path = image_path;

    if (!optionBool(options, QStringLiteral("use_viewport_input"), false))
        return input;

    QImage source(image_path);
    if (source.isNull())
        throw std::runtime_error(QString("读取可视窗口输入图像失败: %1").arg(image_path).toStdString());

    const QVariantMap viewport    = options.value(QStringLiteral("viewport")).toMap();
    const QRect       source_rect = viewportSourceRect(viewport, source.size());
    if (source_rect.isEmpty())
        throw std::runtime_error("智能标注可视窗口区域无效");

    QSize input_size(optionInt(viewport, QStringLiteral("input_width"), source_rect.width()),
                     optionInt(viewport, QStringLiteral("input_height"), source_rect.height()));
    input_size.setWidth(std::max(1, input_size.width()));
    input_size.setHeight(std::max(1, input_size.height()));

    QImage viewport_image = source.copy(source_rect);
    if (viewport_image.size() != input_size)
        viewport_image = viewport_image.scaled(input_size, Qt::IgnoreAspectRatio, Qt::FastTransformation);

    auto temporary_file
        = std::make_unique<QTemporaryFile>(QDir::tempPath() + QStringLiteral("/dltool_smart_view_XXXXXX.png"));
    if (!temporary_file->open())
        throw std::runtime_error("创建智能标注可视窗口临时图像失败");
    if (!viewport_image.save(temporary_file.get(), "PNG"))
        throw std::runtime_error("保存智能标注可视窗口临时图像失败");
    temporary_file->close();

    input.path           = temporary_file->fileName();
    input.source_rect    = QRectF(source_rect);
    input.source_size    = source.size();
    input.input_size     = input_size;
    input.scale_x        = static_cast<double>(input_size.width()) / static_cast<double>(source_rect.width());
    input.scale_y        = static_cast<double>(input_size.height()) / static_cast<double>(source_rect.height());
    input.viewport_input = true;
    input.temporary_file = std::move(temporary_file);
    return input;
}

QPointF mapInputPointToSource(const QPointF &point, const InferenceImageInput &input)
{
    if (!input.viewport_input)
        return point;
    return QPointF(input.source_rect.x() + point.x() / input.scale_x,
                   input.source_rect.y() + point.y() / input.scale_y);
}

QRectF mapInputRectToSource(const QRectF &rect, const InferenceImageInput &input)
{
    if (!input.viewport_input)
        return rect;
    return QRectF(input.source_rect.x() + rect.x() / input.scale_x, input.source_rect.y() + rect.y() / input.scale_y,
                  rect.width() / input.scale_x, rect.height() / input.scale_y);
}

std::vector<QPointF> mapInputPolygonToSource(const std::vector<QPointF> &polygon, const InferenceImageInput &input)
{
    if (!input.viewport_input)
        return polygon;

    std::vector<QPointF> mapped;
    mapped.reserve(polygon.size());
    for (const QPointF &point : polygon) mapped.push_back(mapInputPointToSource(point, input));
    return mapped;
}

std::vector<PromptPoint> mapPromptsToInferenceInput(const std::vector<PromptPoint> &prompts,
                                                    const InferenceImageInput      &input)
{
    if (!input.viewport_input)
        return prompts;

    std::vector<PromptPoint> mapped;
    mapped.reserve(prompts.size());
    const QRectF source_rect = input.source_rect.adjusted(-0.5, -0.5, 0.5, 0.5);
    for (const PromptPoint &prompt : prompts)
    {
        if (!source_rect.contains(prompt.point))
            throw std::runtime_error("智能标注提示点不在当前可视窗口内");

        PromptPoint point;
        point.point = QPointF(std::clamp((prompt.point.x() - input.source_rect.x()) * input.scale_x, 0.0,
                                         static_cast<double>(input.input_size.width())),
                              std::clamp((prompt.point.y() - input.source_rect.y()) * input.scale_y, 0.0,
                                         static_cast<double>(input.input_size.height())));
        point.label = prompt.label;
        mapped.push_back(point);
    }
    return mapped;
}

/**
 * @brief 从二值 Mask 中计算外接矩形
 * @param mask 二值 Mask 数据
 * @param width 图像宽度
 * @param height 图像高度
 * @return 外接矩形，无前景时返回空矩形
 */
QRectF boundingBoxFromMask(const std::vector<uint8_t> &mask, int width, int height)
{
    int x_min = width, y_min = height, x_max = -1, y_max = -1;

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            if (mask[static_cast<size_t>(y) * width + x] == 0)
                continue;
            x_min = std::min(x_min, x);
            y_min = std::min(y_min, y);
            x_max = std::max(x_max, x);
            y_max = std::max(y_max, y);
        }
    }

    if (x_max < x_min || y_max < y_min)
        return {};
    return QRectF(x_min, y_min, x_max - x_min + 1, y_max - y_min + 1);
}

/**
 * @brief 计算多边形面积（绝对值）
 * @param points 多边形顶点
 * @return 面积
 */
double polygonArea(const std::vector<QPointF> &points)
{
    if (points.size() < 3)
        return 0.0;

    double area = 0.0;
    for (size_t i = 0; i < points.size(); ++i)
    {
        const QPointF &a = points[i];
        const QPointF &b = points[(i + 1) % points.size()];
        area += a.x() * b.y() - b.x() * a.y();
    }
    return std::abs(area) / 2.0;
}

/**
 * @brief 计算多边形有向面积
 * @param points 多边形顶点
 * @return 有向面积（正值表示逆时针）
 */
double signedPolygonArea(const std::vector<QPointF> &points)
{
    if (points.size() < 3)
        return 0.0;

    double area = 0.0;
    for (size_t i = 0; i < points.size(); ++i)
    {
        const QPointF &a = points[i];
        const QPointF &b = points[(i + 1) % points.size()];
        area += a.x() * b.y() - b.x() * a.y();
    }
    return area / 2.0;
}

/**
 * @brief 计算点到线段的距离
 * @param point 目标点
 * @param a 线段起点
 * @param b 线段终点
 * @return 最短距离
 */
double distanceToSegment(const QPointF &point, const QPointF &a, const QPointF &b)
{
    const double dx = b.x() - a.x(), dy = b.y() - a.y();
    const double len2 = dx * dx + dy * dy;
    if (len2 <= 0.000001)
        return QLineF(point, a).length();

    const double  t = std::clamp(((point.x() - a.x()) * dx + (point.y() - a.y()) * dy) / len2, 0.0, 1.0);
    const QPointF projection(a.x() + t * dx, a.y() + t * dy);
    return QLineF(point, projection).length();
}

/**
 * @brief 规范化多边形：去重、验证最小面积
 * @param points 原始多边形顶点
 * @return 规范化后的多边形，无效时返回空
 */
std::vector<QPointF> normalizePolygon(std::vector<QPointF> points)
{
    std::vector<QPointF> normalized;
    normalized.reserve(points.size());
    for (const QPointF &point : points)
    {
        if (!normalized.empty() && QLineF(normalized.back(), point).length() < 0.001)
            continue;
        normalized.push_back(point);
    }

    if (normalized.size() > 1 && QLineF(normalized.front(), normalized.back()).length() < 0.001)
        normalized.pop_back();

    if (normalized.size() < 3 || polygonArea(normalized) <= 0.5)
        return {};
    return normalized;
}

/**
 * @brief 移除共线点
 * @param points 原始多边形顶点
 * @param tolerance 共线判定容差
 * @return 移除共线点后的多边形
 */
std::vector<QPointF> removeCollinearPoints(const std::vector<QPointF> &points, double tolerance)
{
    if (points.size() < 4)
        return points;

    std::vector<QPointF> filtered;
    filtered.reserve(points.size());
    for (size_t i = 0; i < points.size(); ++i)
    {
        const QPointF &prev = points[(i + points.size() - 1) % points.size()];
        const QPointF &curr = points[i];
        const QPointF &next = points[(i + 1) % points.size()];
        if (distanceToSegment(curr, prev, next) <= tolerance)
            continue;
        filtered.push_back(curr);
    }
    return filtered.size() >= 3 ? filtered : points;
}

/**
 * @brief Douglas-Peucker 算法简化开放折线
 * @param points 原始折线顶点
 * @param epsilon 简化阈值
 * @return 简化后的折线
 */
std::vector<QPointF> simplifyOpenPolyline(const std::vector<QPointF> &points, double epsilon)
{
    if (points.size() <= 2)
        return points;

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
            continue;

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
        if (keep[i] != 0)
            simplified.push_back(points[i]);
    return simplified;
}

/**
 * @brief Douglas-Peucker 算法简化闭合多边形
 * @param points 原始多边形顶点
 * @param epsilon 简化阈值
 * @return 简化后的多边形
 */
std::vector<QPointF> simplifyClosedPolygon(const std::vector<QPointF> &points, double epsilon)
{
    if (points.size() < 6)
        return points;

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
        return points;

    std::vector<QPointF> first_chain(points.begin(), points.begin() + static_cast<std::ptrdiff_t>(split_index) + 1);
    std::vector<QPointF> second_chain(points.begin() + static_cast<std::ptrdiff_t>(split_index), points.end());
    second_chain.push_back(points.front());

    first_chain  = simplifyOpenPolyline(first_chain, epsilon);
    second_chain = simplifyOpenPolyline(second_chain, epsilon);

    std::vector<QPointF> simplified;
    simplified.reserve(first_chain.size() + second_chain.size());
    simplified.insert(simplified.end(), first_chain.begin(), first_chain.end());
    for (size_t i = 1; i + 1 < second_chain.size(); ++i) simplified.push_back(second_chain[i]);

    return normalizePolygon(simplified);
}

/**
 * @brief 将二值 Mask 转换为带 1 像素背景边框的 OpenCV Mat
 * @param mask 二值 Mask 数据
 * @param width 图像宽度
 * @param height 图像高度
 * @return CV_8UC1 Mask，前景为 255
 */
cv::Mat maskToPaddedMat(const std::vector<uint8_t> &mask, int width, int height)
{
    const int64_t total = static_cast<int64_t>(width) * static_cast<int64_t>(height);
    if (width <= 0 || height <= 0 || total <= 0 || mask.size() < static_cast<size_t>(total))
        return {};

    cv::Mat padded(height + 2, width + 2, CV_8UC1, cv::Scalar(0));
    for (int y = 0; y < height; ++y)
    {
        uchar       *dst        = padded.ptr<uchar>(y + 1) + 1;
        const size_t row_offset = static_cast<size_t>(y) * width;
        for (int x = 0; x < width; ++x)
            dst[x] = mask[row_offset + static_cast<size_t>(x)] != 0 ? uchar{255} : uchar{0};
    }
    return padded;
}

/**
 * @brief 计算签名距离场，Mask 内为正，Mask 外为负
 * @param padded_mask 带背景边框的 CV_8UC1 Mask
 * @return CV_32FC1 签名距离场
 */
cv::Mat signedDistanceField(const cv::Mat &padded_mask)
{
    cv::Mat inside_distance;
    cv::distanceTransform(padded_mask, inside_distance, cv::DIST_L2, cv::DIST_MASK_PRECISE);

    cv::Mat inverted_mask;
    cv::bitwise_not(padded_mask, inverted_mask);

    cv::Mat outside_distance;
    cv::distanceTransform(inverted_mask, outside_distance, cv::DIST_L2, cv::DIST_MASK_PRECISE);

    cv::Mat signed_distance;
    cv::subtract(inside_distance, outside_distance, signed_distance);
    return signed_distance;
}

/**
 * @brief 用距离场将 findContours 的整数边界点修正到亚像素边界
 * @param signed_distance 签名距离场
 * @param point findContours 输出点
 * @param width 原始 Mask 宽度
 * @param height 原始 Mask 高度
 * @return 原始 Mask 坐标系中的亚像素点
 */
QPointF refineContourPoint(const cv::Mat &signed_distance, const cv::Point &point, int width, int height)
{
    const float inside_value = signed_distance.at<float>(point.y, point.x);
    cv::Point   best_outside = point;
    float       best_value   = 0.0F;
    bool        found        = false;

    for (int dy = -1; dy <= 1; ++dy)
    {
        const int y = point.y + dy;
        if (y < 0 || y >= signed_distance.rows)
            continue;
        for (int dx = -1; dx <= 1; ++dx)
        {
            if (dx == 0 && dy == 0)
                continue;
            const int x = point.x + dx;
            if (x < 0 || x >= signed_distance.cols)
                continue;

            const float value = signed_distance.at<float>(y, x);
            if (value <= 0.0F && (!found || value > best_value))
            {
                found        = true;
                best_value   = value;
                best_outside = cv::Point(x, y);
            }
        }
    }

    double x = static_cast<double>(point.x);
    double y = static_cast<double>(point.y);
    if (found && inside_value > best_value)
    {
        const double t = static_cast<double>(inside_value) / static_cast<double>(inside_value - best_value);
        x += t * static_cast<double>(best_outside.x - point.x);
        y += t * static_cast<double>(best_outside.y - point.y);
    }
    else
    {
        const int left   = std::max(point.x - 1, 0);
        const int right  = std::min(point.x + 1, signed_distance.cols - 1);
        const int top    = std::max(point.y - 1, 0);
        const int bottom = std::min(point.y + 1, signed_distance.rows - 1);
        const double dx  = 0.5
                         * static_cast<double>(signed_distance.at<float>(point.y, right)
                                               - signed_distance.at<float>(point.y, left));
        const double dy  = 0.5
                         * static_cast<double>(signed_distance.at<float>(bottom, point.x)
                                               - signed_distance.at<float>(top, point.x));
        const double denom = dx * dx + dy * dy;
        if (denom > 0.000001)
        {
            x -= static_cast<double>(inside_value) * dx / denom;
            y -= static_cast<double>(inside_value) * dy / denom;
        }
    }

    return QPointF(std::clamp(x - 0.5, 0.0, static_cast<double>(width)),
                   std::clamp(y - 0.5, 0.0, static_cast<double>(height)));
}

std::vector<QPointF> contourToPolygon(const std::vector<cv::Point> &contour, const cv::Mat &signed_distance, int width,
                                      int height)
{
    std::vector<QPointF> points;
    points.reserve(contour.size());
    for (const cv::Point &point : contour) points.push_back(refineContourPoint(signed_distance, point, width, height));

    points = normalizePolygon(std::move(points));
    if (!points.empty() && signedPolygonArea(points) < 0.0)
        std::reverse(points.begin(), points.end());
    return points;
}

/**
 * @brief 将二值 Mask 转换为最大外轮廓多边形（距离场 + OpenCV findContours）
 * @param mask 二值 Mask 数据
 * @param width 图像宽度
 * @param height 图像高度
 * @return 最大多边形轮廓列表，最多包含一个外轮廓
 */
std::vector<std::vector<QPointF>> maskToPolygons(const std::vector<uint8_t> &mask, int width, int height)
{
    const cv::Mat padded_mask = maskToPaddedMat(mask, width, height);
    if (padded_mask.empty())
        return {};

    const cv::Mat signed_distance = signedDistanceField(padded_mask);
    cv::Mat       foreground;
    cv::compare(signed_distance, 0.0F, foreground, cv::CMP_GT);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(foreground, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    if (contours.empty())
        return {};

    const auto best_it = std::max_element(contours.begin(), contours.end(),
                                          [](const std::vector<cv::Point> &left,
                                             const std::vector<cv::Point> &right)
                                          { return std::abs(cv::contourArea(left)) < std::abs(cv::contourArea(right)); });
    if (best_it == contours.end() || std::abs(cv::contourArea(*best_it)) <= 0.5)
        return {};

    std::vector<QPointF> points = contourToPolygon(*best_it, signed_distance, width, height);
    if (points.empty())
        return {};

    points = removeCollinearPoints(points, 0.001);
    points = normalizePolygon(std::move(points));
    if (points.empty())
        return {};
    if (signedPolygonArea(points) < 0.0)
        std::reverse(points.begin(), points.end());

    const double epsilon = std::clamp(std::sqrt(polygonArea(points)) * 0.01, 1.0, 3.0);
    points               = simplifyClosedPolygon(points, epsilon);
    points               = removeCollinearPoints(points, 0.01);
    points               = normalizePolygon(std::move(points));
    if (points.empty())
        return {};
    if (signedPolygonArea(points) < 0.0)
        std::reverse(points.begin(), points.end());

    std::vector<std::vector<QPointF>> polygons;
    polygons.push_back(std::move(points));
    return polygons;
}

/**
 * @brief 将多边形顶点转换为 QVariantList
 * @param points 多边形顶点
 * @return QVariantList 格式的顶点数据
 */
QVariantList pointsToVariantList(const std::vector<QPointF> &points)
{
    QVariantList result;
    result.reserve(static_cast<int>(points.size()));
    for (const QPointF &point : points)
        result.push_back(QVariantMap{
            {QStringLiteral("x"), point.x()},
            {QStringLiteral("y"), point.y()}
        });
    return result;
}

/**
 * @brief 将 Mask 数据转换为行程编码格式（RLE）
 * @param mask 二值 Mask 数据
 * @param width 图像宽度
 * @param height 图像高度
 * @return RLE 格式的 QVariantList
 */
QVariantMap maskRunToVariantMap(int x, int y, int width, const InferenceImageInput &input)
{
    if (!input.viewport_input)
        return QVariantMap{
            {    QStringLiteral("x"),     x},
            {    QStringLiteral("y"),     y},
            {QStringLiteral("width"), width}
        };

    return QVariantMap{
        {     QStringLiteral("x"), input.source_rect.x() + static_cast<double>(x) / input.scale_x},
        {     QStringLiteral("y"), input.source_rect.y() + static_cast<double>(y) / input.scale_y},
        { QStringLiteral("width"),                     static_cast<double>(width) / input.scale_x},
        {QStringLiteral("height"),                                            1.0 / input.scale_y}
    };
}

QVariantList maskRunsToVariantList(const std::vector<uint8_t> &mask, int width, int height,
                                   const InferenceImageInput &input)
{
    QVariantList result;
    for (int y = 0; y < height; ++y)
    {
        const size_t row_offset = static_cast<size_t>(y) * width;
        int          x          = 0;
        while (x < width)
        {
            while (x < width && mask[row_offset + static_cast<size_t>(x)] == 0) ++x;
            const int run_start = x;
            while (x < width && mask[row_offset + static_cast<size_t>(x)] != 0) ++x;
            const int run_width = x - run_start;
            if (run_width > 0)
                result.push_back(maskRunToVariantMap(run_start, y, run_width, input));
        }
    }
    return result;
}

/**
 * @brief 将矩形转换为四个顶点
 * @param rect 矩形
 * @return 矩形顶点列表（左上、右上、右下、左下）
 */
std::vector<QPointF> rectanglePoints(const QRectF &rect)
{
    return {QPointF(rect.left(), rect.top()), QPointF(rect.right(), rect.top()), QPointF(rect.right(), rect.bottom()),
            QPointF(rect.left(), rect.bottom())};
}

/**
 * @brief 对最终多边形进行简化和规范化
 * @param polygon 原始多边形
 * @param epsilon 简化阈值
 * @return 简化后的多边形，简化失败则返回原始多边形
 */
std::vector<QPointF> simplifyFinalPolygon(std::vector<QPointF> polygon, double epsilon)
{
    polygon = normalizePolygon(std::move(polygon));
    if (polygon.empty() || epsilon <= 0.0)
        return polygon;

    std::vector<QPointF> simplified = simplifyClosedPolygon(polygon, epsilon);
    simplified                      = removeCollinearPoints(simplified, epsilon);
    simplified                      = normalizePolygon(std::move(simplified));
    return simplified.empty() ? polygon : simplified;
}

std::vector<uint8_t> selectedBinaryMask(const irt::features::SAMImagePrediction &prediction, int mask_index)
{
    if (prediction.width <= 0 || prediction.height <= 0 || prediction.mask_count <= 0)
        throw std::runtime_error("SAM 没有生成有效 mask");
    if (mask_index < 0 || mask_index >= prediction.mask_count)
        throw std::runtime_error("SAM mask 索引无效");

    const size_t single_mask_size = static_cast<size_t>(prediction.width) * prediction.height;
    const size_t mask_offset      = static_cast<size_t>(mask_index) * single_mask_size;
    const size_t expected_size    = single_mask_size * static_cast<size_t>(prediction.mask_count);

    if (!prediction.binary_masks.empty())
    {
        if (prediction.binary_masks.size() < expected_size)
            throw std::runtime_error("SAM binary mask 输出尺寸不匹配");
        return {prediction.binary_masks.begin() + static_cast<std::ptrdiff_t>(mask_offset),
                prediction.binary_masks.begin() + static_cast<std::ptrdiff_t>(mask_offset + single_mask_size)};
    }

    if (prediction.masks.size() < expected_size)
        throw std::runtime_error("SAM mask 输出尺寸不匹配");

    std::vector<uint8_t> mask(single_mask_size, 0);
    for (size_t i = 0; i < single_mask_size; ++i)
        mask[i] = prediction.masks[mask_offset + i] > 0.0F ? uint8_t{1} : uint8_t{0};
    return mask;
}

} // namespace

SmartAnnotationController::SmartAnnotationController(QObject *parent)
    : QObject(parent)
{
    auto *gs = dltool::settings::GlobalSettings::getInstance();
    enabled_ = gs->valueForField(dltool::settings::generated::field::SmartAnnotation::Key::Enabled, false).toBool();

    connect(gs->catalog(), &dltool::settings::SettingsCatalog::fieldValueChanged, this,
            [this](const QString &group_key, const QString &name, const QVariant &value)
            {
                if (group_key == QStringLiteral("SmartAnnotationSettings") && name == QStringLiteral("enabled"))
                {
                    const bool v = value.toBool();
                    if (v != enabled_)
                    {
                        enabled_ = v;
                        emit enabledChanged();
                    }
                }
            });
}

SmartAnnotationController::~SmartAnnotationController() = default;

/// 清除模型缓存并重置状态
void SmartAnnotationController::clearCache()
{
    predictor_.reset();
    cached_model_key_.clear();
    loading_model_key_.clear();
    setLoadingModel(false);
    setRunning(false);
}

/**
 * @brief 启动异步模型加载
 * @param model_name 模型名称
 * @param model_path 模型文件路径
 * @param backend 推理后端
 * @param device 推理设备
 */
void SmartAnnotationController::startAsyncModelLoad(const QString &model_name, const QString &model_path,
                                                    const irt::model::ModelBackend backend,
                                                    const irt::model::ModelDevice  device)
{
    const SmartModelLoadRequest request = buildSmartModelLoadRequest(model_name, model_path, backend, device);
    if (loading_model_ && loading_model_key_ == request.key)
        return;

    loading_model_key_ = request.key;
    setLastError(QString());
    setLoadingModel(true);
    setRunning(true);

    QPointer<SmartAnnotationController> controller(this);
    QThread                            *work_thread = QThread::create(
        [controller, request]()
        {
            auto    predictor_holder = std::make_shared<std::unique_ptr<irt::features::SAMImagePredictor>>();
            QString error;
            bool    success = false;

            try
            {
                *predictor_holder = loadSmartPredictor(request);
                success           = true;
            }
            catch (const std::exception &e)
            {
                error = QString::fromUtf8(e.what());
                spdlog::error("加载智能标注模型失败: {}", error.toUtf8().constData());
            }
            catch (...)
            {
                error = QStringLiteral("Unknown smart annotation model load error");
                spdlog::error("加载智能标注模型失败: {}", error.toUtf8().constData());
            }

            if (!controller)
                return;

            QMetaObject::invokeMethod(
                controller.data(),
                [controller, request, predictor_holder, error, success]() mutable
                {
                    if (!controller)
                        return;
                    if (controller->loading_model_key_ != request.key)
                        return;

                    controller->loading_model_key_.clear();
                    if (success)
                    {
                        controller->predictor_        = std::move(*predictor_holder);
                        controller->cached_model_key_ = request.key;
                        controller->setLastError(QString());
                    }
                    else
                    {
                        controller->predictor_.reset();
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

/**
 * @brief 执行智能标注推理
 * @param image_path 输入图像路径
 * @param prompt_points 提示点列表
 * @return 包含推理结果的 QVariantMap
 */
QVariantMap SmartAnnotationController::infer(const QString &image_path, const QVariantList &prompt_points,
                                             const QVariantMap &options)
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
        namespace generated_field = dltool::settings::generated::field;

        auto *settings = dltool::settings::GlobalSettings::getInstance();
        if (settings == nullptr
            || settings->settingsGroup(dltool::settings::generated::AccessorKey::SmartAnnotation) == nullptr
            || !settingBool(settings, generated_field::SmartAnnotation::Enabled, false))
            throw std::runtime_error("智能标注未启用");

        const std::vector<PromptPoint> prompts = parsePromptPoints(prompt_points);

        const QString model_name
            = normalizedModelName(settingString(settings, generated_field::SmartAnnotation::Model));
        const auto backend = static_cast<irt::model::ModelBackend>(
            settings->valueForField(generated_field::SmartAnnotation::ModelBackend).toInt());
        const auto device = static_cast<irt::model::ModelDevice>(
            settings->valueForField(generated_field::SmartAnnotation::ModelDevice).toInt());
        QString model_path = settingString(settings, generated_field::SmartAnnotation::ModelPath);
        if (model_name.isEmpty())
            throw std::runtime_error("智能标注模型未配置");
        if (model_path.isEmpty())
            throw std::runtime_error("智能标注模型文件未配置");

        const QFileInfo model_info(model_path);
        if (!model_info.isFile())
            throw std::runtime_error(QString("智能标注模型文件不存在: %1").arg(model_path).toStdString());

        const SmartModelLoadRequest request = buildSmartModelLoadRequest(model_name, model_path, backend, device);
        if (predictor_ == nullptr || !predictor_->isReady() || cached_model_key_ != request.key)
        {
            startAsyncModelLoad(model_name, model_path, backend, device);
            result[QStringLiteral("loading")] = true;
            return result;
        }

        setRunning(true);

        const InferenceImageInput               image_input   = prepareInferenceImageInput(image_path, options);
        const std::vector<PromptPoint>          input_prompts = mapPromptsToInferenceInput(prompts, image_input);
        const irt::features::SAMImagePrediction prediction    = predictor_->predict(
            toFilesystemPath(image_input.path), buildImagePrompt(input_prompts), buildPredictOptions(settings));
        const int   mask_index = 0;
        const float selected_iou
            = (mask_index >= 0 && static_cast<size_t>(mask_index) < prediction.iou_predictions.size())
                ? prediction.iou_predictions[mask_index]
                : 0.0F;
        const int input_image_width  = prediction.width;
        const int input_image_height = prediction.height;
        const int image_width        = image_input.viewport_input ? image_input.source_size.width() : input_image_width;
        const int image_height = image_input.viewport_input ? image_input.source_size.height() : input_image_height;

        std::vector<uint8_t> binary_mask = selectedBinaryMask(prediction, mask_index);
        const int foreground_pixels = static_cast<int>(std::count(binary_mask.begin(), binary_mask.end(), uint8_t{1}));

        const QRectF input_bbox = boundingBoxFromMask(binary_mask, input_image_width, input_image_height);
        if (input_bbox.isEmpty())
            throw std::runtime_error("SAM 没有生成有效 mask，请调整提示点或阈值");

        std::vector<QPointF>              polygon;
        std::vector<std::vector<QPointF>> polygons = maskToPolygons(binary_mask, input_image_width, input_image_height);
        if (!polygons.empty())
            polygon = std::move(polygons.front());
        else
            polygon = rectanglePoints(input_bbox);

        const QRectF         bbox           = mapInputRectToSource(input_bbox, image_input);
        std::vector<QPointF> output_polygon = mapInputPolygonToSource(polygon, image_input);
        output_polygon                      = simplifyFinalPolygon(
            std::move(output_polygon),
            settingDouble(settings, generated_field::SmartAnnotation::PolygonSimplifyEpsilon, 2.0));
        if (output_polygon.size() < 3)
            output_polygon = rectanglePoints(bbox);

        const QVariantList mask_runs
            = maskRunsToVariantList(binary_mask, input_image_width, input_image_height, image_input);

        result[QStringLiteral("success")]          = true;
        result[QStringLiteral("error")]            = QString();
        result[QStringLiteral("model_name")]       = model_name;
        result[QStringLiteral("model_path")]       = model_info.absoluteFilePath();
        result[QStringLiteral("backend")]          = QString::fromLatin1(irt::model::modelBackendName(backend));
        result[QStringLiteral("device")]           = QString::fromLatin1(irt::model::modelDeviceName(device));
        result[QStringLiteral("image_path")]       = image_path;
        result[QStringLiteral("image_width")]      = image_width;
        result[QStringLiteral("image_height")]     = image_height;
        result[QStringLiteral("x")]                = bbox.x();
        result[QStringLiteral("y")]                = bbox.y();
        result[QStringLiteral("width")]            = bbox.width();
        result[QStringLiteral("height")]           = bbox.height();
        result[QStringLiteral("points")]           = pointsToVariantList(output_polygon);
        result[QStringLiteral("point_count")]      = static_cast<int>(output_polygon.size());
        result[QStringLiteral("prompt_count")]     = static_cast<int>(prompts.size());
        result[QStringLiteral("mask_index")]       = mask_index;
        result[QStringLiteral("iou")]              = selected_iou;
        result[QStringLiteral("mask_pixel_count")] = foreground_pixels;
        result[QStringLiteral("mask_width")]       = image_width;
        result[QStringLiteral("mask_height")]      = image_height;
        result[QStringLiteral("mask_runs")]        = mask_runs;

        setLastError(QString());
    }
    catch (const std::exception &e)
    {
        const QString error               = QString::fromUtf8(e.what());
        result[QStringLiteral("success")] = false;
        result[QStringLiteral("error")]   = error;
        setLastError(error);
        spdlog::error("智能标注失败: {}", error.toUtf8().constData());
    }
    catch (...)
    {
        const QString error               = QString("未知智能标注错误");
        result[QStringLiteral("success")] = false;
        result[QStringLiteral("error")]   = error;
        setLastError(error);
        spdlog::error("智能标注失败: {}", error.toUtf8().constData());
    }

    setRunning(false);
    return result;
}

void SmartAnnotationController::setRunning(bool running)
{
    if (running_ == running)
        return;
    running_ = running;
    emit runningChanged();
}

void SmartAnnotationController::setLoadingModel(bool loading_model)
{
    if (loading_model_ == loading_model)
        return;
    loading_model_ = loading_model;
    emit loadingModelChanged();
}

void SmartAnnotationController::setLastError(const QString &last_error)
{
    if (last_error_ == last_error)
        return;
    last_error_ = last_error;
    emit lastErrorChanged();
}

} // namespace dltool::feature
