#pragma once

#include "DataImporter.h"

#include <QThread>

namespace dltool::data {

/**
 * @brief 文件夹分类格式导入器
 *
 * 目录结构: image_dir/类别名/图片
 * 每个子目录名称即为该目录下图片的类别，用于图像分类项目。
 */
class DATA_API FolderImporter : public DataImporter
{
    Q_OBJECT

public:
    explicit FolderImporter(dltool::database::ProjectDataBase *database, QObject *parent = nullptr);
    ~FolderImporter() override;

    void startImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir) override;

private:
    void doImport(int64_t dataset_id, const QString &image_dir);
};

} // namespace dltool::data
