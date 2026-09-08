#pragma once

#include "DatasetIO.h"
#include "DataOperationWorkflow.h"
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

namespace dltool::data {

struct DATA_API ImportedLabel
{
    QString     label_class_name;
    QVariantMap data;
    QString     image_path;
};

class DATA_API SafeExportScope
{
public:
    explicit SafeExportScope(const QString &target_dir);
    ~SafeExportScope();

    SafeExportScope(const SafeExportScope &) = delete;
    SafeExportScope &operator=(const SafeExportScope &) = delete;

    bool           isValid() const { return valid_; }
    const QString &stagingDir() const { return staging_dir_; }
    const QString &targetDir() const { return target_dir_; }
    const QString &error() const { return error_; }

    bool publish(QString &err_msg);
    void discard();

private:
    QString target_dir_;
    QString staging_dir_;
    QString error_;
    bool    valid_{false};
    bool    published_{false};
};

class DATA_API DataIO : public QObject
{
    Q_OBJECT

public:
    // Each batch is synchronously committed on the GUI thread so the background
    // parser cannot outrun it.  Keep the batch bounded to let the event loop paint
    // and process input between commits for projects with many labels.
    static constexpr std::size_t ImportBatchImageCount = 256;

    explicit DataIO(QObject *parent = nullptr);
    ~DataIO() override;

    static DataIO *createIO(int data_format, QObject *parent = nullptr);

    /**
     * @brief 校验导出器报告成功后实际生成的目录产物。
     *
     * 该校验在导出 worker 线程内执行，调用方只有在目录结构和关键文件
     * 完整时才能观察到 exportFinished(true)。
     */
    static bool validateExportOutput(int data_format, const ExportDataset &dataset, const QString &output_dir,
                                     const QVariantMap &options, QString &err_msg);

    static bool checkExportSourceCollision(const ExportDataset &dataset, const QString &target_dir,
                                           int data_format, QString &err_msg);

    void setTargetMethod(int method) { target_method_ = method; }
    void setTaskId(const QString &task_id) { task_id_ = task_id; }
    QString taskId() const { return task_id_; }
    void requestCancel();
    bool isCancelRequested() const;
    /**
     * @brief 等待当前数据操作的工作线程退出。
     *
     * 返回时工作函数已经退出；完成信号仍由原有 Qt 事件队列负责投递。
     */
    bool waitForDone(int timeout_ms = -1) const;

    virtual void startImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir);
    virtual void startScanLabelClasses(const QString &image_dir, const QString &data_dir);
    virtual void startExport(ExportDataset dataset, const QString &output_dir,
                             const QVariantMap &options = {});

signals:
    void importFinished(bool success, std::vector<int64_t> image_ids, std::vector<int64_t> label_class_ids);
    void dataBatchReady(int64_t dataset_id, std::vector<QString> image_paths, std::vector<int64_t> image_widths,
                        std::vector<int64_t> image_heights, std::map<QString, QString> label_class_info,
                        std::vector<ImportedLabel> labels, int64_t processed_images, int64_t total_images);
    void labelClassesScanned(bool success, std::map<QString, QString> label_class_info, const QString &message);
    void exportFinished(bool success, const QString &message);

protected:
    int             target_method_{-1};
    QString         task_id_;
    std::atomic_bool cancel_requested_{false};
    DataOperationWorkflow::HandlePtr operation_handle_;

    void updateProgress(int progress, const QString &message);
    void runInThread(std::function<void()> work, std::function<void(const QString &error)> on_failure = {});
    bool importImagesOnly(int64_t dataset_id, const QString &image_dir, const QString &format_name,
                          int thread_count);
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
    void startExport(ExportDataset dataset, const QString &output_dir,
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
                  double polygon_approx_epsilon_ratio, int thread_count);
    void doScanLabelClasses(const QString &data_dir);
    void doExport(ExportDataset dataset, QString output_dir, int thread_count);

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
    void startExport(ExportDataset dataset, const QString &output_dir,
                     const QVariantMap &options = {}) override;

private:
    void doImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir, int thread_count);
    void doScanLabelClasses(const QString &image_dir, const QString &data_dir);
    void doExport(ExportDataset dataset, QString output_dir, int thread_count);

    bool        parseLabelMeJson(const QString &json_path, LabelMeData &data);
    QVariantMap convertShapeToLabelData(const LabelMeShape &shape, int source_image_width, int source_image_height,
                                        int image_width, int image_height, bool convert_rectangle_to_polygon);
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
    void startExport(ExportDataset dataset, const QString &output_dir,
                     const QVariantMap &options = {}) override;

private:
    struct MaskGeometry
    {
        QRect                             bbox;
        std::vector<std::vector<QPointF>> polygons;
        int                               mask_width{0};
        int                               mask_height{0};
    };

    void doImport(int64_t dataset_id, const QString &image_dir, const QString &data_dir,
                  double polygon_approx_epsilon_ratio, int thread_count);
    void doScanLabelClasses(const QString &data_dir);
    void doExport(ExportDataset dataset, QString output_dir, QVariantMap options, int thread_count);

    std::vector<QString>        scanMaskFiles(const QString &mask_dir) const;
    bool                        readMaskGeometry(const QString &mask_path, MaskGeometry &geometry,
                                                 double polygon_approx_epsilon_ratio) const;
    QVariantMap                 maskToLabelData(const std::vector<QPointF> &polygon, int mask_width, int mask_height,
                                                int image_width, int image_height) const;
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
    void startExport(ExportDataset dataset, const QString &output_dir,
                     const QVariantMap &options = {}) override;

private:
    void doImport(int64_t dataset_id, const QString &image_dir, int thread_count);
    void doScanLabelClasses(const QString &image_dir);
    void doExport(ExportDataset dataset, QString output_dir, int thread_count);
};

} // namespace dltool::data
