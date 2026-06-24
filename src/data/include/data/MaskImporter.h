#pragma once

#include "data/DataImporter.h"

#include <QPointF>
#include <QRect>
#include <QString>
#include <map>
#include <vector>

namespace dltool::data {

class DATA_API MaskImporter : public DataImporter
{
    Q_OBJECT

public:
    explicit MaskImporter(dltool::database::ProjectDataBase *database, QObject *parent = nullptr);
    ~MaskImporter() override;

    void startImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir) override;

private:
    struct MaskGeometry
    {
        QRect                bbox;
        std::vector<QPointF> polygon;
        int                  mask_width{0};
        int                  mask_height{0};
    };

    void doImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir);

    std::vector<QString> scanMaskFiles(const QString &mask_dir) const;

    bool readMaskGeometry(const QString &mask_path, MaskGeometry &geometry) const;

    QVariantMap maskToLabelData(const MaskGeometry &geometry, int image_width, int image_height) const;

    QString labelClassNameForMask(const QString &mask_path, const QString &mask_root) const;

    QString imageStemForMask(const QString &mask_path, const QString &mask_root, const QString &mask_stem) const;

    std::map<QString, QString> loadQueryNameMap(const QString &dir_path) const;
};

} // namespace dltool::data
