#pragma once

#include "DatasetIO.h"
#include "dltool/data/Export.h"

#include <QObject>
#include <QPointF>
#include <QRect>
#include <QString>
#include <QVariantMap>
#include <atomic>
#include <cstddef>
#include <functional>
#include <map>
#include <vector>

namespace dltool::database {
class ProjectDataBase;
}

namespace dltool::data {

struct DATA_API ImportedLabel
{
    QString     label_class_name;
    QVariantMap data;
    QString     image_path;
};

class DATA_API DataIO : public QObject
{
    Q_OBJECT

public:
    // Each batch is synchronously committed on the GUI thread so the background
    // parser cannot outrun it.  Keep the batch bounded to let the event loop paint
    // and process input between commits for projects with many labels.
    static constexpr std::size_t ImportBatchImageCount = 256;

    explicit DataIO(dltool::database::ProjectDataBase *database = nullptr, QObject *parent = nullptr);
    ~DataIO() override;

    static DataIO *createIO(int data_format, dltool::database::ProjectDataBase *database = nullptr,
                            QObject *parent = nullptr);

    void setTargetMethod(int method) { target_method_ = method; }
    void requestCancel();
    bool isCancelRequested() const;

    virtual void startImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir);
    virtual void startScanLabelClasses(const QString &image_dir, const QString &data_dir);
    virtual void startExport(const ExportDataset &dataset, const QString &output_dir,
                             const QVariantMap &options = {});

signals:
    void importFinished(bool success, std::vector<int64_t> image_ids, std::vector<int64_t> label_class_ids);
    void dataBatchReady(int64_t dataset_id, std::vector<QString> image_paths, std::vector<int64_t> image_widths,
                        std::vector<int64_t> image_heights, std::map<QString, QString> label_class_info,
                        std::vector<ImportedLabel> labels, int64_t processed_images, int64_t total_images);
    void labelClassesScanned(bool success, std::map<QString, QString> label_class_info, const QString &message);
    void exportFinished(bool success, const QString &message);

protected:
    dltool::database::ProjectDataBase *database_{nullptr};
    int                                target_method_{-1};
    std::atomic_bool                   cancel_requested_{false};

    void updateProgress(int progress, const QString &message);
    void runInThread(std::function<void()> work);
    bool importImagesOnly(int64_t dataset_id, const QString &image_dir, const QString &format_name);
};

// ============================================================================
// COCO JSON format
// ============================================================================

class DATA_API COCOIO : public DataIO
{
    Q_OBJECT

public:
    using DataIO::DataIO;

    void startImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir) override;
    void startScanLabelClasses(const QString &image_dir, const QString &data_dir) override;
    void startExport(const ExportDataset &dataset, const QString &output_dir,
                     const QVariantMap &options = {}) override;

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

    void doImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir,
                  double polygon_approx_epsilon_ratio);
    void doScanLabelClasses(const QString &data_dir);
    void doExport(ExportDataset dataset, QString output_dir);

    QString findCocoJsonFile(const QString &data_path) const;
    bool    looksLikeCocoJson(const QString &json_path) const;
    QString resolveImagePath(const QString &image_dir, const QString &file_name,
                             const std::map<QString, QString> &image_file_index) const;
};

// ============================================================================
// LabelMe per-image JSON format
// ============================================================================

class DATA_API LabelMeIO : public DataIO
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

    using DataIO::DataIO;

    void startImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir) override;
    void startScanLabelClasses(const QString &image_dir, const QString &data_dir) override;
    void startExport(const ExportDataset &dataset, const QString &output_dir,
                     const QVariantMap &options = {}) override;

private:
    void doImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir);
    void doScanLabelClasses(const QString &image_dir, const QString &data_dir);
    void doExport(ExportDataset dataset, QString output_dir);

    bool        parseLabelMeJson(const QString &json_path, LabelMeData &data);
    QVariantMap convertShapeToLabelData(const LabelMeShape &shape, int image_width, int image_height,
                                        bool convert_rectangle_to_polygon);
};

// ============================================================================
// Mask (PNG/BMP/TIFF) format
// ============================================================================

class DATA_API MaskIO : public DataIO
{
    Q_OBJECT

public:
    using DataIO::DataIO;

    void startImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir) override;
    void startScanLabelClasses(const QString &image_dir, const QString &data_dir) override;
    void startExport(const ExportDataset &dataset, const QString &output_dir,
                     const QVariantMap &options = {}) override;

private:
    struct MaskGeometry
    {
        QRect                bbox;
        std::vector<QPointF> polygon;
        int                  mask_width{0};
        int                  mask_height{0};
    };

    void doImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir,
                  double polygon_approx_epsilon_ratio);
    void doScanLabelClasses(const QString &data_dir);
    void doExport(ExportDataset dataset, QString output_dir, QVariantMap options);

    std::vector<QString>        scanMaskFiles(const QString &mask_dir) const;
    bool                        readMaskGeometry(const QString &mask_path, MaskGeometry &geometry,
                                                 double polygon_approx_epsilon_ratio) const;
    QVariantMap                 maskToLabelData(const MaskGeometry &geometry, int image_width, int image_height) const;
    QString                     labelClassNameForMask(const QString &mask_path, const QString &mask_root) const;
    QString                     imageStemForMask(const QString &mask_path, const QString &mask_root,
                                                 const QString &mask_stem) const;
    std::map<QString, QString>  loadQueryNameMap(const QString &dir_path) const;
};

// ============================================================================
// Folder-as-class format
// ============================================================================

class DATA_API FolderIO : public DataIO
{
    Q_OBJECT

public:
    using DataIO::DataIO;

    void startImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir) override;
    void startScanLabelClasses(const QString &image_dir, const QString &data_dir) override;
    void startExport(const ExportDataset &dataset, const QString &output_dir,
                     const QVariantMap &options = {}) override;

private:
    void doImport(int64_t dataset_id, const QString &image_dir);
    void doScanLabelClasses(const QString &image_dir);
    void doExport(ExportDataset dataset, QString output_dir);
};

} // namespace dltool::data
