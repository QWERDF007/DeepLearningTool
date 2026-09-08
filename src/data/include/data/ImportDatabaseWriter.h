#pragma once

#include "dltool/data/Export.h"
#include "data/DataIO.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <map>
#include <memory>
#include <vector>

namespace dltool::data {

class DATA_API ImportDatabaseWriter : public QObject
{
    Q_OBJECT

public:
    struct Stats
    {
        int64_t imported_images{0};
        int64_t imported_labels{0};
        int64_t skipped_labels{0};
    };

    ImportDatabaseWriter(QString database_path, int method, int data_format, int64_t dataset_id,
                         std::map<QString, QString> label_class_groups, QPointer<DataIO> importer,
                         QObject *parent = nullptr);
    ~ImportDatabaseWriter() override;

public slots:
    void onDataBatchReady(int64_t dataset_id, std::vector<QString> image_paths,
                          std::vector<int64_t> image_widths, std::vector<int64_t> image_heights,
                          std::map<QString, QString> label_class_info,
                          std::vector<dltool::data::ImportedLabel> labels, int64_t processed_images,
                          int64_t total_images);

    void onImporterFinished(bool success, std::vector<int64_t> image_ids,
                            std::vector<int64_t> label_class_ids);

signals:
    void finished(bool success, const QString &message, const dltool::data::ImportDatabaseWriter::Stats &stats);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace dltool::data

Q_DECLARE_METATYPE(dltool::data::ImportDatabaseWriter::Stats)

