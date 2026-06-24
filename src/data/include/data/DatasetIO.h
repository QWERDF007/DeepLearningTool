#pragma once

#include "dltool/data/Export.h"

#include <QPointF>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <cstdint>
#include <map>
#include <vector>

namespace dltool::data {

struct DATA_API ImageData
{
    QString image_path;
    int     image_width{0};
    int     image_height{0};
};

struct DATA_API ExportImage
{
    int64_t dataset_id{0};
    int64_t image_id{0};
    QString path;
    int     width{0};
    int     height{0};
};

struct DATA_API ExportLabelClass
{
    int64_t id{0};
    QString name;
    QString color;
};

struct DATA_API ExportLabel
{
    int64_t     label_id{0};
    int64_t     image_id{0};
    int64_t     label_class_id{0};
    QVariantMap data;
};

struct DATA_API ExportDataset
{
    int64_t                       dataset_id{0};
    QString                       dataset_name;
    std::vector<ExportImage>      images;
    std::vector<ExportLabelClass> label_classes;
    std::vector<ExportLabel>      labels;
};

class DATA_API DatasetIO
{
public:
    static std::vector<QString> scanImageFiles(const QString &image_dir);
    static std::vector<QString> scanJsonFiles(const QString &data_path);
    static bool                 getImageDimensions(const QString &image_path, int &width, int &height);
    static QVariantMap          bboxToLabelData(double x, double y, double width, double height, int image_width,
                                                int image_height);
    static QVariantList         pointsToVariantList(const std::vector<QPointF> &points);
    static std::vector<QPointF> variantListToPoints(const QVariant &value);
    static QVariantMap pointsToLabelData(const std::vector<QPointF> &points, int image_width, int image_height);
    static QString     generateDefaultColor(int index);
    static QString     uniqueFileName(const QString &source_path, int64_t stable_id,
                                      const std::map<QString, int> &used_names);
    static bool        ensureDirectory(const QString &path, QString &err_msg);
    static bool        copyFile(const QString &source_path, const QString &target_path, QString &err_msg);
};

} // namespace dltool::data
