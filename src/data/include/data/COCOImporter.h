#pragma once

#include "DataImporter.h"
#include "DatasetIO.h"

#include <QThread>
#include <map>
#include <vector>

namespace dltool::data {

/**
 * @brief COCO 数据导入器
 *
 * 语义分割项目导入时优先读取 polygon/RLE segmentation 并生成多边形点集；
 * 检测项目或没有可用 segmentation 时，使用 annotations[].bbox 生成检测框数据。
 */
class DATA_API COCOImporter : public DataImporter
{
    Q_OBJECT

public:
    explicit COCOImporter(dltool::database::ProjectDataBase *database, QObject *parent = nullptr);
    ~COCOImporter() override;

    void startImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir) override;

private:
    struct CocoImage
    {
        int64_t coco_id{0};
        QString file_name;
        QString image_path;
        int     width{0};
        int     height{0};
    };

    struct CocoCategory
    {
        int64_t id{0};
        QString name;
    };

    QString findCocoJsonFile(const QString &data_path) const;
    bool    looksLikeCocoJson(const QString &json_path) const;
    QString resolveImagePath(const QString &image_dir, const QString &file_name,
                             const std::map<QString, QString> &image_file_index) const;

    void doImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir);
};

} // namespace dltool::data
