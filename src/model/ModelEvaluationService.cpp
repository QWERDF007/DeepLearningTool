#include "model/ModelEvaluationService.h"
#include "model/ModelEvaluationProtocol.h"

#include "common/YamlUtils.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QTextStream>
#include <QUrl>
#include <QVariantList>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace dltool::model {

namespace {

using dltool::common::yaml::nodeString;
using dltool::common::yaml::nodeVariant;

// Evaluation files are user-/framework-produced input.  Bound both the
// document size and sequence cardinality before yaml-cpp expands them into a
// node tree, so a malformed result cannot exhaust the GUI process.
constexpr qint64 kMaxEvaluationYamlBytes = 256LL * 1024LL * 1024LL;
constexpr std::size_t kMaxEvaluationRecords = 5'000'000;

struct Box
{
    double x{0.0};
    double y{0.0};
    double w{0.0};
    double h{0.0};

    bool valid() const { return w > 0.0 && h > 0.0; }
};

struct GroundTruth
{
    qint64 label_id{-1};
    int class_id{-1};
    QString class_name;
    QVariantMap geometry;
    QVariantMap bounds;
    Box box;
};

struct Prediction
{
    QString prediction_id;
    qint64 image_id{-1};
    int class_id{-1};
    QString class_name;
    double score{0.0};
    QVariantMap geometry;
    QVariantMap bounds;
    Box box;
};

struct Image
{
    qint64 id{-1};
    qint64 dataset_id{-1};
    QString path;
    QString name;
    int width{0};
    int height{0};
    QList<GroundTruth> gt;
    QList<Prediction> predictions;
};

QString mapString(const QVariantMap &map, const QString &key, const QString &fallback = {})
{
    const QVariant value = map.value(key);
    return value.isValid() ? value.toString() : fallback;
}

qint64 mapLong(const QVariantMap &map, const QString &key, qint64 fallback = -1)
{
    bool ok = false;
    const qint64 value = map.value(key).toLongLong(&ok);
    return ok ? value : fallback;
}

int mapInt(const QVariantMap &map, const QString &key, int fallback = -1)
{
    bool ok = false;
    const int value = map.value(key).toInt(&ok);
    return ok ? value : fallback;
}

double mapDouble(const QVariantMap &map, const QString &key, double fallback = 0.0)
{
    bool ok = false;
    const double value = map.value(key).toDouble(&ok);
    return ok && std::isfinite(value) ? value : fallback;
}

bool pathWithin(const QString &root, const QString &path)
{
    const QString clean_root = QDir::fromNativeSeparators(QFileInfo(root).absoluteFilePath());
    const QString clean_path = QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath());
    return !clean_root.isEmpty() && !clean_path.isEmpty()
        && (clean_path.compare(clean_root, Qt::CaseInsensitive) == 0
            || clean_path.startsWith(clean_root + QLatin1Char('/'), Qt::CaseInsensitive));
}

bool sourceImageExists(const QString &path, const QString &dataset_manifest)
{
    QFileInfo image(path);
    if (!image.isAbsolute() && !image.exists() && !dataset_manifest.isEmpty())
        image = QFileInfo(QDir(QFileInfo(dataset_manifest).absolutePath()), path);
    return image.exists() && image.isFile();
}

bool finiteNumber(const QVariant &value, double *output = nullptr)
{
    bool ok = false;
    const double number = value.toDouble(&ok);
    if (!ok || !std::isfinite(number))
        return false;
    if (output != nullptr)
        *output = number;
    return true;
}

QVariantMap boxMap(const Box &box)
{
    return {{QStringLiteral("x"), box.x}, {QStringLiteral("y"), box.y},
            {QStringLiteral("width"), box.w}, {QStringLiteral("height"), box.h}};
}

QString geometryType(const QVariantMap &geometry)
{
    return mapString(geometry, QStringLiteral("type")).trimmed().toLower();
}

QVariantMap canonicalGeometry(const QVariantMap &source, const Box &box)
{
    if (source.isEmpty() && !box.valid())
        return {};
    QVariantMap geometry = source;
    const QString type = geometryType(geometry);
    if (type.isEmpty())
    {
        if (geometry.contains(QStringLiteral("points")))
        {
            geometry.insert(QStringLiteral("type"), QStringLiteral("polygon"));
            geometry.insert(QStringLiteral("format"), QStringLiteral("points"));
        }
        else if (box.valid())
        {
            geometry.insert(QStringLiteral("type"), QStringLiteral("bbox"));
            geometry.insert(QStringLiteral("format"), QStringLiteral("xywh"));
            geometry.insert(QStringLiteral("values"), QVariantList{box.x, box.y, box.w, box.h});
        }
    }
    if (!geometry.contains(QStringLiteral("coordinate_system")))
        geometry.insert(QStringLiteral("coordinate_system"), QStringLiteral("image_pixels"));
    if (box.valid())
    {
        geometry.insert(QStringLiteral("bounds"), boxMap(box));
        if (geometryType(geometry) == QStringLiteral("bbox")
            || geometryType(geometry) == QStringLiteral("box")
            || geometryType(geometry) == QStringLiteral("rectangle"))
            geometry.insert(QStringLiteral("values"), QVariantList{box.x, box.y, box.w, box.h});
    }
    return geometry;
}

bool readBox(const QVariantMap &value, Box &box);

QVariantList normalizedOverlayPoints(const QVariantMap &geometry, const QVariantMap &crop)
{
    Box viewport;
    if (!readBox(crop, viewport) || viewport.w <= 0.0 || viewport.h <= 0.0)
        return {};
    const QVariantList points = geometry.value(QStringLiteral("points")).toList();
    if (points.size() < 3)
        return {};
    QVariantList normalized;
    normalized.reserve(points.size());
    for (const QVariant &value : points)
    {
        const QVariantList point = value.toList();
        if (point.size() < 2)
            continue;
        bool x_ok = false;
        bool y_ok = false;
        const double x = point.at(0).toDouble(&x_ok);
        const double y = point.at(1).toDouble(&y_ok);
        if (!x_ok || !y_ok || !std::isfinite(x) || !std::isfinite(y))
            continue;
        normalized.push_back(QVariantList{
            std::clamp((x - viewport.x) / viewport.w, 0.0, 1.0),
            std::clamp((y - viewport.y) / viewport.h, 0.0, 1.0)});
    }
    return normalized.size() >= 3 ? normalized : QVariantList{};
}

QString maskUrl(const QVariantMap &geometry, const QString &root)
{
    if (geometryType(geometry) != QStringLiteral("mask"))
        return {};
    const QString artifact = mapString(geometry, QStringLiteral("artifact_path")).trimmed();
    if (artifact.isEmpty())
        return {};
    const QFileInfo info(QFileInfo(artifact).isAbsolute()
                             ? QFileInfo(artifact).absoluteFilePath()
                             : QFileInfo(QDir(root), artifact).absoluteFilePath());
    if (!info.exists() || !info.isFile())
        return {};
    return QUrl::fromLocalFile(info.absoluteFilePath()).toString(QUrl::FullyEncoded);
}

bool readBox(const QVariantMap &value, Box &box)
{
    QVariantMap source = value;
    if (source.contains(QStringLiteral("bounds")))
        source = source.value(QStringLiteral("bounds")).toMap();
    if (source.contains(QStringLiteral("bbox")))
        source = source.value(QStringLiteral("bbox")).toMap();
    if (source.contains(QStringLiteral("values")))
    {
        const QVariantList values = source.value(QStringLiteral("values")).toList();
        if (values.size() >= 4)
        {
            bool ok[4] = {false, false, false, false};
            const double x = values.at(0).toDouble(&ok[0]);
            const double y = values.at(1).toDouble(&ok[1]);
            const double w = values.at(2).toDouble(&ok[2]);
            const double h = values.at(3).toDouble(&ok[3]);
            if (ok[0] && ok[1] && ok[2] && ok[3] && std::isfinite(x) && std::isfinite(y)
                && std::isfinite(w) && std::isfinite(h))
            {
                box = {x, y, w, h};
                return box.valid();
            }
            return false;
        }
    }
    if (source.contains(QStringLiteral("x")) && source.contains(QStringLiteral("y"))
        && source.contains(QStringLiteral("width")) && source.contains(QStringLiteral("height")))
    {
        bool x_ok = false;
        bool y_ok = false;
        bool w_ok = false;
        bool h_ok = false;
        const double x = source.value(QStringLiteral("x")).toDouble(&x_ok);
        const double y = source.value(QStringLiteral("y")).toDouble(&y_ok);
        const double w = source.value(QStringLiteral("width")).toDouble(&w_ok);
        const double h = source.value(QStringLiteral("height")).toDouble(&h_ok);
        if (x_ok && y_ok && w_ok && h_ok && std::isfinite(x) && std::isfinite(y) && std::isfinite(w)
            && std::isfinite(h))
        {
            box = {x, y, w, h};
            return box.valid();
        }
        return false;
    }
    if (source.contains(QStringLiteral("cx")) && source.contains(QStringLiteral("cy"))
        && source.contains(QStringLiteral("width")) && source.contains(QStringLiteral("height")))
    {
        bool cx_ok = false;
        bool cy_ok = false;
        bool w_ok = false;
        bool h_ok = false;
        const double cx = source.value(QStringLiteral("cx")).toDouble(&cx_ok);
        const double cy = source.value(QStringLiteral("cy")).toDouble(&cy_ok);
        const double w = source.value(QStringLiteral("width")).toDouble(&w_ok);
        const double h = source.value(QStringLiteral("height")).toDouble(&h_ok);
        if (cx_ok && cy_ok && w_ok && h_ok && std::isfinite(cx) && std::isfinite(cy) && std::isfinite(w)
            && std::isfinite(h))
        {
            box = {cx - w / 2.0, cy - h / 2.0, w, h};
            return box.valid();
        }
        return false;
    }
    const QVariantList points = source.value(QStringLiteral("points")).toList();
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
            bool x_ok = false;
            bool y_ok = false;
            const double x = pair.at(0).toDouble(&x_ok);
            const double y = pair.at(1).toDouble(&y_ok);
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

bool validateGeometryProtocol(const QVariantMap &geometry, const Image &image, const QString &task_root,
                              QString *err_msg)
{
    if (geometry.isEmpty())
        return true;
    const QString type = mapString(geometry, QStringLiteral("type")).trimmed().toLower();
    const QString coordinate_system = mapString(geometry, QStringLiteral("coordinate_system")).trimmed().toLower();
    const QString format = mapString(geometry, QStringLiteral("format")).trimmed().toLower();
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
        const QVariantList values = geometry.value(QStringLiteral("values")).toList();
        if (values.size() != 4)
        {
            if (err_msg)
                *err_msg = QString("预测 bbox geometry.values 必须包含 4 个数值");
            return false;
        }
        double x = 0.0;
        double y = 0.0;
        double width = 0.0;
        double height = 0.0;
        if (!finiteNumber(values.at(0), &x) || !finiteNumber(values.at(1), &y)
            || !finiteNumber(values.at(2), &width) || !finiteNumber(values.at(3), &height)
            || width <= 0.0 || height <= 0.0)
        {
            if (err_msg)
                *err_msg = QString("预测 bbox geometry.values 无效");
            return false;
        }
        if (image.width > 0 && image.height > 0)
        {
            const double right = x + width;
            const double bottom = y + height;
            if (right <= 0.0 || bottom <= 0.0 || x >= image.width || y >= image.height)
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
        const QVariantList points = geometry.value(QStringLiteral("points")).toList();
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
        const QString artifact = mapString(geometry, QStringLiteral("artifact_path")).trimmed();
        const QString target = QFileInfo(artifact).isAbsolute()
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

double intersectionOverUnion(const Box &lhs, const Box &rhs)
{
    if (!lhs.valid() || !rhs.valid())
        return 0.0;
    const double left = std::max(lhs.x, rhs.x);
    const double top = std::max(lhs.y, rhs.y);
    const double right = std::min(lhs.x + lhs.w, rhs.x + rhs.w);
    const double bottom = std::min(lhs.y + lhs.h, rhs.y + rhs.h);
    const double intersection = std::max(0.0, right - left) * std::max(0.0, bottom - top);
    const double area = lhs.w * lhs.h + rhs.w * rhs.h - intersection;
    return area > 0.0 ? intersection / area : 0.0;
}

QVariantMap unionBounds(const QVariantMap &gt, const QVariantMap &pred)
{
    Box a;
    Box b;
    const bool has_a = readBox(gt, a);
    const bool has_b = readBox(pred, b);
    if (!has_a)
        return has_b ? boxMap(b) : QVariantMap{};
    if (!has_b)
        return boxMap(a);
    const double left = std::min(a.x, b.x);
    const double top = std::min(a.y, b.y);
    const double right = std::max(a.x + a.w, b.x + b.w);
    const double bottom = std::max(a.y + a.h, b.y + b.h);
    return boxMap({left, top, right - left, bottom - top});
}

QVariantMap cropBounds(const QVariantMap &gt, const QVariantMap &pred, const int image_width,
                       const int image_height)
{
    Box bounds;
    if (!readBox(unionBounds(gt, pred), bounds))
        return {};
    const double padding = std::max(4.0, std::max(bounds.w, bounds.h) * 0.05);
    double left = std::max(0.0, bounds.x - padding);
    double top = std::max(0.0, bounds.y - padding);
    double right = bounds.x + bounds.w + padding;
    double bottom = bounds.y + bounds.h + padding;
    if (image_width > 0)
    {
        right = std::min(right, static_cast<double>(image_width));
        left = std::min(left, right);
    }
    if (image_height > 0)
    {
        bottom = std::min(bottom, static_cast<double>(image_height));
        top = std::min(top, bottom);
    }
    return boxMap({left, top, std::max(0.0, right - left), std::max(0.0, bottom - top)});
}

QVariantMap normalizedOverlayBounds(const QVariantMap &bounds, const QVariantMap &crop)
{
    Box box;
    Box viewport;
    if (!readBox(bounds, box) || !readBox(crop, viewport) || viewport.w <= 0.0 || viewport.h <= 0.0)
        return {};
    const double left = std::clamp((box.x - viewport.x) / viewport.w, 0.0, 1.0);
    const double top = std::clamp((box.y - viewport.y) / viewport.h, 0.0, 1.0);
    const double right = std::clamp((box.x + box.w - viewport.x) / viewport.w, 0.0, 1.0);
    const double bottom = std::clamp((box.y + box.h - viewport.y) / viewport.h, 0.0, 1.0);
    return boxMap({left, top, std::max(0.0, right - left), std::max(0.0, bottom - top)});
}

QList<QString> parseCsvLine(const QString &line, bool *valid = nullptr)
{
    QList<QString> fields;
    QString field;
    bool quoted = false;
    for (int i = 0; i < line.size(); ++i)
    {
        const QChar c = line.at(i);
        if (c == QChar('"'))
        {
            if (quoted && i + 1 < line.size() && line.at(i + 1) == QChar('"'))
            {
                field += QChar('"');
                ++i;
            }
            else
                quoted = !quoted;
        }
        else if (c == QChar(',') && !quoted)
        {
            fields.push_back(field);
            field.clear();
        }
        else
            field += c;
    }
    fields.push_back(field);
    if (valid != nullptr)
        *valid = !quoted;
    return fields;
}

bool isCancelled(const std::shared_ptr<std::atomic_bool> &cancel_token)
{
    return cancel_token != nullptr && cancel_token->load(std::memory_order_relaxed);
}

bool loadImages(const QString &images_path, const QString &dataset_manifest_path, QMap<qint64, Image> &images,
                const std::shared_ptr<std::atomic_bool> &cancel_token, QString *err_msg)
{
    if (isCancelled(cancel_token))
    {
        if (err_msg)
            *err_msg = QString("评估已取消");
        return false;
    }
    const QFileInfo manifest_file(dataset_manifest_path);
    if (!manifest_file.exists() || !manifest_file.isFile())
    {
        if (err_msg)
            *err_msg = QString("测试数据集 manifest.yaml 不存在: %1").arg(dataset_manifest_path);
        return false;
    }
    if (manifest_file.size() > kMaxEvaluationYamlBytes)
    {
        if (err_msg)
            *err_msg = QString("测试数据集 manifest.yaml 超过大小限制");
        return false;
    }
    const QFileInfo images_file(images_path);
    if (!images_file.exists() || !images_file.isFile())
    {
        if (err_msg)
            *err_msg = QString("pred/images.txt 不存在: %1").arg(images_path);
        return false;
    }
    if (images_file.size() > kMaxEvaluationYamlBytes)
    {
        if (err_msg)
            *err_msg = QString("pred/images.txt 超过大小限制");
        return false;
    }
    try
    {
        YAML::Node root = dltool::common::yaml::loadFile(manifest_file);
        YAML::Node entries = root["images"];
        const bool anomaly_samples = !entries || !entries.IsSequence();
        if (anomaly_samples)
            entries = root["samples"];
        if (!entries || !entries.IsSequence())
            throw std::runtime_error("dataset manifest requires images or samples sequence");
        if (entries.size() > kMaxEvaluationRecords)
            throw std::runtime_error("dataset manifest records 数量超过限制");
        QSet<qint64> manifest_ids;
        for (const YAML::Node &node : entries)
        {
            if (isCancelled(cancel_token))
            {
                if (err_msg)
                    *err_msg = QString("评估已取消");
                return false;
            }
            const QVariantMap value = nodeVariant(node).toMap();
            Image image;
            image.id = mapLong(value, QStringLiteral("id"));
            image.dataset_id = mapLong(value, QStringLiteral("dataset_id"));
            image.path = mapString(value, QStringLiteral("path"));
            image.name = QFileInfo(image.path).fileName();
            image.width = mapInt(value, QStringLiteral("width"), 0);
            image.height = mapInt(value, QStringLiteral("height"), 0);
            if (image.id < 0 || image.path.trimmed().isEmpty())
                throw std::runtime_error("dataset manifest image requires id and path");
            if (manifest_ids.contains(image.id))
                throw std::runtime_error("dataset manifest image id duplicated");
            manifest_ids.insert(image.id);
            if (anomaly_samples)
            {
                const int label_index = mapInt(value, QStringLiteral("label_index"), 0);
                if (label_index > 0)
                    image.gt.push_back(GroundTruth{-1, 1, QStringLiteral("anomaly"), {}, {}});
            }
            for (const QVariant &entry : value.value(QStringLiteral("labels")).toList())
            {
                const QVariantMap label = entry.toMap();
                GroundTruth gt;
                gt.label_id = mapLong(label, QStringLiteral("label_id"));
                gt.class_id = mapInt(label, QStringLiteral("label_class_id"), mapInt(label, QStringLiteral("class_id")));
                gt.class_name = mapString(label, QStringLiteral("label_class_name"),
                                           mapString(label, QStringLiteral("class_name")));
                const QVariantMap data = label.value(QStringLiteral("data")).toMap();
                gt.geometry = data.value(QStringLiteral("geometry")).toMap();
                if (gt.geometry.isEmpty())
                    gt.geometry = data;
                gt.bounds = data;
                if (!readBox(gt.geometry, gt.box))
                {
                    const QVariantMap yolo = label.value(QStringLiteral("yolo")).toMap();
                    if (!yolo.isEmpty() && image.width > 0 && image.height > 0)
                    {
                        const double cx = mapDouble(yolo, QStringLiteral("cx")) * image.width;
                        const double cy = mapDouble(yolo, QStringLiteral("cy")) * image.height;
                        const double width = mapDouble(yolo, QStringLiteral("width")) * image.width;
                        const double height = mapDouble(yolo, QStringLiteral("height")) * image.height;
                        gt.box = {cx - width / 2.0, cy - height / 2.0, width, height};
                        gt.geometry = {};
                    }
                }
                gt.geometry = canonicalGeometry(gt.geometry, gt.box);
                if (gt.box.valid())
                    gt.bounds = boxMap(gt.box);
                image.gt.push_back(gt);
            }
            images.insert(image.id, image);
        }
    }
    catch (const std::exception &e)
    {
        if (err_msg)
            *err_msg = QString("读取测试数据集 manifest 失败: %1").arg(QString(e.what()));
        return false;
    }

    const QFileInfo list_file(images_path);
    if (!list_file.exists() || !list_file.isFile())
    {
        if (err_msg)
            *err_msg = QString("pred/images.txt 不存在: %1").arg(images_path);
        return false;
    }
    QFile file(images_path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (err_msg)
            *err_msg = QString("打开 pred/images.txt 失败: %1").arg(file.errorString());
        return false;
    }
    QTextStream stream(&file);
    bool first = true;
    QSet<qint64> listed;
    while (!stream.atEnd())
    {
        if (isCancelled(cancel_token))
        {
            if (err_msg)
                *err_msg = QString("评估已取消");
            return false;
        }
        QString line = stream.readLine();
        if (line.trimmed().isEmpty())
            continue;
        if (first && !line.isEmpty() && line.at(0) == QChar(0xfeff))
            line.remove(0, 1);
        if (first)
        {
            first = false;
            if (line.trimmed().compare(QStringLiteral("image_id,image_path"), Qt::CaseInsensitive) == 0)
                continue;
        }
        bool csv_valid = false;
        const QList<QString> fields = parseCsvLine(line, &csv_valid);
        if (!csv_valid || fields.size() != 2)
        {
            if (err_msg)
                *err_msg = QString("pred/images.txt 行格式无效: %1").arg(line);
            return false;
        }
        bool ok = false;
        const qint64 image_id = fields.at(0).trimmed().toLongLong(&ok);
        if (!ok || image_id < 0)
        {
            if (err_msg)
                *err_msg = QString("pred/images.txt image_id 无效: %1").arg(fields.at(0));
            return false;
        }
        if (listed.contains(image_id))
        {
            if (err_msg)
                *err_msg = QString("pred/images.txt image_id 重复: %1").arg(image_id);
            return false;
        }
        if (!images.contains(image_id))
        {
            if (err_msg)
                *err_msg = QString("pred/images.txt image_id 不在测试数据集 manifest 中: %1").arg(image_id);
            return false;
        }
        if (fields.at(1).trimmed().isEmpty())
        {
            if (err_msg)
                *err_msg = QString("pred/images.txt image_path 为空: %1").arg(image_id);
            return false;
        }
        Image &image = images[image_id];
        image.path = fields.at(1).trimmed();
        image.name = QFileInfo(image.path).fileName();
        listed.insert(image_id);
    }
    if (listed.isEmpty())
    {
        if (err_msg)
            *err_msg = QString("pred/images.txt 没有有效图像");
        return false;
    }
    if (listed.size() != images.size())
    {
        if (err_msg)
            *err_msg = QString("pred/images.txt 未覆盖测试数据集 manifest 中的全部图像");
        return false;
    }
    for (auto it = images.begin(); it != images.end();)
    {
        if (!listed.contains(it.key()))
            it = images.erase(it);
        else
            ++it;
    }
    return true;
}

bool loadPredictions(const QString &manifest_path, QMap<qint64, Image> &images, int *count,
                     const std::shared_ptr<std::atomic_bool> &cancel_token, QString *err_msg,
                     const QString &expected_model_uuid = {}, const QString &expected_task_uuid = {},
                     const QString &expected_method = {})
{
    if (isCancelled(cancel_token))
    {
        if (err_msg)
            *err_msg = QString("评估已取消");
        return false;
    }
    const QFileInfo file(manifest_path);
    if (!file.exists() || !file.isFile())
    {
        if (err_msg)
            *err_msg = QString("pred/manifest.yaml 不存在: %1").arg(manifest_path);
        return false;
    }
    if (file.size() > kMaxEvaluationYamlBytes)
    {
        if (err_msg)
            *err_msg = QString("pred/manifest.yaml 超过大小限制");
        return false;
    }
    try
    {
        YAML::Node root = dltool::common::yaml::loadFile(file);
        if (!root || !root.IsMap() || !root["schema_version"] || !root["schema_version"].IsScalar()
            || root["schema_version"].as<int>() != 1)
            throw std::runtime_error("预测清单 schema_version 必须为 1");
        for (const char *field : {"model_uuid", "test_task_uuid", "method"})
        {
            if (!root[field] || !root[field].IsScalar() || root[field].as<std::string>().empty())
                throw std::runtime_error("预测清单缺少 model_uuid/test_task_uuid/method");
        }
        if (!expected_model_uuid.isEmpty() && nodeString(root["model_uuid"]) != expected_model_uuid)
            throw std::runtime_error("预测清单 model_uuid 不一致");
        if (!expected_task_uuid.isEmpty() && nodeString(root["test_task_uuid"]) != expected_task_uuid)
            throw std::runtime_error("预测清单 test_task_uuid 不一致");
        if (!expected_method.isEmpty() && nodeString(root["method"]) != expected_method)
            throw std::runtime_error("预测清单 method 不一致");
        if (!root["records"] || !root["records"].IsSequence())
            throw std::runtime_error("预测清单缺少 records sequence");
        if (root["records"].size() > kMaxEvaluationRecords)
            throw std::runtime_error("预测清单 records 数量超过限制");
        if (!root["record_count"] || !root["record_count"].IsScalar()
            || root["record_count"].as<int>() < 0
            || root["record_count"].as<int>() != static_cast<int>(root["records"].size()))
            throw std::runtime_error("预测清单 record_count 与 records 数量不一致");
        QSet<QString> ids;
        int total = 0;
        for (std::size_t record_index = 0; record_index < root["records"].size(); ++record_index)
        {
            const YAML::Node node = root["records"][record_index];
            if (isCancelled(cancel_token))
            {
                if (err_msg)
                    *err_msg = QString("评估已取消");
                return false;
            }
            if (!node || !node.IsMap())
                throw std::runtime_error("预测记录必须为 YAML map");
            const QVariantMap value = nodeVariant(node).toMap();
            if (!value.contains(QStringLiteral("prediction_id")) || !value.contains(QStringLiteral("image_id"))
                || !value.contains(QStringLiteral("class_id")) || !value.contains(QStringLiteral("score")))
                throw std::runtime_error("预测记录缺少必填字段");
            Prediction prediction;
            prediction.prediction_id = mapString(value, QStringLiteral("prediction_id"));
            prediction.image_id = mapLong(value, QStringLiteral("image_id"));
            prediction.class_id = mapInt(value, QStringLiteral("class_id"));
            prediction.class_name = mapString(value, QStringLiteral("class_name"));
            const QVariant score_value = value.value(QStringLiteral("score"));
            bool score_ok = false;
            prediction.score = score_value.toDouble(&score_ok);
            if (!score_ok || !std::isfinite(prediction.score) || prediction.score < 0.0 || prediction.score > 1.0)
                throw std::runtime_error("预测 score 不是有限数值");
            if (prediction.prediction_id.isEmpty())
                throw std::runtime_error("预测 prediction_id 为空");
            if (prediction.class_id < 0)
                throw std::runtime_error("预测 class_id 无效");
            if (prediction.image_id < 0 || !images.contains(prediction.image_id))
                throw std::runtime_error("预测 image_id 不在 pred/images.txt 中");
            if (ids.contains(prediction.prediction_id))
                throw std::runtime_error("预测 prediction_id 重复");
            ids.insert(prediction.prediction_id);
            const QVariantMap geometry = value.value(QStringLiteral("geometry")).toMap();
            prediction.geometry = geometry;
            prediction.bounds = geometry.value(QStringLiteral("bounds")).toMap();
            if (!geometry.isEmpty())
            {
                QString geometry_error;
                const QString task_root = QFileInfo(file.absolutePath()).absoluteDir().absolutePath();
                if (!validateGeometryProtocol(geometry, images[prediction.image_id], task_root, &geometry_error))
                    throw std::runtime_error(geometry_error.toUtf8().constData());
            }
            if (readBox(geometry, prediction.box))
            {
                if (images[prediction.image_id].width > 0 && images[prediction.image_id].height > 0)
                {
                    const double right = std::clamp(prediction.box.x + prediction.box.w, 0.0,
                                                    static_cast<double>(images[prediction.image_id].width));
                    const double bottom = std::clamp(prediction.box.y + prediction.box.h, 0.0,
                                                     static_cast<double>(images[prediction.image_id].height));
                    prediction.box.x = std::clamp(prediction.box.x, 0.0,
                                                  static_cast<double>(images[prediction.image_id].width));
                    prediction.box.y = std::clamp(prediction.box.y, 0.0,
                                                  static_cast<double>(images[prediction.image_id].height));
                    prediction.box.w = std::max(0.0, right - prediction.box.x);
                    prediction.box.h = std::max(0.0, bottom - prediction.box.y);
                }
                prediction.bounds = boxMap(prediction.box);
            }
            prediction.geometry = canonicalGeometry(prediction.geometry, prediction.box);
            images[prediction.image_id].predictions.push_back(prediction);
            ++total;
        }
        if (count)
            *count = total;
    }
    catch (const std::exception &e)
    {
        if (err_msg)
            *err_msg = QString("读取预测清单失败: %1").arg(QString(e.what()));
        return false;
    }
    return true;
}

double safeRatio(qint64 numerator, qint64 denominator)
{
    return denominator > 0 ? static_cast<double>(numerator) / static_cast<double>(denominator) : 0.0;
}

QVariantMap metricMap(qint64 tp, qint64 fp, qint64 fn)
{
    const double precision = safeRatio(tp, tp + fp);
    const double recall = safeRatio(tp, tp + fn);
    const double f1 = precision + recall > 0.0 ? 2.0 * precision * recall / (precision + recall) : 0.0;
    return {{QStringLiteral("precision"), precision}, {QStringLiteral("recall"), recall},
            {QStringLiteral("f1"), f1}, {QStringLiteral("precision_defined"), tp + fp > 0},
            {QStringLiteral("recall_defined"), tp + fn > 0},
            {QStringLiteral("f1_defined"), tp + fp > 0 && tp + fn > 0 && precision + recall > 0.0},
            {QStringLiteral("tp"), tp}, {QStringLiteral("fp"), fp}, {QStringLiteral("fn"), fn}};
}

QString digestFor(const QVariantMap &value)
{
    const YAML::Node node = dltool::common::yaml::variantToYaml(value);
    YAML::Emitter emitter;
    emitter << node;
    return QStringLiteral("sha256:%1").arg(QString::fromLatin1(
        QCryptographicHash::hash(QByteArray(emitter.c_str()), QCryptographicHash::Sha256).toHex()));
}

QString classColor(const int class_id)
{
    static const QStringList palette = {QStringLiteral("#ef5350"), QStringLiteral("#42a5f5"),
                                        QStringLiteral("#66bb6a"), QStringLiteral("#ffa726"),
                                        QStringLiteral("#ab47bc"), QStringLiteral("#26c6da"),
                                        QStringLiteral("#8d6e63"), QStringLiteral("#78909c")};
    const int index = class_id >= 0 ? class_id % palette.size() : 0;
    return palette.at(index);
}

struct MatchPair
{
    int prediction{-1};
    int ground_truth{-1};
    double iou{0.0};
};

QList<MatchPair> greedyMatches(const QList<Prediction> &predictions, const QList<GroundTruth> &ground_truth,
                                const double threshold,
                                const std::shared_ptr<std::atomic_bool> &cancel_token = {})
{
    struct Candidate
    {
        int prediction{-1};
        int ground_truth{-1};
        double iou{0.0};
    };
    QList<Candidate> candidates;
    for (int prediction = 0; prediction < predictions.size(); ++prediction)
    {
        if (isCancelled(cancel_token))
            return {};
        for (int gt = 0; gt < ground_truth.size(); ++gt)
        {
            const double iou = (!predictions.at(prediction).box.valid() && !ground_truth.at(gt).box.valid())
                ? 1.0
                : intersectionOverUnion(predictions.at(prediction).box, ground_truth.at(gt).box);
            if (iou >= threshold)
                candidates.push_back({prediction, gt, iou});
        }
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate &lhs, const Candidate &rhs)
              {
                  if (lhs.iou != rhs.iou)
                      return lhs.iou > rhs.iou;
                  if (lhs.prediction != rhs.prediction)
                      return lhs.prediction < rhs.prediction;
                  return lhs.ground_truth < rhs.ground_truth;
              });
    QVector<bool> used_predictions(predictions.size(), false);
    QVector<bool> used_ground_truth(ground_truth.size(), false);
    QList<MatchPair> result;
    for (const Candidate &candidate : candidates)
    {
        if (used_predictions.at(candidate.prediction) || used_ground_truth.at(candidate.ground_truth))
            continue;
        used_predictions[candidate.prediction] = true;
        used_ground_truth[candidate.ground_truth] = true;
        result.push_back({candidate.prediction, candidate.ground_truth, candidate.iou});
    }
    return result;
}

/**
 * @brief 最大权一对一匹配。
 *
 * 以零权 dummy 边补齐方阵后运行 Hungarian 最小费用算法。无效边权为
 * 零，最终只接受达到 IoU 阈值的分配，因此未匹配的预测/GT 会保留为
 * FP/FN。该实现不依赖第三方矩阵库，适用于评估阶段的纯值记录。
 */
QList<MatchPair> hungarianMatches(const QList<Prediction> &predictions, const QList<GroundTruth> &ground_truth,
                                   const double threshold,
                                   const std::shared_ptr<std::atomic_bool> &cancel_token = {})
{
    const int size = std::max(predictions.size(), ground_truth.size());
    if (size <= 0)
        return {};
    QVector<QVector<double>> weight(size, QVector<double>(size, 0.0));
    for (int prediction = 0; prediction < predictions.size(); ++prediction)
    {
        if (isCancelled(cancel_token))
            return {};
        for (int gt = 0; gt < ground_truth.size(); ++gt)
        {
            const double iou = (!predictions.at(prediction).box.valid() && !ground_truth.at(gt).box.valid())
                ? 1.0
                : intersectionOverUnion(predictions.at(prediction).box, ground_truth.at(gt).box);
            if (iou >= threshold)
                weight[prediction][gt] = iou;
        }
    }

    // Hungarian algorithm for a square minimum-cost matrix. Costs are
    // negated IoU values so the returned assignment maximizes total IoU.
    const int n = size;
    QVector<double> u(n + 1), v(n + 1);
    QVector<int> p(n + 1), way(n + 1);
    for (int row = 1; row <= n; ++row)
    {
        if (isCancelled(cancel_token))
            return {};
        p[0] = row;
        int column0 = 0;
        QVector<double> minv(n + 1, std::numeric_limits<double>::max());
        QVector<bool> used(n + 1, false);
        do
        {
            used[column0] = true;
            const int row0 = p[column0];
            double delta = std::numeric_limits<double>::max();
            int column1 = 0;
            for (int column = 1; column <= n; ++column)
            {
                if (isCancelled(cancel_token))
                    return {};
                if (used[column])
                    continue;
                const double current = -weight[row0 - 1][column - 1] - u[row0] - v[column];
                if (current < minv[column])
                {
                    minv[column] = current;
                    way[column] = column0;
                }
                if (minv[column] < delta)
                {
                    delta = minv[column];
                    column1 = column;
                }
            }
            for (int column = 0; column <= n; ++column)
            {
                if (used[column])
                {
                    u[p[column]] += delta;
                    v[column] -= delta;
                }
                else
                    minv[column] -= delta;
            }
            column0 = column1;
        } while (p[column0] != 0);
        do
        {
            const int column1 = way[column0];
            p[column0] = p[column1];
            column0 = column1;
        } while (column0 != 0);
    }

    QList<MatchPair> result;
    for (int column = 1; column <= n; ++column)
    {
        if (isCancelled(cancel_token))
            return {};
        const int prediction = p[column] - 1;
        const int gt = column - 1;
        if (prediction < 0 || prediction >= predictions.size() || gt < 0 || gt >= ground_truth.size())
            continue;
        const double iou = weight[prediction][gt];
        if (iou >= threshold)
            result.push_back({prediction, gt, iou});
    }
    std::sort(result.begin(), result.end(), [](const MatchPair &lhs, const MatchPair &rhs)
              { return lhs.prediction < rhs.prediction; });
    return result;
}

QList<MatchPair> matchPredictions(const QList<Prediction> &predictions, const QList<GroundTruth> &ground_truth,
                                  const double threshold, const evaluation::MatchingStrategy strategy,
                                  const std::shared_ptr<std::atomic_bool> &cancel_token = {})
{
    if (strategy == evaluation::MatchingStrategy::HungarianIoU)
        return hungarianMatches(predictions, ground_truth, threshold, cancel_token);
    return greedyMatches(predictions, ground_truth, threshold, cancel_token);
}

struct OfficialEvaluationOutput
{
    bool available{false};
    QVariantMap metrics;
    QVariantList charts;
    QStringList chart_kinds;
    QVariantMap image_definition;
};

OfficialEvaluationOutput buildOfficialEvaluation(const evaluation::Method method, const QMap<qint64, Image> &images,
                                                 const double confidence, const double iou_threshold,
                                                 const evaluation::MatchingStrategy strategy,
                                                 const QVariantMap &diagnostic,
                                                 const std::shared_ptr<std::atomic_bool> &cancel_token = {})
{
    OfficialEvaluationOutput output;
    const bool detection = evaluation::hasInstanceMetrics(method);
    const bool anomaly = evaluation::isAnomaly(method);
    if (!detection && !anomaly)
        return output;

    if (anomaly)
    {
        QVariantList labels;
        QVariantList scores;
        for (const Image &image : images)
        {
            if (isCancelled(cancel_token))
                return {};
            labels.push_back(image.name);
            double score = 0.0;
            for (const Prediction &prediction : image.predictions)
                score = std::max(score, prediction.score);
            scores.push_back(score);
        }
        output.available = true;
        output.metrics = QVariantMap{{QStringLiteral("available"), true},
                                     {QStringLiteral("image"), diagnostic.value(QStringLiteral("image"))},
                                     {QStringLiteral("definition"), QStringLiteral("anomaly_score_threshold")}};
        output.image_definition = QVariantMap{{QStringLiteral("sample_unit"), QStringLiteral("image")},
                                              {QStringLiteral("aggregation"), QStringLiteral("micro")},
                                              {QStringLiteral("positive_definition"), QStringLiteral("score_above_threshold")},
                                              {QStringLiteral("has_image_metrics"), true}};
        output.charts.push_back(QVariantMap{{QStringLiteral("kind"), QStringLiteral("bar")},
                                            {QStringLiteral("chart_id"), QStringLiteral("anomaly_score_distribution")},
                                            {QStringLiteral("filter_kind"), QStringLiteral("image_score")},
                                            {QStringLiteral("title"), QString("异常分数分布")},
                                            {QStringLiteral("data"), QVariantMap{{QStringLiteral("labels"), labels},
                                                                                    {QStringLiteral("datasets"), QVariantList{
                                                                                        QVariantMap{{QStringLiteral("label"), QStringLiteral("score")},
                                                                                                    {QStringLiteral("data"), scores}}}}}},
                                            {QStringLiteral("options"), QVariantMap{{QStringLiteral("maintainAspectRatio"), false}}}});
        output.chart_kinds.push_back(QStringLiteral("bar"));
        return output;
    }

    struct Counts
    {
        qint64 tp{0};
        qint64 fp{0};
        qint64 fn{0};
    };
    auto countsAt = [&](const double threshold)
    {
        Counts counts;
        for (const Image &image : images)
        {
            if (isCancelled(cancel_token))
                return counts;
            QList<Prediction> predictions;
            for (const Prediction &prediction : image.predictions)
                if (prediction.score >= threshold)
                    predictions.push_back(prediction);
            const QList<MatchPair> pairs = matchPredictions(predictions, image.gt, iou_threshold, strategy,
                                                            cancel_token);
            QVector<bool> used_prediction(predictions.size(), false);
            QVector<bool> used_gt(image.gt.size(), false);
            for (const MatchPair &pair : pairs)
            {
                if (pair.prediction < 0 || pair.ground_truth < 0 || used_prediction.at(pair.prediction)
                    || used_gt.at(pair.ground_truth))
                    continue;
                used_prediction[pair.prediction] = true;
                used_gt[pair.ground_truth] = true;
                if (predictions.at(pair.prediction).class_id == image.gt.at(pair.ground_truth).class_id)
                    ++counts.tp;
                else
                {
                    ++counts.fp;
                    ++counts.fn;
                }
            }
            for (int index = 0; index < predictions.size(); ++index)
                if (!used_prediction.at(index))
                    ++counts.fp;
            for (int index = 0; index < image.gt.size(); ++index)
                if (!used_gt.at(index))
                    ++counts.fn;
        }
        return counts;
    };

    QList<double> thresholds;
    thresholds.push_back(1.0);
    for (const Image &image : images)
    {
        if (isCancelled(cancel_token))
            return {};
        for (const Prediction &prediction : image.predictions)
            if (std::isfinite(prediction.score))
                thresholds.push_back(std::clamp(prediction.score, 0.0, 1.0));
    }
    thresholds.push_back(confidence);
    std::sort(thresholds.begin(), thresholds.end(), std::greater<double>());
    QList<double> unique_thresholds;
    for (const double value : thresholds)
        if (unique_thresholds.isEmpty() || !qFuzzyCompare(unique_thresholds.back() + 1.0, value + 1.0))
            unique_thresholds.push_back(value);

    QVariantList recall_values;
    QVariantList precision_values;
    QVariantList threshold_labels;
    for (const double threshold : unique_thresholds)
    {
        if (isCancelled(cancel_token))
            return {};
        const Counts counts = countsAt(threshold);
        const QVariantMap metric = metricMap(counts.tp, counts.fp, counts.fn);
        threshold_labels.push_back(threshold);
        precision_values.push_back(metric.value(QStringLiteral("precision")));
        recall_values.push_back(metric.value(QStringLiteral("recall")));
    }
    const Counts work_point = countsAt(confidence);
    output.available = true;
    output.metrics = QVariantMap{{QStringLiteral("available"), true},
                                 {QStringLiteral("instance"), metricMap(work_point.tp, work_point.fp, work_point.fn)},
                                 {QStringLiteral("per_class"), diagnostic.value(QStringLiteral("instance")).toMap().value(QStringLiteral("per_class"))},
                                 {QStringLiteral("definition"), QStringLiteral("confidence_iou_work_point")}};
    output.image_definition = QVariantMap{{QStringLiteral("sample_unit"), QStringLiteral("image_class_presence")},
                                          {QStringLiteral("aggregation"), QStringLiteral("micro")},
                                          {QStringLiteral("positive_definition"), QStringLiteral("gt_or_pred_class_present")},
                                          {QStringLiteral("has_image_metrics"), true}};
    output.charts.push_back(QVariantMap{{QStringLiteral("kind"), QStringLiteral("line")},
                                         {QStringLiteral("chart_id"), QStringLiteral("precision_recall")},
                                         {QStringLiteral("filter_kind"), QStringLiteral("precision_recall")},
                                         {QStringLiteral("title"), QString("PR 曲线")},
                                        {QStringLiteral("data"), QVariantMap{{QStringLiteral("labels"), threshold_labels},
                                                                                {QStringLiteral("datasets"), QVariantList{
                                                                                    QVariantMap{{QStringLiteral("label"), QStringLiteral("Precision")},
                                                                                                {QStringLiteral("data"), precision_values}},
                                                                                    QVariantMap{{QStringLiteral("label"), QStringLiteral("Recall")},
                                                                                                {QStringLiteral("data"), recall_values}}}}}},
                                        {QStringLiteral("options"), QVariantMap{{QStringLiteral("maintainAspectRatio"), false}}}});
    output.chart_kinds.push_back(QStringLiteral("line"));
    return output;
}

} // namespace

EvaluationCapabilities ModelEvaluationService::capabilitiesForMethod(const evaluation::Method method)
{
    EvaluationCapabilities capabilities;
    capabilities.has_instance_metrics = evaluation::hasInstanceMetrics(method);
    capabilities.has_image_metrics = evaluation::hasImageMetrics(method);
    capabilities.has_confusion_matrix = evaluation::hasConfusionMatrix(method);
    capabilities.has_instance_events = evaluation::hasInstanceEvents(method);
    if (evaluation::isAnomaly(method))
        capabilities.chart_kinds = {QStringLiteral("bar")};
    else if (capabilities.has_instance_metrics)
        capabilities.chart_kinds = {QStringLiteral("bar"), QStringLiteral("line")};
    return capabilities;
}

bool ModelEvaluationService::validatePrediction(const QString &images_path, const QString &manifest_path,
                                                 int *image_count, int *prediction_count, QString *err_msg,
                                                 const QString &expected_model_uuid,
                                                 const QString &expected_task_uuid,
                                                 const QString &expected_method)
{
    QMap<qint64, Image> images;
    QFile image_file(images_path);
    if (!image_file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (err_msg)
            *err_msg = QString("打开 pred/images.txt 失败: %1").arg(image_file.errorString());
        return false;
    }
    if (QFileInfo(images_path).size() > kMaxEvaluationYamlBytes)
    {
        if (err_msg)
            *err_msg = QString("pred/images.txt 超过大小限制");
        return false;
    }
    QTextStream image_stream(&image_file);
    bool first = true;
    QSet<qint64> listed_ids;
    while (!image_stream.atEnd())
    {
        QString line = image_stream.readLine();
        if (line.trimmed().isEmpty())
            continue;
        if (first && !line.isEmpty() && line.at(0) == QChar(0xfeff))
            line.remove(0, 1);
        if (first)
        {
            first = false;
            if (line.trimmed().compare(QStringLiteral("image_id,image_path"), Qt::CaseInsensitive) == 0)
                continue;
        }
        bool csv_valid = false;
        const QList<QString> fields = parseCsvLine(line, &csv_valid);
        if (!csv_valid || fields.size() != 2)
        {
            if (err_msg)
                *err_msg = QString("pred/images.txt 行格式无效: %1").arg(line);
            return false;
        }
        bool ok = false;
        const qint64 id = fields.at(0).trimmed().toLongLong(&ok);
        if (!ok || id < 0)
        {
            if (err_msg)
                *err_msg = QString("pred/images.txt image_id 无效: %1").arg(fields.at(0));
            return false;
        }
        const QString path = fields.at(1).trimmed();
        if (path.isEmpty())
        {
            if (err_msg)
                *err_msg = QString("pred/images.txt image_path 为空: %1").arg(id);
            return false;
        }
        if (listed_ids.contains(id))
        {
            if (err_msg)
                *err_msg = QString("pred/images.txt image_id 重复: %1").arg(id);
            return false;
        }
        listed_ids.insert(id);
        images.insert(id, Image{id, -1, path, QFileInfo(path).fileName(), 0, 0, {}, {}});
    }
    if (images.isEmpty())
    {
        if (err_msg)
            *err_msg = QString("pred/images.txt 没有有效图像");
        return false;
    }
    int count = 0;
    if (!loadPredictions(manifest_path, images, &count, {}, err_msg,
                         expected_model_uuid, expected_task_uuid, expected_method))
        return false;
    if (image_count)
        *image_count = images.size();
    if (prediction_count)
        *prediction_count = count;
    return true;
}

bool ModelEvaluationService::evaluate(const ModelEvaluationOptions &options, ModelEvaluationResult *result,
                                      QString *err_msg)
{
    if (isCancelled(options.cancel_token))
    {
        if (err_msg)
            *err_msg = QString("评估已取消");
        return false;
    }
    if (options.dataset_manifest_path.isEmpty() || options.prediction_manifest_path.isEmpty()
        || options.prediction_images_path.isEmpty() || options.evaluation_dir.isEmpty()
        || options.report_path.isEmpty())
    {
        if (err_msg)
            *err_msg = QString("评估路径参数不完整");
        return false;
    }

    QMap<qint64, Image> images;
    if (!loadImages(options.prediction_images_path, options.dataset_manifest_path, images, options.cancel_token,
                    err_msg))
        return false;
    int prediction_count = 0;
    if (!loadPredictions(options.prediction_manifest_path, images, &prediction_count, options.cancel_token, err_msg,
                         options.model_uuid, options.test_task_uuid, evaluation::methodKey(options.method)))
        return false;
    for (auto it = images.begin(); it != images.end();)
    {
        if (sourceImageExists(it->path, options.dataset_manifest_path))
            ++it;
        else
            it = images.erase(it);
    }
    if (images.isEmpty())
    {
        if (err_msg)
            *err_msg = QString("测试数据集没有可用图像");
        return false;
    }
    prediction_count = 0;
    for (const Image &image : images)
        prediction_count += image.predictions.size();
    if (isCancelled(options.cancel_token))
    {
        if (err_msg)
            *err_msg = QString("评估已取消");
        return false;
    }
    const bool anomaly_method = evaluation::isAnomaly(options.method);
    QMap<int, QString> classes;
    if (anomaly_method)
    {
        // Anomaly evaluation is image-level binary classification.  GOOD is
        // the implicit negative class because normal samples have no GT label.
        classes.insert(0, QStringLiteral("GOOD"));
        classes.insert(1, QStringLiteral("Anomaly"));
    }
    else
    {
        for (const Image &image : images)
        {
            if (isCancelled(options.cancel_token))
            {
                if (err_msg)
                    *err_msg = QString("评估已取消");
                return false;
            }
            for (const GroundTruth &gt : image.gt)
                classes.insert(gt.class_id, gt.class_name.isEmpty() ? QString::number(gt.class_id) : gt.class_name);
        }
        for (const Image &image : images)
        {
            if (isCancelled(options.cancel_token))
            {
                if (err_msg)
                    *err_msg = QString("评估已取消");
                return false;
            }
            for (const Prediction &prediction : image.predictions)
                classes.insert(prediction.class_id,
                               prediction.class_name.isEmpty() ? QString::number(prediction.class_id)
                                                               : prediction.class_name);
        }
        classes.remove(-1);
    }

    struct Counts
    {
        qint64 tp{0};
        qint64 fp{0};
        qint64 fn{0};
    } overall, image_counts;
    QMap<int, Counts> per_class;
    QMap<QString, qint64> matrix;
    QVariantList event_records;
    const QString dataset_manifest_root = QFileInfo(options.dataset_manifest_path).absolutePath();
    const QString prediction_task_root
        = QFileInfo(QFileInfo(options.prediction_manifest_path).absolutePath()).absoluteDir().absolutePath();
    const auto incrementMatrix = [&matrix](const QString &row, const QString &column, qint64 count = 1)
    { matrix[row + QLatin1Char('\x1f') + column] += count; };

    for (auto image_it = images.begin(); image_it != images.end(); ++image_it)
    {
        if (isCancelled(options.cancel_token))
        {
            if (err_msg)
                *err_msg = QString("评估已取消");
            return false;
        }
        Image &image = image_it.value();
        QList<Prediction> predictions;
        for (const Prediction &prediction : image.predictions)
            if (prediction.score >= options.confidence_threshold)
                predictions.push_back(prediction);
        std::sort(predictions.begin(), predictions.end(), [](const Prediction &a, const Prediction &b)
                  { return a.score > b.score; });
        QVector<bool> used_gt(image.gt.size(), false);
        QVector<bool> used_pred(predictions.size(), false);
        const QList<MatchPair> pairs = matchPredictions(predictions, image.gt, options.iou_threshold,
                                                         options.matching_strategy, options.cancel_token);
        const auto appendEvent = [&](const evaluation::Status status, const GroundTruth *gt, const Prediction *pred,
                                     double iou)
        {
            const QVariantMap crop = cropBounds(gt ? gt->bounds : QVariantMap{},
                                                 pred ? pred->bounds : QVariantMap{},
                                                 image.width, image.height);
            const QVariantMap viewport = crop.isEmpty() && image.width > 0 && image.height > 0
                ? boxMap(Box{0.0, 0.0, static_cast<double>(image.width), static_cast<double>(image.height)})
                : crop;
            const QVariantMap gt_geometry = gt ? gt->geometry : QVariantMap{};
            const QVariantMap pred_geometry = pred ? pred->geometry : QVariantMap{};
            QVariantMap event{{QStringLiteral("event_uuid"),
                               QStringLiteral("%1-%2").arg(image.id).arg(event_records.size() + 1)},
                              {QStringLiteral("image_id"), image.id},
                              {QStringLiteral("status"), evaluation::statusKey(status)},
                              {QStringLiteral("score"), pred ? pred->score : 0.0},
                              {QStringLiteral("iou"), iou},
                              {QStringLiteral("gt_label_id"), gt ? gt->label_id : -1},
                              {QStringLiteral("gt_class_id"), gt ? gt->class_id : -1},
                              {QStringLiteral("gt_class_name"), gt ? gt->class_name : QString()},
                              {QStringLiteral("gt_geometry"), gt_geometry},
                              {QStringLiteral("pred_instance_id"), pred ? pred->prediction_id : QString()},
                              {QStringLiteral("pred_class_id"), pred ? pred->class_id : -1},
                              {QStringLiteral("pred_class_name"), pred ? pred->class_name : QString()},
                              {QStringLiteral("pred_geometry"), pred_geometry},
                              {QStringLiteral("crop_bounds"), viewport},
                               {QStringLiteral("gt_overlay_bounds"),
                                normalizedOverlayBounds(gt ? gt->bounds : QVariantMap{}, viewport)},
                               {QStringLiteral("pred_overlay_bounds"),
                                normalizedOverlayBounds(pred ? pred->bounds : QVariantMap{}, viewport)},
                               {QStringLiteral("gt_overlay_points"),
                                normalizedOverlayPoints(gt_geometry, viewport)},
                               {QStringLiteral("pred_overlay_points"),
                                normalizedOverlayPoints(pred_geometry, viewport)},
                               {QStringLiteral("gt_mask_url"), maskUrl(gt_geometry, dataset_manifest_root)},
                               {QStringLiteral("pred_mask_url"), maskUrl(pred_geometry, prediction_task_root)}};
            event_records.push_back(event);
        };

        for (const MatchPair &pair : pairs)
        {
            if (isCancelled(options.cancel_token))
            {
                if (err_msg)
                    *err_msg = QString("评估已取消");
                return false;
            }
            if (used_gt.at(pair.ground_truth) || used_pred.at(pair.prediction))
                continue;
            used_gt[pair.ground_truth] = true;
            used_pred[pair.prediction] = true;
            const GroundTruth &gt = image.gt.at(pair.ground_truth);
            const Prediction &pred = predictions.at(pair.prediction);
            const bool same_class = gt.class_id == pred.class_id;
            if (same_class)
            {
                ++overall.tp;
                ++per_class[pred.class_id].tp;
                incrementMatrix(QString::number(pred.class_id), QString::number(gt.class_id));
                appendEvent(evaluation::Status::TruePositive, &gt, &pred, pair.iou);
            }
            else
            {
                ++overall.fp;
                ++overall.fn;
                ++per_class[pred.class_id].fp;
                ++per_class[gt.class_id].fn;
                incrementMatrix(QString::number(pred.class_id), QString::number(gt.class_id));
                appendEvent(evaluation::Status::ClassMismatch, &gt, &pred, pair.iou);
            }
        }
        for (int p = 0; p < predictions.size(); ++p)
        {
            if (used_pred.at(p))
                continue;
            const Prediction &pred = predictions.at(p);
            ++overall.fp;
            ++per_class[pred.class_id].fp;
            incrementMatrix(QString::number(pred.class_id), QStringLiteral("FP"));
            appendEvent(evaluation::Status::FalsePositive, nullptr, &pred, 0.0);
        }
        for (int g = 0; g < image.gt.size(); ++g)
        {
            if (used_gt.at(g))
                continue;
            const GroundTruth &gt = image.gt.at(g);
            ++overall.fn;
            ++per_class[gt.class_id].fn;
            incrementMatrix(QStringLiteral("FN"), QString::number(gt.class_id));
            appendEvent(evaluation::Status::FalseNegative, &gt, nullptr, 0.0);
        }

        // 图像级指标按类别 presence 统计，而不是只要图像同时有 GT/PRED
        // 就记为 TP；这样类别错误和多类别图像的 FP/FN 不会被吞掉。
        QSet<int> image_gt_classes;
        QSet<int> image_pred_classes;
        for (const GroundTruth &gt : image.gt)
            if (gt.class_id >= 0)
                image_gt_classes.insert(gt.class_id);
        for (const Prediction &prediction : predictions)
            if (prediction.class_id >= 0)
                image_pred_classes.insert(prediction.class_id);
        if (image_gt_classes.isEmpty() && image_pred_classes.isEmpty())
        {
            const bool has_gt = !image.gt.isEmpty();
            const bool has_pred = !predictions.isEmpty();
            if (has_gt && has_pred)
                ++image_counts.tp;
            else if (has_pred)
                ++image_counts.fp;
            else if (has_gt)
                ++image_counts.fn;
        }
        else
        {
            for (const int class_id : image_pred_classes)
            {
                if (image_gt_classes.contains(class_id))
                    ++image_counts.tp;
                else
                    ++image_counts.fp;
            }
            for (const int class_id : image_gt_classes)
                if (!image_pred_classes.contains(class_id))
                    ++image_counts.fn;
        }
    }

    QVariantList per_class_metrics;
    QVariantList image_records;
    for (const Image &image : images)
    {
        if (isCancelled(options.cancel_token))
        {
            if (err_msg)
                *err_msg = QString("评估已取消");
            return false;
        }
        QVariantList gt_instances;
        for (const GroundTruth &gt : image.gt)
        {
            gt_instances.push_back(QVariantMap{{QStringLiteral("label_id"), gt.label_id},
                                               {QStringLiteral("class_id"), gt.class_id},
                                               {QStringLiteral("class_name"), gt.class_name},
                                               {QStringLiteral("geometry"), gt.geometry}});
        }
        QVariantList prediction_instances;
        for (const Prediction &prediction : image.predictions)
        {
            prediction_instances.push_back(QVariantMap{{QStringLiteral("prediction_id"), prediction.prediction_id},
                                                        {QStringLiteral("class_id"), prediction.class_id},
                                                        {QStringLiteral("class_name"), prediction.class_name},
                                                        {QStringLiteral("score"), prediction.score},
                                                        {QStringLiteral("geometry"), prediction.geometry}});
        }
        image_records.push_back(QVariantMap{{QStringLiteral("image_id"), image.id},
                                            {QStringLiteral("dataset_id"), image.dataset_id},
                                            {QStringLiteral("image_name"), image.name},
                                            {QStringLiteral("image_path"), image.path},
                                            {QStringLiteral("image_width"), image.width},
                                            {QStringLiteral("image_height"), image.height},
                                            {QStringLiteral("gt_instances"), gt_instances},
                                            {QStringLiteral("predictions"), prediction_instances}});
    }
    QVariantList chart_labels;
    QVariantList precision_values;
    QVariantList recall_values;
    QVariantList f1_values;
    for (auto it = classes.cbegin(); it != classes.cend(); ++it)
    {
        if (isCancelled(options.cancel_token))
        {
            if (err_msg)
                *err_msg = QString("评估已取消");
            return false;
        }
        const Counts counts = per_class.value(it.key());
        QVariantMap metric = metricMap(counts.tp, counts.fp, counts.fn);
        metric.insert(QStringLiteral("class_id"), it.key());
        metric.insert(QStringLiteral("class_name"), it.value());
        per_class_metrics.push_back(metric);
        chart_labels.push_back(it.value());
        precision_values.push_back(metric.value(QStringLiteral("precision")));
        recall_values.push_back(metric.value(QStringLiteral("recall")));
        f1_values.push_back(metric.value(QStringLiteral("f1")));
    }

    QVariantList class_catalog;
    for (auto it = classes.cbegin(); it != classes.cend(); ++it)
    {
        if (isCancelled(options.cancel_token))
        {
            if (err_msg)
                *err_msg = QString("评估已取消");
            return false;
        }
        class_catalog.push_back(QVariantMap{{QStringLiteral("id"), it.key()},
                                            {QStringLiteral("name"), it.value()},
                                            {QStringLiteral("color"), classColor(it.key())}});
    }

    if (anomaly_method)
    {
        // Use one binary outcome per image so normal images contribute GOOD /
        // GOOD true negatives even though they have no GT instance event.
        matrix.clear();
        for (const Image &image : images)
        {
            const bool ground_truth_anomaly = std::any_of(
                image.gt.cbegin(), image.gt.cend(),
                [](const GroundTruth &ground_truth) { return ground_truth.class_id == 1; });
            const bool predicted_anomaly = std::any_of(
                image.predictions.cbegin(), image.predictions.cend(),
                [&options](const Prediction &prediction)
                { return prediction.class_id == 1 && prediction.score >= options.confidence_threshold; });
            incrementMatrix(predicted_anomaly ? QStringLiteral("1") : QStringLiteral("0"),
                           ground_truth_anomaly ? QStringLiteral("1") : QStringLiteral("0"));
        }
    }

    QVariantList matrix_cells;
    QMap<int, qint64> pred_totals;
    QMap<int, qint64> gt_totals;
    qint64 unmatched_fp = 0;
    qint64 unmatched_fn = 0;
    for (auto it = matrix.cbegin(); it != matrix.cend(); ++it)
    {
        const QList<QString> keys = it.key().split(QLatin1Char('\x1f'));
        if (keys.size() != 2)
            continue;
        if (keys.at(0) == QStringLiteral("FN"))
            unmatched_fn += it.value();
        else
            pred_totals[keys.at(0).toInt()] += it.value();
        if (keys.at(1) == QStringLiteral("FP"))
            unmatched_fp += it.value();
        else
            gt_totals[keys.at(1).toInt()] += it.value();
    }
    const auto appendCell = [&](const QString &row, const QString &column, qint64 count,
                                const evaluation::CellKind kind,
                                bool selectable, bool diagonal, bool error)
    {
        const bool row_fn = row == QStringLiteral("FN");
        const bool row_total = row == QStringLiteral("TOTAL");
        const bool column_fp = column == QStringLiteral("FP");
        const bool column_total = column == QStringLiteral("TOTAL");
        const int row_id = row_fn || row_total ? -1 : row.toInt();
        const int column_id = column_fp || column_total ? -1 : column.toInt();
        const QString total_label = QString("合计");
        const QString row_label = row_fn ? QStringLiteral("FN") : (row_total ? total_label : classes.value(row_id));
        const QString column_label = column_fp ? QStringLiteral("FP")
                                               : (column_total ? total_label : classes.value(column_id));
        matrix_cells.push_back(QVariantMap{{QStringLiteral("row_key"), row},
                                           {QStringLiteral("column_key"), column},
                                           {QStringLiteral("row_label"), row_label},
                                           {QStringLiteral("column_label"), column_label},
                                           {QStringLiteral("row_class_id"), row_id},
                                           {QStringLiteral("column_class_id"), column_id},
                                           {QStringLiteral("count"), count},
                                           {QStringLiteral("cell_kind"), evaluation::cellKindKey(kind)},
                                           {QStringLiteral("selectable"), selectable},
                                           {QStringLiteral("is_diagonal"), diagonal},
                                           {QStringLiteral("is_error"), error}});
    };
    for (auto row_it = classes.cbegin(); row_it != classes.cend(); ++row_it)
    {
        const QString row = QString::number(row_it.key());
        for (auto column_it = classes.cbegin(); column_it != classes.cend(); ++column_it)
        {
            const QString column = QString::number(column_it.key());
            const bool diagonal = row_it.key() == column_it.key();
            appendCell(row, column, matrix.value(row + QLatin1Char('\x1f') + column),
                       diagonal ? evaluation::CellKind::Match : evaluation::CellKind::ClassMismatch, true, diagonal,
                       !diagonal);
        }
        appendCell(row, QStringLiteral("FP"), matrix.value(row + QLatin1Char('\x1f') + QStringLiteral("FP")),
                   evaluation::CellKind::FalsePositive, true, false, true);
        appendCell(row, QStringLiteral("TOTAL"), pred_totals.value(row_it.key()), evaluation::CellKind::PredTotal, true,
                   false, false);
    }
    for (auto column_it = classes.cbegin(); column_it != classes.cend(); ++column_it)
    {
        const QString column = QString::number(column_it.key());
        appendCell(QStringLiteral("FN"), column, matrix.value(QStringLiteral("FN") + QLatin1Char('\x1f') + column),
                   evaluation::CellKind::FalseNegative, true, false, true);
    }
    appendCell(QStringLiteral("FN"), QStringLiteral("FP"), 0, evaluation::CellKind::NotApplicable, false, false, false);
    appendCell(QStringLiteral("FN"), QStringLiteral("TOTAL"), unmatched_fn, evaluation::CellKind::FalseNegativeTotal,
               true, false, true);
    for (auto column_it = classes.cbegin(); column_it != classes.cend(); ++column_it)
    {
        const QString column = QString::number(column_it.key());
        appendCell(QStringLiteral("TOTAL"), column, gt_totals.value(column_it.key()), evaluation::CellKind::GtTotal, true,
                   false, false);
    }
    appendCell(QStringLiteral("TOTAL"), QStringLiteral("FP"), unmatched_fp, evaluation::CellKind::FalsePositiveTotal,
               true, false, true);
    appendCell(QStringLiteral("TOTAL"), QStringLiteral("TOTAL"), anomaly_method ? images.size() : event_records.size(),
               evaluation::CellKind::All, true, false, false);

    const QVariantMap diagnostic = {
        {QStringLiteral("instance"), QVariantMap{{QStringLiteral("overall"), metricMap(overall.tp, overall.fp, overall.fn)},
                                                   {QStringLiteral("per_class"), per_class_metrics}}},
        {QStringLiteral("image"), metricMap(image_counts.tp, image_counts.fp, image_counts.fn)}};
    const OfficialEvaluationOutput official = buildOfficialEvaluation(options.method, images,
                                                                        options.confidence_threshold,
                                                                        options.iou_threshold,
                                                                         options.matching_strategy, diagnostic,
                                                                         options.cancel_token);
    if (isCancelled(options.cancel_token))
    {
        if (err_msg)
            *err_msg = QString("评估已取消");
        return false;
    }
    const EvaluationCapabilities capabilities = capabilitiesForMethod(options.method);
    const QVariantMap evaluation_config = {{QStringLiteral("schema_version"), 1},
                                           {QStringLiteral("model_uuid"), options.model_uuid},
                                           {QStringLiteral("test_task_uuid"), options.test_task_uuid},
                                           {QStringLiteral("method"), evaluation::methodKey(options.method)},
                                           {QStringLiteral("inference_digest"), options.evaluation_config.value(QStringLiteral("inference_digest"))},
                                           {QStringLiteral("input_data_digest"), options.evaluation_config.value(QStringLiteral("input_data_digest"))},
                                           {QStringLiteral("ground_truth_digest"), options.evaluation_config.value(QStringLiteral("ground_truth_digest"))},
                                           {QStringLiteral("ground_truth_revision"), options.evaluation_config.value(QStringLiteral("ground_truth_revision"))},
                                           {QStringLiteral("image_list_digest"), options.evaluation_config.value(QStringLiteral("image_list_digest"))},
                                           {QStringLiteral("input_digest"), options.evaluation_config.value(QStringLiteral("input_digest"))},
                                           {QStringLiteral("evaluation_digest"), digestFor(options.evaluation_config)},
                                           {QStringLiteral("confidence_threshold"), options.confidence_threshold},
                                           {QStringLiteral("iou_threshold"), options.iou_threshold},
                                           {QStringLiteral("matching_strategy"),
                                            evaluation::matchingStrategyKey(options.matching_strategy)}};
    const QString evaluation_digest = evaluation_config.value(QStringLiteral("evaluation_digest")).toString();
    QVariantList charts = {QVariantMap{{QStringLiteral("kind"), QStringLiteral("bar")},
                                             {QStringLiteral("chart_id"), QStringLiteral("per_class_metrics")},
                                             {QStringLiteral("filter_kind"), QStringLiteral("per_class_metrics")},
                                             {QStringLiteral("title"), QString("按类别指标")},
                                             {QStringLiteral("data"), QVariantMap{{QStringLiteral("labels"), chart_labels},
                                                                                     {QStringLiteral("datasets"), QVariantList{
                                                                                         QVariantMap{{QStringLiteral("label"), QStringLiteral("Precision")}, {QStringLiteral("data"), precision_values}},
                                                                                         QVariantMap{{QStringLiteral("label"), QStringLiteral("Recall")}, {QStringLiteral("data"), recall_values}},
                                                                                         QVariantMap{{QStringLiteral("label"), QStringLiteral("F1")}, {QStringLiteral("data"), f1_values}}}}}},
                                             {QStringLiteral("options"), QVariantMap{{QStringLiteral("maintainAspectRatio"), false}}}}};
    for (const QVariant &chart : official.charts)
        charts.push_back(chart);

    const QVariantMap report = {
        {QStringLiteral("schema_version"), evaluation::kReportSchemaVersion},
        {QStringLiteral("model_uuid"), options.model_uuid},
        {QStringLiteral("test_task_uuid"), options.test_task_uuid},
        {QStringLiteral("method"), evaluation::methodKey(options.method)},
        {QStringLiteral("primary_metric_set"), official.available
                ? evaluation::metricSetKey(evaluation::MetricSet::Official)
                : evaluation::metricSetKey(evaluation::MetricSet::Diagnostic)},
        {QStringLiteral("inference_digest"), options.evaluation_config.value(QStringLiteral("inference_digest"))},
        {QStringLiteral("input_data_digest"), options.evaluation_config.value(QStringLiteral("input_data_digest"))},
        {QStringLiteral("evaluation_digest"), evaluation_digest},
        {QStringLiteral("ground_truth_digest"), options.evaluation_config.value(QStringLiteral("ground_truth_digest"))},
        {QStringLiteral("ground_truth_revision"), options.evaluation_config.value(QStringLiteral("ground_truth_revision"))},
        {QStringLiteral("image_list_digest"), options.evaluation_config.value(QStringLiteral("image_list_digest"))},
        {QStringLiteral("input_digest"), options.evaluation_config.value(QStringLiteral("input_digest"))},
        {QStringLiteral("evaluated_at"), QDateTime::currentSecsSinceEpoch()},
        {QStringLiteral("evaluation_config"), QVariantMap{{QStringLiteral("confidence_threshold"), options.confidence_threshold},
                                                           {QStringLiteral("iou_threshold"), options.iou_threshold},
                                                           {QStringLiteral("matching_strategy"),
                                                            evaluation::matchingStrategyKey(options.matching_strategy)}}},
        {QStringLiteral("class_catalog"), class_catalog},
        {QStringLiteral("diagnostic_metrics"), diagnostic},
        {QStringLiteral("official_metrics"), official.available ? official.metrics
                                                                   : QVariantMap{{QStringLiteral("available"), false}}},
        {QStringLiteral("image_metric_definition"), official.available && !official.image_definition.isEmpty()
                ? official.image_definition
                : QVariantMap{{QStringLiteral("sample_unit"), QStringLiteral("image_presence")},
                              {QStringLiteral("aggregation"), QStringLiteral("micro")},
                              {QStringLiteral("positive_definition"), QStringLiteral("gt_or_pred_class_present")},
                              {QStringLiteral("has_image_metrics"), true}}},
        {QStringLiteral("capabilities"), QVariantMap{{QStringLiteral("has_instance_metrics"), capabilities.has_instance_metrics},
                                                         {QStringLiteral("has_image_metrics"), capabilities.has_image_metrics},
                                                         {QStringLiteral("has_confusion_matrix"), capabilities.has_confusion_matrix},
                                                         {QStringLiteral("has_instance_events"), capabilities.has_instance_events},
                                                         {QStringLiteral("chart_kinds"), capabilities.chart_kinds}}},
        {QStringLiteral("confusion_matrix"), QVariantMap{{QStringLiteral("cells"), matrix_cells}}},
        {QStringLiteral("charts"), charts},
        {QStringLiteral("image_records"), image_records},
        {QStringLiteral("instance_records"), event_records},
        {QStringLiteral("image_list"), QStringLiteral("../pred/images.txt")},
        {QStringLiteral("prediction_manifest"), QStringLiteral("../pred/manifest.yaml")},
        {QStringLiteral("dataset_manifest"),
         QDir(options.evaluation_dir).relativeFilePath(options.dataset_manifest_path)},
        {QStringLiteral("image_count"), images.size()},
        {QStringLiteral("prediction_count"), prediction_count},
        {QStringLiteral("event_count"), event_records.size()},
    };
    if (!QDir().mkpath(options.evaluation_dir))
    {
        if (err_msg)
            *err_msg = QString("创建评估目录失败: %1").arg(options.evaluation_dir);
        return false;
    }
    QString error;
    if (!dltool::common::yaml::writeFileAtomic(options.report_path,
                                                  dltool::common::yaml::variantToYaml(report), &error,
                                                  QString("打开评估报告失败"), QString("生成评估报告失败"),
                                                  QString("提交评估报告失败")))
    {
        if (err_msg)
            *err_msg = error;
        return false;
    }
    if (result)
    {
        result->image_count = images.size();
        result->prediction_count = prediction_count;
        result->event_count = event_records.size();
        result->evaluation_digest = evaluation_config.value(QStringLiteral("evaluation_digest")).toString();
    }
    return true;
}

} // namespace dltool::model
