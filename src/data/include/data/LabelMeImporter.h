#pragma once

#include "DataImporter.h"
#include "DatasetIO.h"

#include <QPointF>
#include <QThread>
#include <QVariantMap>
#include <map>
#include <vector>

namespace dltool::data {

/**
 * @brief LabelMe 数据格式导入器
 *
 * 解析 LabelMe 每图一个 JSON 的标注结构，并输出 DataManager 可直接写入数据库的统一数据。
 * 通用的图片扫描、尺寸读取、bbox 裁剪和默认颜色由 DatasetIO 负责，避免格式实现之间重复。
 */
class DATA_API LabelMeImporter : public DataImporter
{
    Q_OBJECT

public:
    struct LabelMeShape
    {
        QString              label;
        QString              shape_type;
        std::vector<QPointF> points;
    };

    struct LabelMeData
    {
        QString                   image_path;
        int                       image_width{0};
        int                       image_height{0};
        std::vector<LabelMeShape> shapes;
    };

    explicit LabelMeImporter(dltool::database::ProjectDataBase *database, QObject *parent = nullptr);
    ~LabelMeImporter() override;

    void startImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir) override;

private:
    bool parseLabelMeJson(const QString &json_path, LabelMeData &data);

    void doImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir);

    QVariantMap convertShapeToLabelData(const LabelMeShape &shape, int image_width, int image_height);

    void processAndEmitData(int64_t dataset_id, const std::map<QString, ImageData> &images,
                            const std::vector<LabelMeData> &parsed_annotations);
};

} // namespace dltool::data
