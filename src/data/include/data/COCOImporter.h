#pragma once

#include "DataImporter.h"
#include "DatasetIO.h"

#include <QThread>
#include <map>
#include <vector>

namespace dltool::data {

/**
 * @brief COCO 检测数据导入器
 *
 * 当前内部标注模型只保存检测框，因此导入 COCO 时使用 annotations[].bbox。
 * segmentation/iscrowd 等字段会被忽略，后续可在新的 LabelData 实现中扩展。
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
