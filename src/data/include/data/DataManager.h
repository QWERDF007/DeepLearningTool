#pragma once

#include "CategoryStatisticsModel.h"
#include "DataExporter.h"
#include "DataImporter.h"
#include "Datasets.h"
#include "FilterItemsModel.h"
#include "GlobalFilter.h"
#include "ImageTags.h"
#include "Images.h"
#include "LabelClasses.h"
#include "Labels.h"
#include "dltool/data/Export.h"
#include "feature/FewShotLearningController.h"
#include "feature/FewShotLearningDataProvider.h"
#include "feature/ImageSearchController.h"
#include "feature/ImageSearchDataProvider.h"
#include "feature/RoiSearchController.h"
#include "feature/SmartAnnotationController.h"

#include <QMetaObject>
#include <QObject>
#include <QVariantList>
#include <QtQml>
#include <memory>
#include <vector>

class QQmlApplicationEngine;

namespace dltool::database {
class ProjectDataBase;
} // namespace dltool::database

namespace dltool::model {
class TaskManager;
} // namespace dltool::model

namespace dltool::data {

class DATA_API DataManager
    : public QObject
    , public dltool::feature::ImageSearchDataProvider
    , public dltool::feature::FewShotLearningDataProvider
{
    Q_OBJECT
    QML_NAMED_ELEMENT(DataManager)
    QML_UNCREATABLE("Can not create DataManager directly!")
    Q_PROPERTY(DatasetsListModel *datasets READ datasets CONSTANT FINAL)
    Q_PROPERTY(ImageInstancesListModel *imageInstances READ imageInstances CONSTANT FINAL)
    Q_PROPERTY(LabelClassesListModel *labelClasses READ labelClasses CONSTANT FINAL)
    Q_PROPERTY(ImageTagsListModel *imageTags READ imageTags CONSTANT FINAL)
    Q_PROPERTY(LabelInstancesListModel *labelInstances READ labelInstances CONSTANT FINAL)
    Q_PROPERTY(ImageLabelsListModel *imageLabelsList READ imageLabelsList CONSTANT FINAL)
    Q_PROPERTY(ImageLabelsTableModel *imageLabelsTable READ imageLabelsTable CONSTANT FINAL)
    Q_PROPERTY(ImageInfoListModel *imageInfo READ imageInfo CONSTANT FINAL)
    Q_PROPERTY(GlobalFilter *globalFilter READ globalFilter CONSTANT FINAL)
    Q_PROPERTY(dltool::feature::ImageSearchController *imageSearch READ imageSearch CONSTANT FINAL)
    Q_PROPERTY(dltool::feature::RoiSearchController *roiSearch READ roiSearch CONSTANT FINAL)
    Q_PROPERTY(dltool::feature::SmartAnnotationController *smartAnnotation READ smartAnnotation CONSTANT FINAL)
    Q_PROPERTY(dltool::feature::FewShotLearningController *fewShotLearning READ fewShotLearning CONSTANT FINAL)
    Q_PROPERTY(DatasetFilterItemsModel *datasetFilterItems READ datasetFilterItems CONSTANT FINAL)
    Q_PROPERTY(TagFilterItemsModel *tagFilterItems READ tagFilterItems CONSTANT FINAL)
    Q_PROPERTY(LabelClassFilterItemsModel *labelClassFilterItems READ labelClassFilterItems CONSTANT FINAL)
    Q_PROPERTY(CategoryStatisticsModel *categoryStatisticsModel READ categoryStatisticsModel CONSTANT FINAL)
    Q_PROPERTY(int method READ method CONSTANT FINAL)

public:
    DataManager(const int method, dltool::database::ProjectDataBase *database, QObject *parent = nullptr);
    ~DataManager();

    DatasetsListModel *datasets() const
    {
        return datasets_;
    }

    ImageInstancesListModel *imageInstances() const
    {
        return image_instances_;
    }

    LabelClassesListModel *labelClasses() const
    {
        return label_classes_;
    }

    ImageTagsListModel *imageTags() const
    {
        return image_tags_;
    }

    LabelInstancesListModel *labelInstances() const
    {
        return label_instances_;
    }

    ImageLabelsListModel *imageLabelsList() const
    {
        return image_labels_list_;
    }

    ImageLabelsTableModel *imageLabelsTable() const
    {
        return image_labels_table_;
    }

    ImageInfoListModel *imageInfo() const
    {
        return image_info_;
    }

    GlobalFilter *globalFilter() const
    {
        return global_filter_;
    }

    dltool::feature::ImageSearchController *imageSearch() const
    {
        return image_search_;
    }

    dltool::feature::RoiSearchController *roiSearch() const
    {
        return roi_search_;
    }

    dltool::feature::SmartAnnotationController *smartAnnotation() const
    {
        return smart_annotation_;
    }

    dltool::feature::FewShotLearningController *fewShotLearning() const
    {
        return few_shot_learning_;
    }

    DatasetFilterItemsModel *datasetFilterItems() const
    {
        return dataset_filter_items_;
    }

    TagFilterItemsModel *tagFilterItems() const
    {
        return tag_filter_items_;
    }

    LabelClassFilterItemsModel *labelClassFilterItems() const
    {
        return label_class_filter_items_;
    }

    CategoryStatisticsModel *categoryStatisticsModel() const
    {
        return category_statistics_model_;
    }

    int method() const override
    {
        return method_;
    }

    QString databasePath() const override;

    void setTaskManager(dltool::model::TaskManager *task_manager);

    Q_INVOKABLE QList<QString> getAllDatasetsName() const;
    Q_INVOKABLE QVariantList   getAllLabelClassIds() const;

    Q_INVOKABLE int     getDatasetId(const QString &dataset_name) const;
    Q_INVOKABLE QString getDatasetName(const int dataset_id) const;

    Q_INVOKABLE void addDataset(const QString &name);
    Q_INVOKABLE void updateDataset(const int64_t dataset_id, const QString &name);
    Q_INVOKABLE void deleteDataset(const int64_t dataset_id);

    Q_INVOKABLE void importData(const int64_t dataset_id, const int data_format, const QString &image_dir,
                                const QString &data_dir);
    Q_INVOKABLE void exportDataset(const int64_t dataset_id, const int data_format, const QString &output_dir);

    Q_INVOKABLE void deleteSelectedImages();
    Q_INVOKABLE void copySelectedImagesToDataset(const int64_t dataset_id);
    Q_INVOKABLE void moveSelectedImagesToDataset(const int64_t dataset_id);

    Q_INVOKABLE void addLabelClass(const QString &name, const QString &color, const QString &shortcut);

    /**
     * @brief 更新标签类别的名称、颜色、快捷键、序号索引
     * @param label_class_id
     * @param name
     * @param color
     * @param shortcut
     * @param ordinal_index
     */
    Q_INVOKABLE void updateLabelClass(const int64_t label_class_id, const QString &name, const QString &color,
                                      const QString &shortcut, const int64_t ordinal_index);
    Q_INVOKABLE void deleteLabelClass(const int64_t label_class_id);

    Q_INVOKABLE void addLabels(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_class_ids,
                               const std::vector<QVariantMap> &data);
    Q_INVOKABLE bool addLabel(const int64_t image_id, const int64_t label_class_id, const QVariantMap &data);
    Q_INVOKABLE void updateLabels(const std::vector<int64_t> &label_ids, const std::vector<QVariantMap> &data);
    Q_INVOKABLE void updateLabelsClass(const std::vector<int64_t> &label_ids,
                                       const std::vector<int64_t> &label_class_ids);
    Q_INVOKABLE void deleteLabels(const std::vector<int64_t> &label_ids);
    Q_INVOKABLE void duplicateSelectedLabels();

    Q_INVOKABLE void addTagClass(const QString &name);

    /**
     * @brief 初始化 QML 引擎，注册图像提供器
     * @param engine QML 应用引擎指针
     */
    void initializeQmlEngine(QQmlApplicationEngine *engine);

    Q_INVOKABLE QString getImageName(const int64_t image_id) const;
    Q_INVOKABLE QString getImagePath(const int64_t image_id) const;
    Q_INVOKABLE QString getImageDatasetName(const int64_t image_id) const;
    Q_INVOKABLE QString getImageTagName(const int64_t image_id) const;

    std::vector<int64_t>    selectedImageIds() const override;
    std::vector<int64_t>    allImageIds() const override;
    QString                 imagePath(int64_t image_id) const override;
    int64_t                 imageDatasetId(int64_t image_id) const override;
    std::vector<int64_t>    allLabelIds() const override;
    int64_t                 labelImageId(int64_t label_id) const override;
    int64_t                 labelClassId(int64_t label_id) const override;
    QVariantMap             labelData(int64_t label_id) const override;
    QString                 labelClassName(int64_t label_class_id) const override;
    QString                 datasetName(int64_t dataset_id) const override;
    void                    importMaskData(int64_t dataset_id, const QString &image_manifest_path,
                                           const QString &prediction_output_dir) override;
    QMetaObject::Connection connectImportFinished(
        QObject *context, dltool::feature::FewShotLearningDataProvider::ImportFinishedHandler handler) override;
    void disconnectImportFinished(const QMetaObject::Connection &connection) override;
    void clearImageSearchResults() override;
    void setImageSearchResults(const std::vector<int64_t> &image_ids, bool enable_filter) override;
    void clearLabelSearchResults() override;
    void setLabelSearchResults(const std::vector<int64_t> &label_ids, bool enable_filter) override;

signals:
    void dataImportFinished(bool success, const QString &message);

private:
    struct PendingImportTask;

    void init(const int method);
    void startAsyncLabelLoading();
    void handleAsyncLabelsLoaded(std::shared_ptr<std::vector<LoadedLabelInstance>> labels, bool success,
                                 const QString &err_msg, qint64 elapsed_ms);
    void rebuildLabelRelations();

    void updateDatasetsStats();

    ExportDataset buildExportDataset(const int64_t dataset_id) const;

    /**
     * @brief 处理导入器解析出的单个数据批次
     */
    void handleDataBatchReady(int64_t dataset_id, std::vector<QString> image_paths, std::vector<int64_t> image_widths,
                              std::vector<int64_t> image_heights, std::map<QString, QString> label_class_info,
                              std::vector<ImportedLabel> labels, int64_t processed_images, int64_t total_images);
    void handleImportFinished(bool success, std::vector<int64_t> image_ids, std::vector<int64_t> label_class_ids);
    bool addLabelsInternal(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_class_ids,
                           const std::vector<QVariantMap> &data, QString *err_msg = nullptr);
    bool writeImportBatch(int64_t dataset_id, const std::vector<QString> &image_paths,
                          const std::map<QString, QString> &label_class_info, const std::vector<ImportedLabel> &labels,
                          QString &err_msg);
    void finishBatchedImport(bool success, const QString &message);

    dltool::database::ProjectDataBase *database_{nullptr};

    DatasetsListModel       *datasets_{nullptr};
    ImageInstancesListModel *image_instances_{nullptr};
    LabelClassesListModel   *label_classes_{nullptr};
    ImageTagsListModel      *image_tags_{nullptr};
    LabelInstancesListModel *label_instances_{nullptr};
    ImageLabelsListModel    *image_labels_list_{nullptr};
    ImageLabelsTableModel   *image_labels_table_{nullptr};

    ImageInfoListModel *image_info_{nullptr};

    GlobalFilter                               *global_filter_{nullptr};
    dltool::feature::ImageSearchController     *image_search_{nullptr};
    dltool::feature::RoiSearchController       *roi_search_{nullptr};
    dltool::feature::SmartAnnotationController *smart_annotation_{nullptr};
    dltool::feature::FewShotLearningController *few_shot_learning_{nullptr};

    DatasetFilterItemsModel    *dataset_filter_items_{nullptr};
    TagFilterItemsModel        *tag_filter_items_{nullptr};
    LabelClassFilterItemsModel *label_class_filter_items_{nullptr};

    CategoryStatisticsModel *category_statistics_model_{nullptr};

    int method_{0}; // 标签数据类型

    bool                               import_running_{false};
    std::unique_ptr<PendingImportTask> pending_import_task_;

    bool labels_loading_{false};
    bool labels_changed_during_loading_{false};
};

} // namespace dltool::data
