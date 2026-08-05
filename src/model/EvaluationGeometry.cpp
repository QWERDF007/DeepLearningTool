#include "model/EvaluationGeometry.h"

#include "model/ModelEvaluationProtocol.h"

#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <limits>

namespace dltool::model {

namespace {

/**
 * @brief 判断数值是否有限。
 * @param value 待检查数值。
 * @param output 输出解析后的数值，可为 nullptr。
 * @return 解析成功且有限返回 true。
 */
bool finiteNumber(const QVariant &value, double *output = nullptr)
{
    bool         ok     = false;
    const double number = value.toDouble(&ok);
    if (!ok || !std::isfinite(number))
        return false;
    if (output != nullptr)
        *output = number;
    return true;
}

/**
 * @brief 判断路径是否位于根目录之内。
 * @param root 根目录。
 * @param path 待检查路径。
 * @return 位于根目录内返回 true。
 */
bool pathWithin(const QString &root, const QString &path)
{
    const QString clean_root = QDir::fromNativeSeparators(QFileInfo(root).absoluteFilePath());
    const QString clean_path = QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath());
    return !clean_root.isEmpty() && !clean_path.isEmpty()
        && (clean_path.compare(clean_root, Qt::CaseInsensitive) == 0
            || clean_path.startsWith(clean_root + QLatin1Char('/'), Qt::CaseInsensitive));
}

/**
 * @brief 读取几何记录的字符串字段。
 * @param geometry 几何记录。
 * @param field 协议字段。
 * @return 字段值。
 */
QString geometryString(const QVariantMap &geometry, const evaluation::Field field)
{
    return geometry.value(evaluation::fieldName(field)).toString();
}

} // namespace

QVariantMap evaluationBoxMap(const EvaluationBox &box)
{
    return {
        {     evaluation::fieldName(evaluation::Field::X), box.x},
        {     evaluation::fieldName(evaluation::Field::Y), box.y},
        { evaluation::fieldName(evaluation::Field::Width), box.w},
        {evaluation::fieldName(evaluation::Field::Height), box.h}
    };
}

QString evaluationGeometryType(const QVariantMap &geometry)
{
    return geometryString(geometry, evaluation::Field::Type).trimmed().toLower();
}

QVariantMap canonicalGeometry(const QVariantMap &source, const EvaluationBox &box)
{
    if (source.isEmpty() && !box.valid())
        return {};
    QVariantMap   geometry = source;
    const QString type     = evaluationGeometryType(geometry);
    if (type.isEmpty())
    {
        if (geometry.contains(evaluation::fieldName(evaluation::Field::Points)))
        {
            geometry.insert(evaluation::fieldName(evaluation::Field::Type), QStringLiteral("polygon"));
            geometry.insert(evaluation::fieldName(evaluation::Field::Format), QStringLiteral("points"));
        }
        else if (box.valid())
        {
            geometry.insert(evaluation::fieldName(evaluation::Field::Type), QStringLiteral("bbox"));
            geometry.insert(evaluation::fieldName(evaluation::Field::Format), QStringLiteral("xywh"));
            geometry.insert(evaluation::fieldName(evaluation::Field::Values), QVariantList{box.x, box.y, box.w, box.h});
        }
    }
    if (!geometry.contains(evaluation::fieldName(evaluation::Field::CoordinateSystem)))
        geometry.insert(evaluation::fieldName(evaluation::Field::CoordinateSystem), QStringLiteral("image_pixels"));
    if (box.valid())
    {
        geometry.insert(evaluation::fieldName(evaluation::Field::Bounds), evaluationBoxMap(box));
        if (evaluationGeometryType(geometry) == QStringLiteral("bbox")
            || evaluationGeometryType(geometry) == QStringLiteral("box")
            || evaluationGeometryType(geometry) == QStringLiteral("rectangle"))
            geometry.insert(evaluation::fieldName(evaluation::Field::Values), QVariantList{box.x, box.y, box.w, box.h});
    }
    return geometry;
}

QVariantList normalizedOverlayPoints(const QVariantMap &geometry, const QVariantMap &crop)
{
    EvaluationBox viewport;
    if (!readBox(crop, viewport) || viewport.w <= 0.0 || viewport.h <= 0.0)
        return {};
    const QVariantList points = geometry.value(evaluation::fieldName(evaluation::Field::Points)).toList();
    if (points.size() < 3)
        return {};
    QVariantList normalized;
    normalized.reserve(points.size());
    for (const QVariant &value : points)
    {
        const QVariantList point = value.toList();
        if (point.size() < 2)
            continue;
        bool         x_ok = false;
        bool         y_ok = false;
        const double x    = point.at(0).toDouble(&x_ok);
        const double y    = point.at(1).toDouble(&y_ok);
        if (!x_ok || !y_ok || !std::isfinite(x) || !std::isfinite(y))
            continue;
        normalized.push_back(QVariantList{std::clamp((x - viewport.x) / viewport.w, 0.0, 1.0),
                                          std::clamp((y - viewport.y) / viewport.h, 0.0, 1.0)});
    }
    return normalized.size() >= 3 ? normalized : QVariantList{};
}

QString maskUrl(const QVariantMap &geometry, const QString &root)
{
    if (evaluationGeometryType(geometry) != QStringLiteral("mask"))
        return {};
    const QString artifact = geometryString(geometry, evaluation::Field::ArtifactPath).trimmed();
    if (artifact.isEmpty())
        return {};
    const QFileInfo info(QFileInfo(artifact).isAbsolute() ? QFileInfo(artifact).absoluteFilePath()
                                                          : QFileInfo(QDir(root), artifact).absoluteFilePath());
    if (!info.exists() || !info.isFile())
        return {};
    return QUrl::fromLocalFile(info.absoluteFilePath()).toString(QUrl::FullyEncoded);
}

bool readBox(const QVariantMap &value, EvaluationBox &box)
{
    QVariantMap source = value;
    if (source.contains(evaluation::fieldName(evaluation::Field::Bounds)))
        source = source.value(evaluation::fieldName(evaluation::Field::Bounds)).toMap();
    if (source.contains(QStringLiteral("bbox")))
        source = source.value(QStringLiteral("bbox")).toMap();
    if (source.contains(evaluation::fieldName(evaluation::Field::Values)))
    {
        const QVariantList values = source.value(evaluation::fieldName(evaluation::Field::Values)).toList();
        if (values.size() >= 4)
        {
            bool         ok[4] = {false, false, false, false};
            const double x     = values.at(0).toDouble(&ok[0]);
            const double y     = values.at(1).toDouble(&ok[1]);
            const double w     = values.at(2).toDouble(&ok[2]);
            const double h     = values.at(3).toDouble(&ok[3]);
            if (ok[0] && ok[1] && ok[2] && ok[3] && std::isfinite(x) && std::isfinite(y) && std::isfinite(w)
                && std::isfinite(h))
            {
                box = {x, y, w, h};
                return box.valid();
            }
            return false;
        }
    }
    if (source.contains(evaluation::fieldName(evaluation::Field::X))
        && source.contains(evaluation::fieldName(evaluation::Field::Y))
        && source.contains(evaluation::fieldName(evaluation::Field::Width))
        && source.contains(evaluation::fieldName(evaluation::Field::Height)))
    {
        bool         x_ok = false;
        bool         y_ok = false;
        bool         w_ok = false;
        bool         h_ok = false;
        const double x    = source.value(evaluation::fieldName(evaluation::Field::X)).toDouble(&x_ok);
        const double y    = source.value(evaluation::fieldName(evaluation::Field::Y)).toDouble(&y_ok);
        const double w    = source.value(evaluation::fieldName(evaluation::Field::Width)).toDouble(&w_ok);
        const double h    = source.value(evaluation::fieldName(evaluation::Field::Height)).toDouble(&h_ok);
        if (x_ok && y_ok && w_ok && h_ok && std::isfinite(x) && std::isfinite(y) && std::isfinite(w)
            && std::isfinite(h))
        {
            box = {x, y, w, h};
            return box.valid();
        }
        return false;
    }
    if (source.contains(evaluation::fieldName(evaluation::Field::Cx))
        && source.contains(evaluation::fieldName(evaluation::Field::Cy))
        && source.contains(evaluation::fieldName(evaluation::Field::Width))
        && source.contains(evaluation::fieldName(evaluation::Field::Height)))
    {
        bool         cx_ok = false;
        bool         cy_ok = false;
        bool         w_ok  = false;
        bool         h_ok  = false;
        const double cx    = source.value(evaluation::fieldName(evaluation::Field::Cx)).toDouble(&cx_ok);
        const double cy    = source.value(evaluation::fieldName(evaluation::Field::Cy)).toDouble(&cy_ok);
        const double w     = source.value(evaluation::fieldName(evaluation::Field::Width)).toDouble(&w_ok);
        const double h     = source.value(evaluation::fieldName(evaluation::Field::Height)).toDouble(&h_ok);
        if (cx_ok && cy_ok && w_ok && h_ok && std::isfinite(cx) && std::isfinite(cy) && std::isfinite(w)
            && std::isfinite(h))
        {
            box = {cx - w / 2.0, cy - h / 2.0, w, h};
            return box.valid();
        }
        return false;
    }
    const QVariantList points = source.value(evaluation::fieldName(evaluation::Field::Points)).toList();
    if (!points.isEmpty())
    {
        double min_x = std::numeric_limits<double>::max();
        double min_y = std::numeric_limits<double>::max();
        double max_x = std::numeric_limits<double>::lowest();
        double max_y = std::numeric_limits<double>::lowest();
        for (const QVariant &point : points)
        {
            const QVariantList pair = point.toList();
            if (pair.size() < 2)
                continue;
            bool         x_ok = false;
            bool         y_ok = false;
            const double x    = pair.at(0).toDouble(&x_ok);
            const double y    = pair.at(1).toDouble(&y_ok);
            if (!x_ok || !y_ok || !std::isfinite(x) || !std::isfinite(y))
                return false;
            min_x = std::min(min_x, x);
            min_y = std::min(min_y, y);
            max_x = std::max(max_x, x);
            max_y = std::max(max_y, y);
        }
        if (max_x > min_x && max_y > min_y)
        {
            box = {min_x, min_y, max_x - min_x, max_y - min_y};
            return true;
        }
    }
    return false;
}

bool validateGeometryProtocol(const QVariantMap &geometry, const int image_width, const int image_height,
                              const QString &task_root, QString *err_msg)
{
    if (geometry.isEmpty())
        return true;
    const QString type              = geometryString(geometry, evaluation::Field::Type).trimmed().toLower();
    const QString coordinate_system = geometryString(geometry, evaluation::Field::CoordinateSystem).trimmed().toLower();
    const QString format            = geometryString(geometry, evaluation::Field::Format).trimmed().toLower();
    if (type.isEmpty() || coordinate_system != QStringLiteral("image_pixels"))
    {
        if (err_msg)
            *err_msg = QString("预测 geometry 必须声明 coordinate_system=image_pixels");
        return false;
    }

    if (type == QStringLiteral("bbox") || type == QStringLiteral("box") || type == QStringLiteral("rectangle"))
    {
        if (format != QStringLiteral("xywh"))
        {
            if (err_msg)
                *err_msg = QString("预测 bbox geometry.format 必须为 xywh");
            return false;
        }
        const QVariantList values = geometry.value(evaluation::fieldName(evaluation::Field::Values)).toList();
        if (values.size() != 4)
        {
            if (err_msg)
                *err_msg = QString("预测 bbox geometry.values 必须包含 4 个数值");
            return false;
        }
        double x      = 0.0;
        double y      = 0.0;
        double width  = 0.0;
        double height = 0.0;
        if (!finiteNumber(values.at(0), &x) || !finiteNumber(values.at(1), &y) || !finiteNumber(values.at(2), &width)
            || !finiteNumber(values.at(3), &height) || width <= 0.0 || height <= 0.0)
        {
            if (err_msg)
                *err_msg = QString("预测 bbox geometry.values 无效");
            return false;
        }
        if (image_width > 0 && image_height > 0)
        {
            const double right  = x + width;
            const double bottom = y + height;
            if (right <= 0.0 || bottom <= 0.0 || x >= image_width || y >= image_height)
            {
                if (err_msg)
                    *err_msg = QString("预测 bbox 完全位于图像边界之外");
                return false;
            }
        }
        return true;
    }

    if (type == QStringLiteral("polygon") || type == QStringLiteral("segmentation"))
    {
        if (format != QStringLiteral("polygon") && format != QStringLiteral("points"))
        {
            if (err_msg)
                *err_msg = QString("预测 polygon geometry.format 无效");
            return false;
        }
        const QVariantList points = geometry.value(evaluation::fieldName(evaluation::Field::Points)).toList();
        if (points.size() < 3)
        {
            if (err_msg)
                *err_msg = QString("预测 polygon 至少需要 3 个点");
            return false;
        }
        for (const QVariant &point_value : points)
        {
            const QVariantList point = point_value.toList();
            if (point.size() < 2 || !finiteNumber(point.at(0)) || !finiteNumber(point.at(1)))
            {
                if (err_msg)
                    *err_msg = QString("预测 polygon 点坐标无效");
                return false;
            }
        }
        return true;
    }

    if (type == QStringLiteral("mask"))
    {
        if (format != QStringLiteral("mask_reference") && format != QStringLiteral("file"))
        {
            if (err_msg)
                *err_msg = QString("预测 mask geometry.format 无效");
            return false;
        }
        const QString artifact = geometryString(geometry, evaluation::Field::ArtifactPath).trimmed();
        const QString target   = QFileInfo(artifact).isAbsolute()
                                   ? QFileInfo(artifact).absoluteFilePath()
                                   : QFileInfo(QDir(task_root), artifact).absoluteFilePath();
        if (artifact.isEmpty() || !pathWithin(task_root, target) || !QFileInfo(target).exists()
            || !QFileInfo(target).isFile())
        {
            if (err_msg)
                *err_msg = QString("预测 mask artifact_path 越界或文件不存在");
            return false;
        }
        return true;
    }

    if (err_msg)
        *err_msg = QString("预测 geometry.type 不受支持");
    return false;
}

double intersectionOverUnion(const EvaluationBox &lhs, const EvaluationBox &rhs)
{
    if (!lhs.valid() || !rhs.valid())
        return 0.0;
    const double left         = std::max(lhs.x, rhs.x);
    const double top          = std::max(lhs.y, rhs.y);
    const double right        = std::min(lhs.x + lhs.w, rhs.x + rhs.w);
    const double bottom       = std::min(lhs.y + lhs.h, rhs.y + rhs.h);
    const double intersection = std::max(0.0, right - left) * std::max(0.0, bottom - top);
    const double area         = lhs.w * lhs.h + rhs.w * rhs.h - intersection;
    return area > 0.0 ? intersection / area : 0.0;
}

QVariantMap unionBounds(const QVariantMap &gt, const QVariantMap &pred)
{
    EvaluationBox a;
    EvaluationBox b;
    const bool    has_a = readBox(gt, a);
    const bool    has_b = readBox(pred, b);
    if (!has_a)
        return has_b ? evaluationBoxMap(b) : QVariantMap{};
    if (!has_b)
        return evaluationBoxMap(a);
    const double left   = std::min(a.x, b.x);
    const double top    = std::min(a.y, b.y);
    const double right  = std::max(a.x + a.w, b.x + b.w);
    const double bottom = std::max(a.y + a.h, b.y + b.h);
    return evaluationBoxMap({left, top, right - left, bottom - top});
}

QVariantMap cropBounds(const QVariantMap &gt, const QVariantMap &pred, const int image_width, const int image_height)
{
    EvaluationBox bounds;
    if (!readBox(unionBounds(gt, pred), bounds))
        return {};
    const double padding = std::max(4.0, std::max(bounds.w, bounds.h) * 0.05);
    double       left    = std::max(0.0, bounds.x - padding);
    double       top     = std::max(0.0, bounds.y - padding);
    double       right   = bounds.x + bounds.w + padding;
    double       bottom  = bounds.y + bounds.h + padding;
    if (image_width > 0)
    {
        right = std::min(right, static_cast<double>(image_width));
        left  = std::min(left, right);
    }
    if (image_height > 0)
    {
        bottom = std::min(bottom, static_cast<double>(image_height));
        top    = std::min(top, bottom);
    }
    return evaluationBoxMap({left, top, std::max(0.0, right - left), std::max(0.0, bottom - top)});
}

QVariantMap normalizedOverlayBounds(const QVariantMap &bounds, const QVariantMap &crop)
{
    EvaluationBox box;
    EvaluationBox viewport;
    if (!readBox(bounds, box) || !readBox(crop, viewport) || viewport.w <= 0.0 || viewport.h <= 0.0)
        return {};
    const double left   = std::clamp((box.x - viewport.x) / viewport.w, 0.0, 1.0);
    const double top    = std::clamp((box.y - viewport.y) / viewport.h, 0.0, 1.0);
    const double right  = std::clamp((box.x + box.w - viewport.x) / viewport.w, 0.0, 1.0);
    const double bottom = std::clamp((box.y + box.h - viewport.y) / viewport.h, 0.0, 1.0);
    return evaluationBoxMap({left, top, std::max(0.0, right - left), std::max(0.0, bottom - top)});
}

} // namespace dltool::model
