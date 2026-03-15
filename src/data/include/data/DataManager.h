#pragma once

#include "DataExport.h"
#include "DataImporter.h"
#include "Datasets.h"
#include "GlobalFilter.h"
#include "ImageTags.h"
#include "Images.h"
#include "Labels.h"
#include "labelclasses.h"

#include <QObject>
#include <QtQml>

class QQmlApplicationEngine;

namespace dltool::data {
class ProjectDataBase;
} // namespace dltool::data

namespace dltool::data {

class DATA_API DataManager : public QObject
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

public:
    DataManager(const int method, data::ProjectDataBase *database, QObject *parent = nullptr);
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

    Q_INVOKABLE QList<QString> getAllDatasetsName() const;

    Q_INVOKABLE int     getDatasetId(const QString &dataset_name) const;
    Q_INVOKABLE QString getDatasetName(const int dataset_id) const;

    Q_INVOKABLE void addDataset(const QString &name);
    Q_INVOKABLE void updateDataset(const int64_t dataset_id, const QString &name);
    Q_INVOKABLE void deleteDataset(const int64_t dataset_id);

    Q_INVOKABLE void importData(const int64_t dataset_id, const int data_format, const QString &image_dir,
                                const QString &data_dir);

    Q_INVOKABLE void deleteSelectedImages();

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
    Q_INVOKABLE void updateLabels(const std::vector<int64_t> &label_ids, const std::vector<QVariantMap> &data);
    Q_INVOKABLE void updateLabelsClass(const std::vector<int64_t> &label_ids,
                                       const std::vector<int64_t> &label_class_ids);
    Q_INVOKABLE void deleteLabels(const std::vector<int64_t> &label_ids);

    Q_INVOKABLE void addTagClass(const QString &name);

    /**
     * @brief 初始化 QML 引擎，注册图像提供器
     * @param engine QML 应用引擎指针
     */
    void initializeQmlEngine(QQmlApplicationEngine *engine);

private:
    void init(const int method);

    void updateDatasetsStats();

    /**
     * @brief 处理导入器的 dataReady 信号
     */
    void handleDataReady(bool success, int64_t dataset_id, std::vector<QString> image_paths,
                         std::vector<int64_t> image_widths, std::vector<int64_t> image_heights,
                         std::map<QString, QString> label_class_info, std::vector<ImportedLabel> labels);

    data::ProjectDataBase *database_{nullptr};

    DatasetsListModel       *datasets_{nullptr};
    ImageInstancesListModel *image_instances_{nullptr};
    LabelClassesListModel   *label_classes_{nullptr};
    ImageTagsListModel      *image_tags_{nullptr};
    LabelInstancesListModel *label_instances_{nullptr};
    ImageLabelsListModel    *image_labels_list_{nullptr};
    ImageLabelsTableModel   *image_labels_table_{nullptr};

    ImageInfoListModel *image_info_{nullptr};

    GlobalFilter *global_filter_{nullptr};

    int method_{0}; // 标签数据类型
};

} // namespace dltool::data
