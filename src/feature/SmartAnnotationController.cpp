#include "feature/SmartAnnotationController.h"

#include "settings/GlobalSettings.h"
#include "settings/SettingsValue.h"

#include <inferrt/features/SAMImagePredictor.hpp>
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
#include <QThread>
#include <QTemporaryFile>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dltool::feature {

namespace {

constexpr int kSamMaxPoints = 16; ///< SAM 模型最大提示点数

using dltool::settings::settingBool;
using dltool::settings::settingDouble;
using dltool::settings::settingString;

/// 提示点结构
struct PromptPoint
{
    QPointF point;   ///< 坐标
    int     label{1}; ///< 标签（1=前景，0=背景）
};

struct InferenceImageInput
{
    QString                         path;                 ///< 实际送入 SAM 的图像路径
    QRectF                          source_rect;          ///< 输入图像对应的原图区域
    QSize                           source_size;          ///< 原图尺寸
    QSize                           input_size;           ///< 输入图像尺寸
    double                          scale_x{1.0};         ///< 原图到输入图像的 X 缩放
    double                          scale_y{1.0};         ///< 原图到输入图像的 Y 缩放
    bool                            viewport_input{false}; ///< 是否使用可视窗口输入
    std::unique_ptr<QTemporaryFile> temporary_file;        ///< 临时输入图文件
};

/// Mask 像素坐标点
struct MaskPoint
{
    int x{0}; ///< X 坐标
    int y{0}; ///< Y 坐标
};

bool operator==(const MaskPoint &left, const MaskPoint &right)
{
    return left.x == right.x && left.y == right.y;
}

/// Mask 边界边
struct BoundaryEdge
{
    MaskPoint from;       ///< 起点
    MaskPoint to;         ///< 终点
    int       direction{0}; ///< 方向（0=右, 1=下, 2=左, 3=上）
    bool      used{false};  ///< 是否已被追踪使用
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
    QString                  model_name;          ///< 模型名称
    irt::model::ModelBackend backend{irt::model::ModelBackend::TensorRT}; ///< 推理后端
    irt::model::ModelDevice  device{irt::model::ModelDevice::GPU};        ///< 推理设备
    QString                  absolute_model_path; ///< 模型文件绝对路径
    QString                  key;                 ///< 缓存唯一标识
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
        if (map.isEmpty()) continue;

        PromptPoint point;
        point.point = QPointF(map.value(QStringLiteral("x")).toDouble(), map.value(QStringLiteral("y")).toDouble());
        point.label = map.value(QStringLiteral("label"), 1).toInt() > 0 ? 1 : 0;
        if (point.label == 1) ++positive_count;
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
        prompt.points.push_back(
            irt::features::SAMPromptPoint{static_cast<float>(point.point.x()), static_cast<float>(point.point.y()),
                                          point.label});
    }
    return prompt;
}

irt::features::SAMImagePredictOptions buildPredictOptions(dltool::settings::GlobalSettings *settings)
{
    namespace generated_field = dltool::settings::generated::field;

    irt::features::SAMImagePredictOptions options;
    options.return_logits  = false;
    options.mask_threshold = static_cast<float>(
        settingDouble(settings, generated_field::SmartAnnotation::MaskThreshold, 0.0));
    return options;
}

bool optionBool(const QVariantMap &options, const QString &key, bool fallback)
{
    if (!options.contains(key)) return fallback;
    return options.value(key).toBool();
}

double optionDouble(const QVariantMap &options, const QString &key, double fallback)
{
    bool ok    = false;
    const auto value = options.value(key).toDouble(&ok);
    return ok && std::isfinite(value) ? value : fallback;
}

int optionInt(const QVariantMap &options, const QString &key, int fallback)
{
    bool ok    = false;
    const int value = options.value(key).toInt(&ok);
    return ok ? value : fallback;
}

QRect viewportSourceRect(const QVariantMap &viewport, const QSize &source_size)
{
    const int x      = static_cast<int>(std::floor(optionDouble(viewport, QStringLiteral("x"), 0.0)));
    const int y      = static_cast<int>(std::floor(optionDouble(viewport, QStringLiteral("y"), 0.0)));
    const int width  = static_cast<int>(std::ceil(optionDouble(viewport, QStringLiteral("width"), 0.0)));
    const int height = static_cast<int>(std::ceil(optionDouble(viewport, QStringLiteral("height"), 0.0)));

    if (width <= 0 || height <= 0) return {};
    return QRect(x, y, width, height).intersected(QRect(QPoint(0, 0), source_size));
}

InferenceImageInput prepareInferenceImageInput(const QString &image_path, const QVariantMap &options)
{
    InferenceImageInput input;
    input.path = image_path;

    if (!optionBool(options, QStringLiteral("use_viewport_input"), false)) return input;

    QImage source(image_path);
    if (source.isNull())
        throw std::runtime_error(QString("读取可视窗口输入图像失败: %1").arg(image_path).toStdString());

    const QVariantMap viewport = options.value(QStringLiteral("viewport")).toMap();
    const QRect       source_rect = viewportSourceRect(viewport, source.size());
    if (source_rect.isEmpty()) throw std::runtime_error("智能标注可视窗口区域无效");

    QSize input_size(optionInt(viewport, QStringLiteral("input_width"), source_rect.width()),
                     optionInt(viewport, QStringLiteral("input_height"), source_rect.height()));
    input_size.setWidth(std::max(1, input_size.width()));
    input_size.setHeight(std::max(1, input_size.height()));

    QImage viewport_image = source.copy(source_rect);
    if (viewport_image.size() != input_size)
        viewport_image = viewport_image.scaled(input_size, Qt::IgnoreAspectRatio, Qt::FastTransformation);

    auto temporary_file
        = std::make_unique<QTemporaryFile>(QDir::tempPath() + QStringLiteral("/dltool_smart_view_XXXXXX.png"));
    if (!temporary_file->open()) throw std::runtime_error("创建智能标注可视窗口临时图像失败");
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
    if (!input.viewport_input) return point;
    return QPointF(input.source_rect.x() + point.x() / input.scale_x,
                   input.source_rect.y() + point.y() / input.scale_y);
}

QRectF mapInputRectToSource(const QRectF &rect, const InferenceImageInput &input)
{
    if (!input.viewport_input) return rect;
    return QRectF(input.source_rect.x() + rect.x() / input.scale_x,
                  input.source_rect.y() + rect.y() / input.scale_y,
                  rect.width() / input.scale_x,
                  rect.height() / input.scale_y);
}

std::vector<QPointF> mapInputPolygonToSource(const std::vector<QPointF> &polygon, const InferenceImageInput &input)
{
    if (!input.viewport_input) return polygon;

    std::vector<QPointF> mapped;
    mapped.reserve(polygon.size());
    for (const QPointF &point : polygon) mapped.push_back(mapInputPointToSource(point, input));
    return mapped;
}

std::vector<PromptPoint> mapPromptsToInferenceInput(const std::vector<PromptPoint> &prompts,
                                                    const InferenceImageInput        &input)
{
    if (!input.viewport_input) return prompts;

    std::vector<PromptPoint> mapped;
    mapped.reserve(prompts.size());
    const QRectF source_rect = input.source_rect.adjusted(-0.5, -0.5, 0.5, 0.5);
    for (const PromptPoint &prompt : prompts)
    {
        if (!source_rect.contains(prompt.point))
            throw std::runtime_error("智能标注提示点不在当前可视窗口内");

        PromptPoint point;
        point.point = QPointF(std::clamp((prompt.point.x() - input.source_rect.x()) * input.scale_x,
                                         0.0, static_cast<double>(input.input_size.width())),
                              std::clamp((prompt.point.y() - input.source_rect.y()) * input.scale_y,
                                         0.0, static_cast<double>(input.input_size.height())));
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
            if (mask[static_cast<size_t>(y) * width + x] == 0) continue;
            x_min = std::min(x_min, x);
            y_min = std::min(y_min, y);
            x_max = std::max(x_max, x);
            y_max = std::max(y_max, y);
        }
    }

    if (x_max < x_min || y_max < y_min) return {};
    return QRectF(x_min, y_min, x_max - x_min + 1, y_max - y_min + 1);
}

/**
 * @brief 计算多边形面积（绝对值）
 * @param points 多边形顶点
 * @return 面积
 */
double polygonArea(const std::vector<QPointF> &points)
{
    if (points.size() < 3) return 0.0;

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
    if (points.size() < 3) return 0.0;

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
    if (len2 <= 0.000001) return QLineF(point, a).length();

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
        if (!normalized.empty() && QLineF(normalized.back(), point).length() < 0.001) continue;
        normalized.push_back(point);
    }

    if (normalized.size() > 1 && QLineF(normalized.front(), normalized.back()).length() < 0.001)
        normalized.pop_back();

    if (normalized.size() < 3 || polygonArea(normalized) <= 0.5) return {};
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
    if (points.size() < 4) return points;

    std::vector<QPointF> filtered;
    filtered.reserve(points.size());
    for (size_t i = 0; i < points.size(); ++i)
    {
        const QPointF &prev = points[(i + points.size() - 1) % points.size()];
        const QPointF &curr = points[i];
        const QPointF &next = points[(i + 1) % points.size()];
        if (distanceToSegment(curr, prev, next) <= tolerance) continue;
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
    if (points.size() <= 2) return points;

    std::vector<uint8_t> keep(points.size(), 0);
    keep.front() = 1;
    keep.back()  = 1;

    std::vector<std::pair<size_t, size_t>> ranges;
    ranges.emplace_back(0, points.size() - 1);

    while (!ranges.empty())
    {
        const auto [first, last] = ranges.back();
        ranges.pop_back();
        if (last <= first + 1) continue;

        double max_distance = 0.0;
        size_t max_index    = first;
        for (size_t i = first + 1; i < last; ++i)
        {
            const double distance = distanceToSegment(points[i], points[first], points[last]);
            if (distance > max_distance) { max_distance = distance; max_index = i; }
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
        if (keep[i] != 0) simplified.push_back(points[i]);
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
    if (points.size() < 6) return points;

    size_t split_index  = 1;
    double max_distance = 0.0;
    for (size_t i = 1; i < points.size(); ++i)
    {
        const double distance = QLineF(points.front(), points[i]).length();
        if (distance > max_distance) { max_distance = distance; split_index = i; }
    }

    if (split_index == 0 || split_index >= points.size()) return points;

    std::vector<QPointF> first_chain(points.begin(), points.begin() + static_cast<std::ptrdiff_t>(split_index) + 1);
    std::vector<QPointF> second_chain(points.begin() + static_cast<std::ptrdiff_t>(split_index), points.end());
    second_chain.push_back(points.front());

    first_chain  = simplifyOpenPolyline(first_chain, epsilon);
    second_chain = simplifyOpenPolyline(second_chain, epsilon);

    std::vector<QPointF> simplified;
    simplified.reserve(first_chain.size() + second_chain.size());
    simplified.insert(simplified.end(), first_chain.begin(), first_chain.end());
    for (size_t i = 1; i + 1 < second_chain.size(); ++i)
        simplified.push_back(second_chain[i]);

    return normalizePolygon(simplified);
}

/**
 * @brief 计算边界边的方向
 * @param from 起点
 * @param to 终点
 * @return 方向码（0=右, 1=下, 2=左, 3=上）
 */
int edgeDirection(const MaskPoint &from, const MaskPoint &to)
{
    if (to.x > from.x) return 0;
    if (to.y > from.y) return 1;
    if (to.x < from.x) return 2;
    return 3;
}

/**
 * @brief 计算 MaskPoint 的唯一键值
 * @param point 坐标点
 * @param width 图像宽度
 * @return 唯一键值
 */
int64_t maskPointKey(const MaskPoint &point, int width)
{
    return static_cast<int64_t>(point.y) * static_cast<int64_t>(width + 1) + point.x;
}

/**
 * @brief 计算转向优先级（用于边界追踪的最右转规则）
 * @param previous_direction 之前的方向
 * @param next_direction 候选方向
 * @return 优先级（数值越大越优先）
 */
int turnPriority(int previous_direction, int next_direction)
{
    const int turn = (next_direction - previous_direction + 4) % 4;
    if (turn == 1) return 3; // 右转
    if (turn == 0) return 2; // 直行
    if (turn == 3) return 1; // 左转
    return 0;                // 掉头
}

/**
 * @brief 选择下一个要追踪的边界边
 * @param outgoing_edges 出发边索引表
 * @param edges 所有边界边
 * @param point 当前点
 * @param width 图像宽度
 * @param previous_direction 上一个方向
 * @return 选中的边索引，无有效边时返回 max
 */
size_t chooseNextBoundaryEdge(const std::unordered_map<int64_t, std::vector<size_t>> &outgoing_edges,
                              const std::vector<BoundaryEdge> &edges, const MaskPoint &point, int width,
                              int previous_direction)
{
    const auto outgoing_it = outgoing_edges.find(maskPointKey(point, width));
    if (outgoing_it == outgoing_edges.end()) return std::numeric_limits<size_t>::max();

    size_t best_index    = std::numeric_limits<size_t>::max();
    int    best_priority = -1;
    for (const size_t edge_index : outgoing_it->second)
    {
        const BoundaryEdge &edge = edges[edge_index];
        if (edge.used) continue;
        const int priority = turnPriority(previous_direction, edge.direction);
        if (priority > best_priority) { best_priority = priority; best_index = edge_index; }
    }
    return best_index;
}

/**
 * @brief 将二值 Mask 转换为多边形轮廓（Moore 边界追踪）
 * @param mask 二值 Mask 数据
 * @param width 图像宽度
 * @param height 图像高度
 * @return 多边形轮廓列表（按面积降序排列）
 */
std::vector<std::vector<QPointF>> maskToPolygons(const std::vector<uint8_t> &mask, int width, int height)
{
    const int64_t total = static_cast<int64_t>(width) * static_cast<int64_t>(height);
    if (width <= 0 || height <= 0 || total <= 0 || mask.size() < static_cast<size_t>(total)) return {};

    std::vector<BoundaryEdge>                        edges;
    std::unordered_map<int64_t, std::vector<size_t>> outgoing_edges;

    const auto is_foreground = [&](int x, int y) -> bool {
        return x >= 0 && y >= 0 && x < width && y < height && mask[static_cast<size_t>(y * width + x)] != 0;
    };

    const auto add_edge = [&](const MaskPoint &from, const MaskPoint &to) {
        const size_t edge_index = edges.size();
        edges.push_back(BoundaryEdge{from, to, edgeDirection(from, to), false});
        outgoing_edges[maskPointKey(from, width)].push_back(edge_index);
    };

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            if (!is_foreground(x, y)) continue;
            if (!is_foreground(x, y - 1))   add_edge(MaskPoint{x, y}, MaskPoint{x + 1, y});
            if (!is_foreground(x + 1, y))   add_edge(MaskPoint{x + 1, y}, MaskPoint{x + 1, y + 1});
            if (!is_foreground(x, y + 1))   add_edge(MaskPoint{x + 1, y + 1}, MaskPoint{x, y + 1});
            if (!is_foreground(x - 1, y))   add_edge(MaskPoint{x, y + 1}, MaskPoint{x, y});
        }
    }

    if (edges.empty()) return {};

    std::vector<std::vector<QPointF>> polygons;
    polygons.reserve(8);

    for (size_t start_edge_index = 0; start_edge_index < edges.size(); ++start_edge_index)
    {
        if (edges[start_edge_index].used) continue;

        const MaskPoint        start_point = edges[start_edge_index].from;
        size_t                 edge_index  = start_edge_index;
        bool                   closed      = false;
        std::vector<MaskPoint> loop;
        loop.reserve(256);

        for (size_t guard = 0; edge_index != std::numeric_limits<size_t>::max() && guard <= edges.size(); ++guard)
        {
            BoundaryEdge &edge = edges[edge_index];
            if (edge.used) break;

            edge.used = true;
            if (loop.empty()) loop.push_back(edge.from);
            loop.push_back(edge.to);

            if (edge.to == start_point) { closed = true; break; }
            edge_index = chooseNextBoundaryEdge(outgoing_edges, edges, edge.to, width, edge.direction);
        }

        if (!closed || loop.size() < 4) continue;
        if (loop.back() == loop.front()) loop.pop_back();

        std::vector<QPointF> points;
        points.reserve(loop.size());
        for (const MaskPoint &point : loop) points.emplace_back(point.x, point.y);

        points = normalizePolygon(std::move(points));
        if (points.empty() || signedPolygonArea(points) <= 0.5) continue;

        points = removeCollinearPoints(points, 0.001);
        points = normalizePolygon(std::move(points));
        if (points.empty() || signedPolygonArea(points) <= 0.5) continue;

        const double epsilon = std::clamp(std::sqrt(polygonArea(points)) * 0.01, 1.0, 3.0);
        points = simplifyClosedPolygon(points, epsilon);
        points = removeCollinearPoints(points, 0.01);
        points = normalizePolygon(std::move(points));
        if (!points.empty() && signedPolygonArea(points) > 0.5)
            polygons.push_back(std::move(points));
    }

    std::sort(polygons.begin(), polygons.end(), [](const std::vector<QPointF> &left, const std::vector<QPointF> &right) {
        return polygonArea(left) > polygonArea(right);
    });
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
        result.push_back(QVariantMap{{QStringLiteral("x"), point.x()}, {QStringLiteral("y"), point.y()}});
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
        return QVariantMap{{QStringLiteral("x"), x}, {QStringLiteral("y"), y}, {QStringLiteral("width"), width}};

    return QVariantMap{{QStringLiteral("x"), input.source_rect.x() + static_cast<double>(x) / input.scale_x},
                       {QStringLiteral("y"), input.source_rect.y() + static_cast<double>(y) / input.scale_y},
                       {QStringLiteral("width"), static_cast<double>(width) / input.scale_x},
                       {QStringLiteral("height"), 1.0 / input.scale_y}};
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
    return {QPointF(rect.left(), rect.top()), QPointF(rect.right(), rect.top()),
            QPointF(rect.right(), rect.bottom()), QPointF(rect.left(), rect.bottom())};
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
    if (polygon.empty() || epsilon <= 0.0) return polygon;

    std::vector<QPointF> simplified = simplifyClosedPolygon(polygon, epsilon);
    simplified = removeCollinearPoints(simplified, epsilon);
    simplified = normalizePolygon(std::move(simplified));
    return simplified.empty() ? polygon : simplified;
}

/**
 * @brief 从 IoU 值中选择最佳 Mask 索引
 * @param iou_values IoU 值数组
 * @param candidate_count 候选数量
 * @return 最佳 Mask 索引
 */
int bestMaskIndex(const std::vector<float> &iou_values, int candidate_count)
{
    if (iou_values.empty() || candidate_count <= 1) return 0;

    const int count = std::min(candidate_count, static_cast<int>(iou_values.size()));
    return static_cast<int>(std::max_element(iou_values.begin(), iou_values.begin() + count) - iou_values.begin());
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
            [this](const QString &group_key, const QString &name, const QVariant &value) {
                if (group_key == QStringLiteral("SmartAnnotationSettings") && name == QStringLiteral("enabled"))
                {
                    const bool v = value.toBool();
                    if (v != enabled_) { enabled_ = v; emit enabledChanged(); }
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
    if (loading_model_ && loading_model_key_ == request.key) return;

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
                success       = true;
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

            if (!controller) return;

            QMetaObject::invokeMethod(
                controller.data(),
                [controller, request, predictor_holder, error, success]() mutable
                {
                    if (!controller) return;
                    if (controller->loading_model_key_ != request.key) return;

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
    QVariantMap result{{QStringLiteral("success"), false}, {QStringLiteral("error"), {}}, {QStringLiteral("loading"), false}};

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

        const QString model_name = normalizedModelName(settingString(settings, generated_field::SmartAnnotation::Model));
        const auto backend = static_cast<irt::model::ModelBackend>(
            settings->valueForField(generated_field::SmartAnnotation::ModelBackend).toInt());
        const auto device = static_cast<irt::model::ModelDevice>(
            settings->valueForField(generated_field::SmartAnnotation::ModelDevice).toInt());
        QString model_path = settingString(settings, generated_field::SmartAnnotation::ModelPath);
        if (model_name.isEmpty()) throw std::runtime_error("智能标注模型未配置");
        if (model_path.isEmpty()) throw std::runtime_error("智能标注模型文件未配置");

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

        const InferenceImageInput      image_input = prepareInferenceImageInput(image_path, options);
        const std::vector<PromptPoint> input_prompts = mapPromptsToInferenceInput(prompts, image_input);
        const irt::features::SAMImagePrediction prediction = predictor_->predict(
            toFilesystemPath(image_input.path), buildImagePrompt(input_prompts), buildPredictOptions(settings));
        const int   mask_index   = bestMaskIndex(prediction.iou_predictions, prediction.mask_count);
        const float selected_iou = (mask_index >= 0 && static_cast<size_t>(mask_index) < prediction.iou_predictions.size())
                                       ? prediction.iou_predictions[mask_index]
                                       : 0.0F;
        const int input_image_width  = prediction.width;
        const int input_image_height = prediction.height;
        const int image_width
            = image_input.viewport_input ? image_input.source_size.width() : input_image_width;
        const int image_height
            = image_input.viewport_input ? image_input.source_size.height() : input_image_height;

        std::vector<uint8_t> binary_mask = selectedBinaryMask(prediction, mask_index);
        const int foreground_pixels = static_cast<int>(std::count(binary_mask.begin(), binary_mask.end(), uint8_t{1}));

        const QRectF input_bbox = boundingBoxFromMask(binary_mask, input_image_width, input_image_height);
        if (input_bbox.isEmpty()) throw std::runtime_error("SAM 没有生成有效 mask，请调整提示点或阈值");

        std::vector<QPointF>              polygon;
        std::vector<std::vector<QPointF>> polygons = maskToPolygons(binary_mask, input_image_width, input_image_height);
        if (!polygons.empty()) polygon = std::move(polygons.front());
        else polygon = rectanglePoints(input_bbox);

        const QRectF         bbox = mapInputRectToSource(input_bbox, image_input);
        std::vector<QPointF> output_polygon = mapInputPolygonToSource(polygon, image_input);
        output_polygon = simplifyFinalPolygon(
            std::move(output_polygon),
            settingDouble(settings, generated_field::SmartAnnotation::PolygonSimplifyEpsilon, 2.0));
        if (output_polygon.size() < 3) output_polygon = rectanglePoints(bbox);

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
        const QString error = QString::fromUtf8(e.what());
        result[QStringLiteral("success")] = false;
        result[QStringLiteral("error")]   = error;
        setLastError(error);
        spdlog::error("智能标注失败: {}", error.toUtf8().constData());
    }
    catch (...)
    {
        const QString error = QString("未知智能标注错误");
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
    if (running_ == running) return;
    running_ = running;
    emit runningChanged();
}

void SmartAnnotationController::setLoadingModel(bool loading_model)
{
    if (loading_model_ == loading_model) return;
    loading_model_ = loading_model;
    emit loadingModelChanged();
}

void SmartAnnotationController::setLastError(const QString &last_error)
{
    if (last_error_ == last_error) return;
    last_error_ = last_error;
    emit lastErrorChanged();
}

} // namespace dltool::feature
