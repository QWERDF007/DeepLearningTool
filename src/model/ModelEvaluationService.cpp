#include "model/ModelEvaluationService.h"

#include "data/DatasetIO.h"
#include "data/LabelData.h"
#include "database/DataBase.h"
#include "model/ModelDatasetSelection.h"
#include "database/ModelTaskDataBase.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaType>
#include <QMap>
#include <QSet>
#include <QTextStream>
#include <QUrl>
#include <QVariantList>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace dltool::model {

namespace {

// Prediction artifacts are user-/framework-produced input.  Bound both the
// document size and sequence cardinality before JSON is converted to
// QVariant values, so malformed prediction data cannot exhaust the GUI process.
constexpr qint64 kMaxEvaluationFileBytes = 256LL * 1024LL * 1024LL;
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

int mapInt(const QVariantMap &map, const QString &key, int fallback = -1)
{
    bool ok = false;
    const int value = map.value(key).toInt(&ok);
    return ok ? value : fallback;
}

bool pathWithin(const QString &root, const QString &path)
{
    const QString clean_root = QDir::fromNativeSeparators(QFileInfo(root).absoluteFilePath());
    const QString clean_path = QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath());
    return !clean_root.isEmpty() && !clean_path.isEmpty()
        && (clean_path.compare(clean_root, Qt::CaseInsensitive) == 0
            || clean_path.startsWith(clean_root + QLatin1Char('/'), Qt::CaseInsensitive));
}

bool sourceImageExists(const QString &path, const QString &dataset_root)
{
    QFileInfo image(path);
    if (!image.isAbsolute() && !image.exists() && !dataset_root.isEmpty())
        image = QFileInfo(QDir(dataset_root), path);
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
    return {{evaluation::fieldName(evaluation::Field::X), box.x},
            {evaluation::fieldName(evaluation::Field::Y), box.y},
            {evaluation::fieldName(evaluation::Field::Width), box.w},
            {evaluation::fieldName(evaluation::Field::Height), box.h}};
}

QString geometryType(const QVariantMap &geometry)
{
    return mapString(geometry, evaluation::fieldName(evaluation::Field::Type)).trimmed().toLower();
}

QVariantMap canonicalGeometry(const QVariantMap &source, const Box &box)
{
    if (source.isEmpty() && !box.valid())
        return {};
    QVariantMap geometry = source;
    const QString type = geometryType(geometry);
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
        geometry.insert(evaluation::fieldName(evaluation::Field::Bounds), boxMap(box));
        if (geometryType(geometry) == QStringLiteral("bbox")
            || geometryType(geometry) == QStringLiteral("box")
            || geometryType(geometry) == QStringLiteral("rectangle"))
            geometry.insert(evaluation::fieldName(evaluation::Field::Values), QVariantList{box.x, box.y, box.w, box.h});
    }
    return geometry;
}

bool readBox(const QVariantMap &value, Box &box);

QVariantList normalizedOverlayPoints(const QVariantMap &geometry, const QVariantMap &crop)
{
    Box viewport;
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
    const QString artifact = mapString(geometry, evaluation::fieldName(evaluation::Field::ArtifactPath)).trimmed();
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
    if (source.contains(evaluation::fieldName(evaluation::Field::Bounds)))
        source = source.value(evaluation::fieldName(evaluation::Field::Bounds)).toMap();
    if (source.contains(QStringLiteral("bbox")))
        source = source.value(QStringLiteral("bbox")).toMap();
    if (source.contains(evaluation::fieldName(evaluation::Field::Values)))
    {
        const QVariantList values = source.value(evaluation::fieldName(evaluation::Field::Values)).toList();
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
    if (source.contains(evaluation::fieldName(evaluation::Field::X))
        && source.contains(evaluation::fieldName(evaluation::Field::Y))
        && source.contains(evaluation::fieldName(evaluation::Field::Width))
        && source.contains(evaluation::fieldName(evaluation::Field::Height)))
    {
        bool x_ok = false;
        bool y_ok = false;
        bool w_ok = false;
        bool h_ok = false;
        const double x = source.value(evaluation::fieldName(evaluation::Field::X)).toDouble(&x_ok);
        const double y = source.value(evaluation::fieldName(evaluation::Field::Y)).toDouble(&y_ok);
        const double w = source.value(evaluation::fieldName(evaluation::Field::Width)).toDouble(&w_ok);
        const double h = source.value(evaluation::fieldName(evaluation::Field::Height)).toDouble(&h_ok);
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
        bool cx_ok = false;
        bool cy_ok = false;
        bool w_ok = false;
        bool h_ok = false;
        const double cx = source.value(evaluation::fieldName(evaluation::Field::Cx)).toDouble(&cx_ok);
        const double cy = source.value(evaluation::fieldName(evaluation::Field::Cy)).toDouble(&cy_ok);
        const double w = source.value(evaluation::fieldName(evaluation::Field::Width)).toDouble(&w_ok);
        const double h = source.value(evaluation::fieldName(evaluation::Field::Height)).toDouble(&h_ok);
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
    const QString type = mapString(geometry, evaluation::fieldName(evaluation::Field::Type)).trimmed().toLower();
    const QString coordinate_system
        = mapString(geometry, evaluation::fieldName(evaluation::Field::CoordinateSystem)).trimmed().toLower();
    const QString format = mapString(geometry, evaluation::fieldName(evaluation::Field::Format)).trimmed().toLower();
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
        const QString artifact = mapString(geometry, evaluation::fieldName(evaluation::Field::ArtifactPath)).trimmed();
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

bool readImageList(const QString &path, QList<QPair<qint64, QString>> &rows,
                  const std::shared_ptr<std::atomic_bool> &cancel_token, QString *err_msg)
{
    rows.clear();
    const QFileInfo file_info(path);
    if (!file_info.exists() || !file_info.isFile())
    {
        if (err_msg)
            *err_msg = QString("图像文件列表不存在: %1").arg(path);
        return false;
    }
    if (file_info.size() > kMaxEvaluationFileBytes)
    {
        if (err_msg)
            *err_msg = QString("图像文件列表超过大小限制: %1").arg(path);
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (err_msg)
            *err_msg = QString("打开图像文件列表失败: %1").arg(file.errorString());
        return false;
    }
    QTextStream stream(&file);
    QSet<qint64> ids;
    bool first = true;
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
        bool csv_valid = false;
        const QList<QString> fields = parseCsvLine(line, &csv_valid);
        if (first && csv_valid && fields.size() == 2
            && fields.at(0).trimmed().compare(QString("image_id"), Qt::CaseInsensitive) == 0)
        {
            first = false;
            continue;
        }
        first = false;
        if (!csv_valid || fields.size() != 2)
        {
            if (err_msg)
                *err_msg = QString("图像文件列表行格式无效: %1").arg(line);
            return false;
        }
        bool ok = false;
        const qint64 image_id = fields.at(0).trimmed().toLongLong(&ok);
        const QString image_path = fields.at(1).trimmed();
        if (!ok || image_id < 0 || image_path.isEmpty())
            continue;
        if (ids.contains(image_id))
            continue;
        ids.insert(image_id);
        rows.push_back({image_id, image_path});
        if (rows.size() > static_cast<int>(kMaxEvaluationRecords))
        {
            if (err_msg)
                *err_msg = QString("图像文件列表记录数量超过限制");
            return false;
        }
    }
    if (rows.isEmpty())
    {
        if (err_msg)
            *err_msg = QString("图像文件列表没有有效图像: %1").arg(path);
        return false;
    }
    return true;
}

struct SourceImage
{
    qint64 id{-1};
    qint64 dataset_id{-1};
    QString path;
    std::vector<uint8_t> extra_data;
};

struct SourceLabel
{
    qint64 id{-1};
    qint64 image_id{-1};
    qint64 class_id{-1};
    std::vector<uint8_t> data;
};

struct SourceClass
{
    QString name;
    QString group;
};

QString normalizedLabelClassGroup(const QString &group)
{
    const QString normalized = group.trimmed().toLower();
    if (normalized == QString("good") || normalized == QString("良好"))
        return QString("good");
    if (normalized == QString("unlabeled") || normalized == QString("unlabelled")
        || normalized == QString("未标注"))
        return QString("unlabeled");
    return QString("anomaly");
}

qint64 imageLabelClassIdFromExtraData(const std::vector<uint8_t> &extra_data)
{
    if (extra_data.empty())
        return -1;
    const QByteArray encoded(reinterpret_cast<const char *>(extra_data.data()),
                             static_cast<qsizetype>(extra_data.size()));
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(encoded, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        return -1;
    return document.object().value(QString("image_label_class_id")).toInteger(-1);
}

QString labelClassGroupFromExtraData(const std::vector<uint8_t> &extra_data)
{
    if (extra_data.empty())
        return QString("anomaly");
    const QByteArray encoded(reinterpret_cast<const char *>(extra_data.data()),
                             static_cast<qsizetype>(extra_data.size()));
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(encoded, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        return QString("anomaly");
    return normalizedLabelClassGroup(document.object().value(QString("group")).toString());
}

bool selectionIncludesImage(const ModelDatasetSelection &selection, const SourceImage &image,
                            const QList<SourceLabel> &labels)
{
    if (selection.dataset_ids.find(image.dataset_id) != selection.dataset_ids.cend())
        return true;

    const qint64 image_class_id = imageLabelClassIdFromExtraData(image.extra_data);
    if (selection.containsLabelClass(image.dataset_id, image_class_id))
        return true;

    return std::any_of(labels.cbegin(), labels.cend(), [&selection, &image](const SourceLabel &label)
                       { return selection.containsLabelClass(image.dataset_id, label.class_id); });
}

bool selectedLabel(const ModelDatasetSelection &selection, const SourceImage &image, const SourceLabel &label)
{
    return selection.dataset_ids.find(image.dataset_id) != selection.dataset_ids.cend()
        || selection.containsLabelClass(image.dataset_id, label.class_id);
}

bool loadImages(const QString &file_list_path, const QString &project_database_path,
                const QString &task_database_path, const evaluation::Method method, QMap<qint64, Image> &images,
                const std::shared_ptr<std::atomic_bool> &cancel_token, QString *err_msg,
                int *missing_database_images = nullptr, int *ignored_selection_images = nullptr)
{
    images.clear();
    if (missing_database_images != nullptr)
        *missing_database_images = 0;
    if (ignored_selection_images != nullptr)
        *ignored_selection_images = 0;
    if (isCancelled(cancel_token))
    {
        if (err_msg)
            *err_msg = QString("评估已取消");
        return false;
    }

    QList<QPair<qint64, QString>> rows;
    if (!readImageList(file_list_path, rows, cancel_token, err_msg))
        return false;

    if (task_database_path.trimmed().isEmpty() || !QFileInfo(task_database_path).isFile())
    {
        if (err_msg)
            *err_msg = QString("测试任务数据库不存在: %1").arg(task_database_path);
        return false;
    }
    database::ModelTaskDataBase task_database(task_database_path);
    QList<database::DatasetSelectionRecord> selection_records;
    if (!task_database.readDatasets(selection_records, err_msg))
        return false;
    const ModelDatasetSelection selection = modelDatasetSelectionsFromDatabase(selection_records).test;
    if (selection.isEmpty())
    {
        if (err_msg)
            *err_msg = QString("测试任务没有保存测试数据集或类别选择");
        return false;
    }

    if (project_database_path.trimmed().isEmpty() || !QFileInfo(project_database_path).isFile())
    {
        if (err_msg)
            *err_msg = QString("项目数据库不存在: %1").arg(project_database_path);
        return false;
    }

    database::ProjectDataBase project_database(project_database_path);
    QString database_error;
    std::vector<int64_t> image_dataset_ids;
    std::vector<int64_t> image_ids;
    std::vector<QString> image_paths;
    std::vector<std::vector<uint8_t>> image_extra_data;
    if (!project_database.getAllImages(image_dataset_ids, image_ids, image_paths, image_extra_data, database_error))
    {
        if (err_msg)
            *err_msg = QString("读取项目图像失败: %1").arg(database_error);
        return false;
    }
    if (image_dataset_ids.size() != image_ids.size() || image_ids.size() != image_paths.size()
        || image_ids.size() != image_extra_data.size())
    {
        if (err_msg)
            *err_msg = QString("项目图像数据数量不一致");
        return false;
    }

    std::vector<int64_t> label_ids;
    std::vector<int64_t> label_image_ids;
    std::vector<int64_t> label_class_ids;
    std::vector<int64_t> label_types;
    std::vector<std::vector<uint8_t>> label_data;
    if (!project_database.getAllLabels(label_ids, label_image_ids, label_class_ids, label_types, label_data,
                                       database_error))
    {
        if (err_msg)
            *err_msg = QString("读取项目标注失败: %1").arg(database_error);
        return false;
    }
    if (label_ids.size() != label_image_ids.size() || label_ids.size() != label_class_ids.size()
        || label_ids.size() != label_types.size() || label_ids.size() != label_data.size())
    {
        if (err_msg)
            *err_msg = QString("项目标注数据数量不一致");
        return false;
    }

    std::vector<int64_t> class_ids;
    std::vector<QString> class_names;
    std::vector<QString> class_colors;
    std::vector<QString> class_shortcuts;
    std::vector<int64_t> class_ordinals;
    std::vector<std::vector<uint8_t>> class_extra_data;
    if (!project_database.getAllLabelClasses(class_ids, class_names, class_colors, class_shortcuts, class_ordinals,
                                             class_extra_data, database_error))
    {
        if (err_msg)
            *err_msg = QString("读取项目标签类别失败: %1").arg(database_error);
        return false;
    }
    if (class_ids.size() != class_names.size() || class_ids.size() != class_extra_data.size())
    {
        if (err_msg)
            *err_msg = QString("项目标签类别数据数量不一致");
        return false;
    }

    QMap<qint64, SourceImage> source_images;
    for (size_t index = 0; index < image_ids.size(); ++index)
    {
        if (image_ids[index] < 0 || source_images.contains(image_ids[index]))
            continue;
        source_images.insert(image_ids[index],
                             SourceImage{image_ids[index], image_dataset_ids[index], image_paths[index],
                                         image_extra_data[index]});
    }

    QMap<qint64, QList<SourceLabel>> labels_by_image;
    for (size_t index = 0; index < label_ids.size(); ++index)
    {
        if (label_ids[index] < 0 || label_image_ids[index] < 0 || label_class_ids[index] < 0)
            continue;
        labels_by_image[label_image_ids[index]].push_back(
            SourceLabel{label_ids[index], label_image_ids[index], label_class_ids[index], label_data[index]});
    }

    QMap<qint64, SourceClass> classes;
    for (size_t index = 0; index < class_ids.size(); ++index)
    {
        classes.insert(class_ids[index], SourceClass{class_names[index],
                                                     labelClassGroupFromExtraData(class_extra_data[index])});
    }

    const std::unique_ptr<data::LabelDataHelper_t> label_helper
        = data::createLabelDataHelper(static_cast<int>(method));
    if (label_helper == nullptr)
    {
        if (err_msg)
            *err_msg = QString("无法创建评估标注数据解析器");
        return false;
    }

    for (const auto &[image_id, listed_path] : rows)
    {
        if (isCancelled(cancel_token))
        {
            if (err_msg)
                *err_msg = QString("评估已取消");
            return false;
        }
        const auto source_it = source_images.find(image_id);
        if (source_it == source_images.end())
        {
            if (missing_database_images != nullptr)
                ++(*missing_database_images);
            continue;
        }
        const SourceImage &source_image = source_it.value();
        const QList<SourceLabel> source_labels = labels_by_image.value(image_id);
        if (!selectionIncludesImage(selection, source_image, source_labels))
        {
            if (ignored_selection_images != nullptr)
                ++(*ignored_selection_images);
            continue;
        }

        Image image;
        image.id = source_image.id;
        image.dataset_id = source_image.dataset_id;
        image.path = source_image.path.trimmed().isEmpty() ? listed_path : source_image.path;
        image.name = QFileInfo(image.path).fileName();
        data::DatasetIO::getImageDimensions(image.path, image.width, image.height);

        const qint64 image_class_id = imageLabelClassIdFromExtraData(source_image.extra_data);
        const auto image_class = classes.find(image_class_id);
        if (evaluation::isAnomaly(method) && image_class != classes.cend()
            && image_class.value().group == QString("anomaly"))
        {
            image.gt.push_back(GroundTruth{-1, 1, QString("Anomaly"), {}, {}});
        }
        else if (method == evaluation::Method::Classification && image_class != classes.cend()
                 && (selection.dataset_ids.find(image.dataset_id) != selection.dataset_ids.cend()
                     || selection.containsLabelClass(image.dataset_id, image_class_id)))
        {
            image.gt.push_back(GroundTruth{-1, static_cast<int>(image_class_id), image_class.value().name, {}, {}});
        }

        for (const SourceLabel &source_label : source_labels)
        {
            if (!selectedLabel(selection, source_image, source_label))
                continue;
            const auto class_it = classes.find(source_label.class_id);
            if (class_it == classes.cend())
                continue;

            QVariantMap label_geometry;
            if (!source_label.data.empty())
            {
                try
                {
                    const std::unique_ptr<data::LabelData_t> label = label_helper->createLabelData();
                    if (label == nullptr)
                    {
                        if (err_msg)
                            *err_msg = QString("无法创建图像 %1 的标注数据").arg(source_label.id);
                        return false;
                    }
                    label->fromBlob(source_label.data);
                    label_geometry = label->dataMap();
                }
                catch (const std::exception &exception)
                {
                    if (err_msg)
                        *err_msg = QString("读取标注 %1 失败: %2")
                                       .arg(source_label.id)
                                       .arg(QString::fromUtf8(exception.what()));
                    return false;
                }
            }

            GroundTruth ground_truth;
            ground_truth.label_id = source_label.id;
            ground_truth.class_id = evaluation::isAnomaly(method)
                ? (class_it.value().group == QString("anomaly") ? 1 : 0)
                : static_cast<int>(source_label.class_id);
            ground_truth.class_name = evaluation::isAnomaly(method)
                ? (ground_truth.class_id == 1 ? QString("Anomaly") : QString("GOOD"))
                : class_it.value().name;
            ground_truth.geometry = label_geometry;
            ground_truth.bounds = label_geometry;
            if (!readBox(ground_truth.geometry, ground_truth.box))
                ground_truth.geometry.clear();
            ground_truth.geometry = canonicalGeometry(ground_truth.geometry, ground_truth.box);
            if (ground_truth.box.valid())
                ground_truth.bounds = boxMap(ground_truth.box);
            if (!evaluation::isAnomaly(method) || ground_truth.class_id == 1)
                image.gt.push_back(std::move(ground_truth));
        }
        images.insert(image.id, std::move(image));
    }
    if (images.isEmpty())
    {
        if (err_msg)
            *err_msg = QString("测试数据集没有有效图像");
        return false;
    }
    return true;
}

bool loadPredictions(const QString &task_database_path, const QString &prediction_dir,
                     QMap<qint64, Image> &images, const bool anomaly_method, int *count,
                     const std::shared_ptr<std::atomic_bool> &cancel_token, QString *err_msg,
                     int *ignored_count = nullptr)
{
    if (count)
        *count = 0;
    if (ignored_count)
        *ignored_count = 0;
    if (isCancelled(cancel_token))
    {
        if (err_msg)
            *err_msg = QString("评估已取消");
        return false;
    }
    if (task_database_path.trimmed().isEmpty() || !QFileInfo(task_database_path).isFile())
        return true;

    database::ModelTaskDataBase database(task_database_path);
    QHash<qint64, QVariant> records;
    if (!database.readPredictions(records, err_msg))
        return false;

    QSet<QString> prediction_ids;
    int total = 0;
    const auto fail = [err_msg](const QString &message)
    {
        if (err_msg)
            *err_msg = message;
        return false;
    };
    for (auto record_it = records.cbegin(); record_it != records.cend(); ++record_it)
    {
        if (isCancelled(cancel_token))
            return fail(QString("评估已取消"));
        const qint64 image_id = record_it.key();
        if (!images.contains(image_id))
        {
            if (ignored_count)
                ++(*ignored_count);
            continue;
        }

        if (anomaly_method)
        {
            const QVariantMap value = record_it.value().toMap();
            const QVariant score_value = value.value(QStringLiteral("image_score"));
            double score = 0.0;
            if (!finiteNumber(score_value, &score) || score < 0.0 || score > 1.0)
                return fail(QString("图像 %1 的 image_score 无效").arg(image_id));
            Prediction prediction;
            prediction.prediction_id = QString("image-%1").arg(image_id);
            prediction.image_id = image_id;
            prediction.class_id = 1;
            prediction.class_name = QString("Anomaly");
            prediction.score = score;
            images[image_id].predictions.push_back(prediction);
            ++total;
            continue;
        }

        const QVariant value = record_it.value();
        QVariantList prediction_values;
        if (value.metaType().id() == QMetaType::QVariantList)
            prediction_values = value.toList();
        else
        {
            const QVariantMap map = value.toMap();
            if (map.contains(evaluation::fieldName(evaluation::Field::Predictions)))
                prediction_values = map.value(evaluation::fieldName(evaluation::Field::Predictions)).toList();
            else if (map.contains(evaluation::fieldName(evaluation::Field::ClassId))
                     || map.contains(evaluation::fieldName(evaluation::Field::Score)))
                prediction_values.push_back(value);
            else if (!map.isEmpty())
                return fail(QString("图像 %1 的预测记录格式无效").arg(image_id));
        }
        if (prediction_values.size() > static_cast<int>(kMaxEvaluationRecords))
            return fail(QString("图像 %1 的预测数量超过限制").arg(image_id));

        for (int index = 0; index < prediction_values.size(); ++index)
        {
            const QVariantMap value_map = prediction_values.at(index).toMap();
            if (value_map.isEmpty())
                return fail(QString("图像 %1 的预测记录必须是对象").arg(image_id));
            Prediction prediction;
            prediction.prediction_id = mapString(value_map, evaluation::fieldName(evaluation::Field::PredictionId));
            if (prediction.prediction_id.isEmpty())
                prediction.prediction_id = QString("%1-%2").arg(image_id).arg(index + 1);
            prediction.image_id = image_id;
            prediction.class_id = mapInt(value_map, evaluation::fieldName(evaluation::Field::ClassId));
            prediction.class_name = mapString(value_map, evaluation::fieldName(evaluation::Field::ClassName));
            if (prediction.class_id < 0)
                return fail(QString("预测 %1 的 class_id 无效").arg(prediction.prediction_id));
            if (!finiteNumber(value_map.value(evaluation::fieldName(evaluation::Field::Score)), &prediction.score)
                || prediction.score < 0.0 || prediction.score > 1.0)
                return fail(QString("预测 %1 的 score 无效").arg(prediction.prediction_id));
            if (prediction_ids.contains(prediction.prediction_id))
                return fail(QString("预测 prediction_id 重复: %1").arg(prediction.prediction_id));
            prediction_ids.insert(prediction.prediction_id);

            prediction.geometry = value_map.value(evaluation::fieldName(evaluation::Field::Geometry)).toMap();
            const QString x_key = evaluation::fieldName(evaluation::Field::X);
            const QString y_key = evaluation::fieldName(evaluation::Field::Y);
            const QString width_key = evaluation::fieldName(evaluation::Field::Width);
            const QString height_key = evaluation::fieldName(evaluation::Field::Height);
            const QString short_width_key = QString("w");
            const QString short_height_key = QString("h");
            const bool has_direct_x = value_map.contains(x_key);
            const bool has_direct_y = value_map.contains(y_key);
            const bool has_direct_width = value_map.contains(width_key) || value_map.contains(short_width_key);
            const bool has_direct_height = value_map.contains(height_key) || value_map.contains(short_height_key);
            if (prediction.geometry.isEmpty()
                && (has_direct_x || has_direct_y || has_direct_width || has_direct_height))
            {
                if (!has_direct_x || !has_direct_y || !has_direct_width || !has_direct_height)
                    return fail(QString("预测 %1 的 bbox 必须同时包含 x、y、w/width、h/height")
                                    .arg(prediction.prediction_id));
                prediction.geometry = {
                    {evaluation::fieldName(evaluation::Field::Type), QStringLiteral("bbox")},
                    {evaluation::fieldName(evaluation::Field::Format), QStringLiteral("xywh")},
                    {evaluation::fieldName(evaluation::Field::CoordinateSystem), QStringLiteral("image_pixels")},
                    {evaluation::fieldName(evaluation::Field::Values),
                     QVariantList{value_map.value(x_key), value_map.value(y_key),
                                  value_map.contains(width_key) ? value_map.value(width_key)
                                                                : value_map.value(short_width_key),
                                  value_map.contains(height_key) ? value_map.value(height_key)
                                                                 : value_map.value(short_height_key)}}};
            }
            prediction.bounds = prediction.geometry.value(evaluation::fieldName(evaluation::Field::Bounds)).toMap();
            if (!prediction.geometry.isEmpty())
            {
                QString geometry_error;
                if (!validateGeometryProtocol(prediction.geometry, images[image_id], prediction_dir,
                                              &geometry_error))
                    return fail(geometry_error);
            }
            if (readBox(prediction.geometry, prediction.box))
            {
                const Image &image = images[image_id];
                if (image.width > 0 && image.height > 0)
                {
                    const double right = std::clamp(prediction.box.x + prediction.box.w, 0.0,
                                                    static_cast<double>(image.width));
                    const double bottom = std::clamp(prediction.box.y + prediction.box.h, 0.0,
                                                     static_cast<double>(image.height));
                    prediction.box.x = std::clamp(prediction.box.x, 0.0, static_cast<double>(image.width));
                    prediction.box.y = std::clamp(prediction.box.y, 0.0, static_cast<double>(image.height));
                    prediction.box.w = std::max(0.0, right - prediction.box.x);
                    prediction.box.h = std::max(0.0, bottom - prediction.box.y);
                }
                prediction.bounds = boxMap(prediction.box);
            }
            prediction.geometry = canonicalGeometry(prediction.geometry, prediction.box);
            images[image_id].predictions.push_back(prediction);
            ++total;
        }
    }
    if (count)
        *count = total;
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
    return {{evaluation::fieldName(evaluation::Field::Precision), precision},
            {evaluation::fieldName(evaluation::Field::Recall), recall},
            {evaluation::fieldName(evaluation::Field::F1), f1},
            {evaluation::fieldName(evaluation::Field::PrecisionDefined), tp + fp > 0},
            {evaluation::fieldName(evaluation::Field::RecallDefined), tp + fn > 0},
            {evaluation::fieldName(evaluation::Field::F1Defined),
             tp + fp > 0 && tp + fn > 0 && precision + recall > 0.0},
            {evaluation::fieldName(evaluation::Field::Tp), tp},
            {evaluation::fieldName(evaluation::Field::Fp), fp},
            {evaluation::fieldName(evaluation::Field::Fn), fn}};
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
        output.available = true;
        output.metrics = QVariantMap{{evaluation::fieldName(evaluation::Field::Available), true},
                                     {evaluation::fieldName(evaluation::Field::Image),
                                      diagnostic.value(evaluation::fieldName(evaluation::Field::Image))},
                                     {evaluation::fieldName(evaluation::Field::Definition),
                                      QStringLiteral("anomaly_score_threshold")}};
        output.image_definition = QVariantMap{{evaluation::fieldName(evaluation::Field::SampleUnit), QStringLiteral("image")},
                                              {evaluation::fieldName(evaluation::Field::Aggregation), QStringLiteral("micro")},
                                              {evaluation::fieldName(evaluation::Field::PositiveDefinition),
                                               QStringLiteral("score_above_threshold")},
                                               {evaluation::fieldName(evaluation::Field::HasImageMetrics), true}};
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
        precision_values.push_back(metric.value(evaluation::fieldName(evaluation::Field::Precision)));
        recall_values.push_back(metric.value(evaluation::fieldName(evaluation::Field::Recall)));
    }
    const Counts work_point = countsAt(confidence);
    output.available = true;
    output.metrics = QVariantMap{{evaluation::fieldName(evaluation::Field::Available), true},
                                 {evaluation::fieldName(evaluation::Field::Instance),
                                  metricMap(work_point.tp, work_point.fp, work_point.fn)},
                                 {evaluation::fieldName(evaluation::Field::PerClass),
                                  diagnostic.value(evaluation::fieldName(evaluation::Field::Instance))
                                      .toMap()
                                      .value(evaluation::fieldName(evaluation::Field::PerClass))},
                                 {evaluation::fieldName(evaluation::Field::Definition),
                                  QStringLiteral("confidence_iou_work_point")}};
    output.image_definition = QVariantMap{{evaluation::fieldName(evaluation::Field::SampleUnit),
                                           QStringLiteral("image_class_presence")},
                                          {evaluation::fieldName(evaluation::Field::Aggregation), QStringLiteral("micro")},
                                          {evaluation::fieldName(evaluation::Field::PositiveDefinition),
                                           QStringLiteral("gt_or_pred_class_present")},
                                          {evaluation::fieldName(evaluation::Field::HasImageMetrics), true}};
    output.charts.push_back(QVariantMap{{evaluation::fieldName(evaluation::Field::Kind), QStringLiteral("line")},
                                         {evaluation::fieldName(evaluation::Field::ChartId),
                                          QStringLiteral("precision_recall")},
                                         {evaluation::fieldName(evaluation::Field::FilterKind),
                                          QStringLiteral("precision_recall")},
                                         {evaluation::fieldName(evaluation::Field::Title), QString("PR 曲线")},
                                         {evaluation::fieldName(evaluation::Field::Data),
                                          QVariantMap{{evaluation::fieldName(evaluation::Field::Labels), threshold_labels},
                                                      {evaluation::fieldName(evaluation::Field::Datasets), QVariantList{
                                                          QVariantMap{{evaluation::fieldName(evaluation::Field::Label),
                                                                       QStringLiteral("Precision")},
                                                                      {evaluation::fieldName(evaluation::Field::Data),
                                                                       precision_values}},
                                                          QVariantMap{{evaluation::fieldName(evaluation::Field::Label),
                                                                       QStringLiteral("Recall")},
                                                                      {evaluation::fieldName(evaluation::Field::Data),
                                                                       recall_values}}}}}},
                                         {evaluation::fieldName(evaluation::Field::Options),
                                          QVariantMap{{QStringLiteral("maintainAspectRatio"), false}}}});
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
        capabilities.chart_kinds = {QStringLiteral("line")};
    else if (capabilities.has_instance_metrics)
        capabilities.chart_kinds = {QStringLiteral("bar"), QStringLiteral("line")};
    return capabilities;
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
    if (options.project_database_path.isEmpty() || options.dataset_file_list_path.isEmpty()
        || options.task_database_path.isEmpty() || options.prediction_dir.isEmpty())
    {
        if (err_msg)
            *err_msg = QString("评估路径参数不完整");
        return false;
    }

    QMap<qint64, Image> images;
    int missing_database_images = 0;
    int ignored_selection_images = 0;
    if (!loadImages(options.dataset_file_list_path, options.project_database_path, options.task_database_path,
                    options.method, images, options.cancel_token, err_msg, &missing_database_images,
                    &ignored_selection_images))
        return false;
    if (missing_database_images > 0)
        spdlog::warn("测试任务文件列表中有 {} 个图像已不在当前项目数据库中，已跳过", missing_database_images);
    if (ignored_selection_images > 0)
        spdlog::warn("测试任务文件列表中有 {} 个图像不属于当前数据集或类别选择，已跳过", ignored_selection_images);

    const QString dataset_root = QFileInfo(options.project_database_path).absolutePath();
    int missing_source_images = 0;
    for (auto it = images.begin(); it != images.end();)
    {
        if (sourceImageExists(it->path, dataset_root))
            ++it;
        else
        {
            ++missing_source_images;
            it = images.erase(it);
        }
    }
    if (missing_source_images > 0)
        spdlog::warn("测试评估跳过 {} 个不存在的源图像", missing_source_images);

    int prediction_count = 0;
    int ignored_prediction_count = 0;
    if (!loadPredictions(options.task_database_path, options.prediction_dir, images,
                         evaluation::isAnomaly(options.method), &prediction_count, options.cancel_token, err_msg,
                         &ignored_prediction_count))
        return false;
    if (ignored_prediction_count > 0)
        spdlog::warn("预测结果中有 {} 条记录不属于当前可用图像，已跳过", ignored_prediction_count);
    int images_without_predictions = 0;
    for (const Image &image : images)
    {
        if (image.predictions.isEmpty())
            ++images_without_predictions;
    }
    if (images_without_predictions > 0)
        spdlog::warn("{} 个图像没有推理结果，按空预测进行评估", images_without_predictions);
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
    const QString dataset_root_path = dataset_root;
    const QString prediction_task_root = options.prediction_dir;
    const QString matrix_fn = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::FalseNegative);
    const QString matrix_fp = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::FalsePositive);
    const QString matrix_total = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::Total);
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
            QVariantMap event{{evaluation::fieldName(evaluation::Field::EventUuid),
                               QStringLiteral("%1-%2").arg(image.id).arg(event_records.size() + 1)},
                              {evaluation::fieldName(evaluation::Field::ImageId), image.id},
                              {evaluation::fieldName(evaluation::Field::Status), evaluation::statusKey(status)},
                              {evaluation::fieldName(evaluation::Field::Score), pred ? pred->score : 0.0},
                              {evaluation::fieldName(evaluation::Field::Iou), iou},
                              {evaluation::fieldName(evaluation::Field::GtLabelId), gt ? gt->label_id : -1},
                              {evaluation::fieldName(evaluation::Field::GtClassId), gt ? gt->class_id : -1},
                              {evaluation::fieldName(evaluation::Field::GtClassName), gt ? gt->class_name : QString()},
                              {evaluation::fieldName(evaluation::Field::GtGeometry), gt_geometry},
                              {evaluation::fieldName(evaluation::Field::PredInstanceId), pred ? pred->prediction_id : QString()},
                              {evaluation::fieldName(evaluation::Field::PredClassId), pred ? pred->class_id : -1},
                              {evaluation::fieldName(evaluation::Field::PredClassName), pred ? pred->class_name : QString()},
                              {evaluation::fieldName(evaluation::Field::PredGeometry), pred_geometry},
                              {evaluation::fieldName(evaluation::Field::CropBounds), viewport},
                               {evaluation::fieldName(evaluation::Field::GtOverlayBounds),
                                normalizedOverlayBounds(gt ? gt->bounds : QVariantMap{}, viewport)},
                               {evaluation::fieldName(evaluation::Field::PredOverlayBounds),
                                normalizedOverlayBounds(pred ? pred->bounds : QVariantMap{}, viewport)},
                               {evaluation::fieldName(evaluation::Field::GtOverlayPoints),
                                normalizedOverlayPoints(gt_geometry, viewport)},
                               {evaluation::fieldName(evaluation::Field::PredOverlayPoints),
                                normalizedOverlayPoints(pred_geometry, viewport)},
                               {evaluation::fieldName(evaluation::Field::GtMaskUrl), maskUrl(gt_geometry, dataset_root_path)},
                               {evaluation::fieldName(evaluation::Field::PredMaskUrl), maskUrl(pred_geometry, prediction_task_root)}};
            event_records.push_back(event);
        };

        if (anomaly_method)
        {
            // Anomaly evaluation is image-level.  Build the same one-event-
            // per-image view that the UI displays so all consumers use one
            // event model during this in-memory evaluation.
            const GroundTruth *anomaly_gt = nullptr;
            for (const GroundTruth &gt : image.gt)
            {
                if (gt.class_id == 1)
                {
                    anomaly_gt = &gt;
                    break;
                }
            }
            const Prediction *anomaly_prediction = nullptr;
            double image_score = 0.0;
            for (const Prediction &prediction : image.predictions)
            {
                image_score = std::max(image_score, prediction.score);
                if (prediction.class_id == 1 && prediction.score >= options.confidence_threshold
                    && (anomaly_prediction == nullptr || prediction.score > anomaly_prediction->score))
                    anomaly_prediction = &prediction;
            }
            const bool ground_truth_anomaly = anomaly_gt != nullptr;
            const bool predicted_anomaly = anomaly_prediction != nullptr;
            const evaluation::Status status = ground_truth_anomaly && predicted_anomaly
                ? evaluation::Status::TruePositive
                : (!ground_truth_anomaly && !predicted_anomaly
                       ? evaluation::Status::TrueNegative
                       : (predicted_anomaly ? evaluation::Status::FalsePositive
                                            : evaluation::Status::FalseNegative));
            if (status == evaluation::Status::TruePositive)
                ++image_counts.tp;
            else if (status == evaluation::Status::FalsePositive)
                ++image_counts.fp;
            else if (status == evaluation::Status::FalseNegative)
                ++image_counts.fn;

            GroundTruth display_gt = anomaly_gt != nullptr ? *anomaly_gt : GroundTruth{};
            display_gt.class_id = ground_truth_anomaly ? 1 : 0;
            display_gt.class_name = ground_truth_anomaly ? QStringLiteral("Anomaly") : QStringLiteral("GOOD");
            Prediction display_prediction
                = anomaly_prediction != nullptr ? *anomaly_prediction : Prediction{};
            display_prediction.class_id = predicted_anomaly ? 1 : 0;
            display_prediction.class_name = predicted_anomaly ? QStringLiteral("Anomaly") : QStringLiteral("GOOD");
            display_prediction.score = image_score;
            appendEvent(status, &display_gt, &display_prediction, 0.0);
            continue;
        }

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
            incrementMatrix(QString::number(pred.class_id), matrix_fp);
            appendEvent(evaluation::Status::FalsePositive, nullptr, &pred, 0.0);
        }
        for (int g = 0; g < image.gt.size(); ++g)
        {
            if (used_gt.at(g))
                continue;
            const GroundTruth &gt = image.gt.at(g);
            ++overall.fn;
            ++per_class[gt.class_id].fn;
            incrementMatrix(matrix_fn, QString::number(gt.class_id));
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
            gt_instances.push_back(QVariantMap{{evaluation::fieldName(evaluation::Field::LabelId), gt.label_id},
                                               {evaluation::fieldName(evaluation::Field::ClassId), gt.class_id},
                                               {evaluation::fieldName(evaluation::Field::ClassName), gt.class_name},
                                               {evaluation::fieldName(evaluation::Field::Geometry), gt.geometry}});
        }
        QVariantList prediction_instances;
        for (const Prediction &prediction : image.predictions)
        {
            prediction_instances.push_back(QVariantMap{{evaluation::fieldName(evaluation::Field::PredictionId), prediction.prediction_id},
                                                        {evaluation::fieldName(evaluation::Field::ClassId), prediction.class_id},
                                                        {evaluation::fieldName(evaluation::Field::ClassName), prediction.class_name},
                                                        {evaluation::fieldName(evaluation::Field::Score), prediction.score},
                                                        {evaluation::fieldName(evaluation::Field::Geometry), prediction.geometry}});
        }
        image_records.push_back(QVariantMap{{evaluation::fieldName(evaluation::Field::ImageId), image.id},
                                            {evaluation::fieldName(evaluation::Field::DatasetId), image.dataset_id},
                                            {evaluation::fieldName(evaluation::Field::ImageName), image.name},
                                            {evaluation::fieldName(evaluation::Field::ImagePath), image.path},
                                            {evaluation::fieldName(evaluation::Field::ImageWidth), image.width},
                                            {evaluation::fieldName(evaluation::Field::ImageHeight), image.height},
                                            {evaluation::fieldName(evaluation::Field::GtInstances), gt_instances},
                                            {evaluation::fieldName(evaluation::Field::Predictions), prediction_instances}});
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
        metric.insert(evaluation::fieldName(evaluation::Field::ClassId), it.key());
        metric.insert(evaluation::fieldName(evaluation::Field::ClassName), it.value());
        per_class_metrics.push_back(metric);
        chart_labels.push_back(it.value());
        precision_values.push_back(metric.value(evaluation::fieldName(evaluation::Field::Precision)));
        recall_values.push_back(metric.value(evaluation::fieldName(evaluation::Field::Recall)));
        f1_values.push_back(metric.value(evaluation::fieldName(evaluation::Field::F1)));
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
        class_catalog.push_back(QVariantMap{{evaluation::fieldName(evaluation::Field::Id), it.key()},
                                            {evaluation::fieldName(evaluation::Field::Name), it.value()},
                                            {evaluation::fieldName(evaluation::Field::Color), classColor(it.key())}});
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
        if (keys.at(0) == matrix_fn)
            unmatched_fn += it.value();
        else
            pred_totals[keys.at(0).toInt()] += it.value();
        if (keys.at(1) == matrix_fp)
            unmatched_fp += it.value();
        else
            gt_totals[keys.at(1).toInt()] += it.value();
    }
    const auto appendCell = [&](const QString &row, const QString &column, qint64 count,
                                const evaluation::CellKind kind,
                                bool selectable, bool diagonal, bool error)
    {
        const bool row_fn = row == matrix_fn;
        const bool row_total = row == matrix_total;
        const bool column_fp = column == matrix_fp;
        const bool column_total = column == matrix_total;
        const int row_id = row_fn || row_total ? -1 : row.toInt();
        const int column_id = column_fp || column_total ? -1 : column.toInt();
        const QString total_label = QString("合计");
        const QString row_label = row_fn ? matrix_fn : (row_total ? total_label : classes.value(row_id));
        const QString column_label = column_fp ? matrix_fp
                                               : (column_total ? total_label : classes.value(column_id));
        matrix_cells.push_back(QVariantMap{{evaluation::fieldName(evaluation::Field::RowKey), row},
                                           {evaluation::fieldName(evaluation::Field::ColumnKey), column},
                                           {evaluation::fieldName(evaluation::Field::RowLabel), row_label},
                                           {evaluation::fieldName(evaluation::Field::ColumnLabel), column_label},
                                           {evaluation::fieldName(evaluation::Field::RowClassId), row_id},
                                           {evaluation::fieldName(evaluation::Field::ColumnClassId), column_id},
                                           {evaluation::fieldName(evaluation::Field::Count), count},
                                           {evaluation::fieldName(evaluation::Field::CellKind), evaluation::cellKindKey(kind)},
                                           {evaluation::fieldName(evaluation::Field::Selectable), selectable},
                                           {evaluation::fieldName(evaluation::Field::IsDiagonal), diagonal},
                                           {evaluation::fieldName(evaluation::Field::IsError), error}});
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
        appendCell(row, matrix_fp, matrix.value(row + QLatin1Char('\x1f') + matrix_fp),
                   evaluation::CellKind::FalsePositive, true, false, true);
        appendCell(row, matrix_total, pred_totals.value(row_it.key()), evaluation::CellKind::PredTotal, true,
                   false, false);
    }
    for (auto column_it = classes.cbegin(); column_it != classes.cend(); ++column_it)
    {
        const QString column = QString::number(column_it.key());
        appendCell(matrix_fn, column, matrix.value(matrix_fn + QLatin1Char('\x1f') + column),
                   evaluation::CellKind::FalseNegative, true, false, true);
    }
    appendCell(matrix_fn, matrix_fp, 0, evaluation::CellKind::NotApplicable, false, false, false);
    appendCell(matrix_fn, matrix_total, unmatched_fn, evaluation::CellKind::FalseNegativeTotal,
               true, false, true);
    for (auto column_it = classes.cbegin(); column_it != classes.cend(); ++column_it)
    {
        const QString column = QString::number(column_it.key());
        appendCell(matrix_total, column, gt_totals.value(column_it.key()), evaluation::CellKind::GtTotal, true,
                   false, false);
    }
    appendCell(matrix_total, matrix_fp, unmatched_fp, evaluation::CellKind::FalsePositiveTotal,
               true, false, true);
    appendCell(matrix_total, matrix_total, anomaly_method ? images.size() : event_records.size(),
               evaluation::CellKind::All, true, false, false);

    const QVariantMap diagnostic = {
        {evaluation::fieldName(evaluation::Field::Instance),
         QVariantMap{{evaluation::fieldName(evaluation::Field::Overall), metricMap(overall.tp, overall.fp, overall.fn)},
                     {evaluation::fieldName(evaluation::Field::PerClass), per_class_metrics}}},
        {evaluation::fieldName(evaluation::Field::Image),
         metricMap(image_counts.tp, image_counts.fp, image_counts.fn)}};
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
    QVariantList charts;
    if (capabilities.has_instance_metrics)
    {
        charts.push_back(QVariantMap{{evaluation::fieldName(evaluation::Field::Kind), QStringLiteral("bar")},
                                     {evaluation::fieldName(evaluation::Field::ChartId),
                                      QStringLiteral("per_class_metrics")},
                                     {evaluation::fieldName(evaluation::Field::FilterKind),
                                      QStringLiteral("per_class_metrics")},
                                     {evaluation::fieldName(evaluation::Field::Title), QString("按类别指标")},
                                     {evaluation::fieldName(evaluation::Field::Data),
                                      QVariantMap{{evaluation::fieldName(evaluation::Field::Labels), chart_labels},
                                                  {evaluation::fieldName(evaluation::Field::Datasets), QVariantList{
                                                      QVariantMap{{evaluation::fieldName(evaluation::Field::Label),
                                                                   QStringLiteral("Precision")},
                                                                  {evaluation::fieldName(evaluation::Field::Data),
                                                                   precision_values}},
                                                      QVariantMap{{evaluation::fieldName(evaluation::Field::Label),
                                                                   QStringLiteral("Recall")},
                                                                  {evaluation::fieldName(evaluation::Field::Data),
                                                                   recall_values}},
                                                      QVariantMap{{evaluation::fieldName(evaluation::Field::Label),
                                                                   QStringLiteral("F1")},
                                                                  {evaluation::fieldName(evaluation::Field::Data),
                                                                   f1_values}}}}}},
                                     {evaluation::fieldName(evaluation::Field::Options),
                                      QVariantMap{{QStringLiteral("maintainAspectRatio"), false}}}});
    }
    for (const QVariant &chart : official.charts)
        charts.push_back(chart);

    const QVariantMap evaluation_data = {
        {evaluation::fieldName(evaluation::Field::PrimaryMetricSet), official.available
                ? evaluation::metricSetKey(evaluation::MetricSet::Official)
                : evaluation::metricSetKey(evaluation::MetricSet::Diagnostic)},
        {evaluation::fieldName(evaluation::Field::EvaluationConfig),
         evaluation::normalizedEvaluationConfig(options.evaluation_config)},
        {evaluation::fieldName(evaluation::Field::ClassCatalog), class_catalog},
        {evaluation::fieldName(evaluation::Field::DiagnosticMetrics), diagnostic},
        {evaluation::fieldName(evaluation::Field::OfficialMetrics), official.available ? official.metrics
                                                                   : QVariantMap{{evaluation::fieldName(evaluation::Field::Available), false}}},
        {evaluation::fieldName(evaluation::Field::ImageMetricDefinition), official.available && !official.image_definition.isEmpty()
                ? official.image_definition
                : QVariantMap{{evaluation::fieldName(evaluation::Field::SampleUnit), QStringLiteral("image_presence")},
                              {evaluation::fieldName(evaluation::Field::Aggregation), QStringLiteral("micro")},
                              {evaluation::fieldName(evaluation::Field::PositiveDefinition),
                               QStringLiteral("gt_or_pred_class_present")},
                              {evaluation::fieldName(evaluation::Field::HasImageMetrics), true}}},
        {evaluation::fieldName(evaluation::Field::Capabilities), QVariantMap{{evaluation::fieldName(evaluation::Field::HasInstanceMetrics), capabilities.has_instance_metrics},
                                                         {evaluation::fieldName(evaluation::Field::HasImageMetrics), capabilities.has_image_metrics},
                                                         {evaluation::fieldName(evaluation::Field::HasConfusionMatrix), capabilities.has_confusion_matrix},
                                                         {evaluation::fieldName(evaluation::Field::HasInstanceEvents), capabilities.has_instance_events},
                                                         {evaluation::fieldName(evaluation::Field::ChartKinds), capabilities.chart_kinds}}},
        {evaluation::fieldName(evaluation::Field::ConfusionMatrix), QVariantMap{{evaluation::fieldName(evaluation::Field::Cells), matrix_cells}}},
        {evaluation::fieldName(evaluation::Field::Charts), charts},
        {evaluation::fieldName(evaluation::Field::ImageRecords), image_records},
        {evaluation::fieldName(evaluation::Field::InstanceRecords), event_records},
        {evaluation::fieldName(evaluation::Field::ImageCount), images.size()},
        {evaluation::fieldName(evaluation::Field::PredictionCount), prediction_count},
        {evaluation::fieldName(evaluation::Field::EventCount), event_records.size()},
    };
    if (result)
    {
        result->evaluation_data = evaluation_data;
    }
    return true;
}

} // namespace dltool::model
