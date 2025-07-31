#include "project/DataManager.h"

#include "data/DataBase.h"
#include "data/DataFormat.h"
#include "data/LabelData.h"

#include <spdlog/spdlog.h>

namespace dltool::project {

DataManager::DataManager(const int method, data::ProjectDataBase *database, QObject *parent)
    : QObject(parent)
    , database_(database)
{
    init(method);
}

DataManager::~DataManager() {}

void DataManager::init(const int method)
{
    datasets_           = new DatasetsListModel(database_, this);
    image_instances_    = new ImageInstancesListModel(database_, this);
    label_classes_      = new LabelClassesListModel(database_, this);
    image_tags_         = new ImageTagsListModel(database_, image_instances_, this);
    label_instances_    = new LabelInstancesListModel(database_, image_instances_, label_classes_,
                                                      data::createLabelDataHelper(method), this);
    image_labels_list_  = new ImageLabelsListModel(image_instances_, label_instances_, label_classes_, this);
    image_labels_table_ = new ImageLabelsTableModel(image_instances_, label_instances_, label_classes_, this);
    image_info_         = new ImageInfoListModel(datasets_, image_instances_, label_classes_, label_instances_, this);

    connect(image_instances_, &ImageInstancesListModel::currentImageChanged, image_labels_list_,
            &ImageLabelsListModel::onCurrentImageChanged);
    connect(image_instances_, &ImageInstancesListModel::currentImageChanged, image_labels_table_,
            &ImageLabelsTableModel::onCurrentImageChanged);
    connect(image_instances_, &ImageInstancesListModel::currentImageChanged, image_info_,
            &ImageInfoListModel::onCurrentImageChanged);

    connect(image_instances_->selection(), &QItemSelectionModel::selectionChanged, image_tags_,
            &ImageTagsListModel::updateStats);
    connect(image_instances_->selection(), &QItemSelectionModel::currentChanged, image_tags_,
            &ImageTagsListModel::updateStats);

    std::vector<int64_t> image_ids   = image_instances_->getAllImageIds();
    std::vector<int64_t> dataset_ids = image_instances_->getImagesDatasetIds(image_ids);

    std::vector<std::vector<int64_t>> images_label_ids = label_instances_->getImagesLabelIds(image_ids);
    std::vector<std::vector<int64_t>> images_tag_ids   = image_tags_->getImagesTagIds(image_ids);

    datasets_->addImages(dataset_ids, image_ids);
    datasets_->setStats(dataset_ids, image_ids, images_label_ids);

    image_instances_->addImagesLabelIds(image_ids, images_label_ids);
    image_instances_->addImagesTagIds(image_ids, images_tag_ids);
}

QList<QString> DataManager::getAllDatasetsName() const
{
    return datasets_->getAllDatasetsName();
}

int DataManager::getDatasetId(const QString &dataset_name) const
{
    return datasets_->getDatasetId(dataset_name);
}

QString DataManager::getDatasetName(const int dataset_id) const
{
    return datasets_->getDatasetName(dataset_id);
}

void DataManager::addDataset(const QString &name)
{
    datasets_->addDataset(name);
}

void DataManager::updateDataset(const int64_t dataset_id, const QString &name)
{
    datasets_->updateDataset(dataset_id, name);
}

void DataManager::deleteDataset(const int64_t dataset_id)
{
    std::vector<int64_t> image_ids;
    image_instances_->deleteImages(dataset_id, image_ids);
    datasets_->deleteDataset(dataset_id);
}

void DataManager::importData(const int64_t dataset_id, const int data_format, const QString &image_dir,
                             const QString &data_dir)
{
    qInfo() << __FUNCTION__ << __LINE__ << "dataset_id" << dataset_id << "data_format" << data_format << "image_dir"
            << image_dir << "data_dir" << data_dir;
    if (!data::DataFormat::isDataFormatSupported(data_format))
    {
        spdlog::error("导入数据失败, 数据格式不支持: {}", data_format);
        return;
    }
    std::vector<int64_t> image_ids;
    image_instances_->addImages(dataset_id, image_dir, image_ids);
    std::vector<int64_t> dataset_ids(image_ids.size(), dataset_id);
    datasets_->addImages(dataset_ids, image_ids);
}

void DataManager::deleteSelectedImages()
{
    std::vector<int64_t>              image_ids        = image_instances_->getSelectedImagesId();
    std::vector<int64_t>              dataset_ids      = image_instances_->getDatasetIds(image_ids);
    std::vector<std::vector<int64_t>> images_label_ids = image_instances_->getLabelIds(image_ids);
    std::vector<int64_t>              label_ids;
    for (const std::vector<int64_t> &_label_ids : images_label_ids)
    {
        label_ids.insert(label_ids.end(), _label_ids.begin(), _label_ids.end());
    }
    datasets_->deleteImages(dataset_ids, image_ids);
    image_tags_->removeImagesTags(image_ids);
    image_instances_->deleteImages(image_ids);
    label_instances_->deleteLabels(label_ids);
    updateDatasetsStats();
}

void DataManager::addLabelClass(const QString &name, const QString &color, const QString &shortcut)
{
    label_classes_->addLabelClass(name, color, shortcut);
}

void DataManager::updateLabelClass(const int64_t label_class_id, const QString &name, const QString &color,
                                   const QString &shortcut, const int64_t ordinal_index)
{
    label_classes_->updateLabelClass(label_class_id, name, color, shortcut, ordinal_index);
}

void DataManager::deleteLabelClass(const int64_t label_class_id)
{
    // TODO: 删除对应标签类别的标注
    label_classes_->deleteLabelClass(label_class_id);
}

void DataManager::addLabels(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_class_ids,
                            const std::vector<QVariantMap> &data)
{
    std::vector<int64_t> label_ids;
    label_instances_->addLabels(label_ids, image_ids, label_class_ids, data);
    image_instances_->addImagesLabelIds(image_ids, label_ids);
    image_labels_list_->addLabels(image_ids, label_ids);
    image_labels_table_->addLabels(image_ids, label_ids);
    updateDatasetsStats();
    image_info_->updateLabelInfo();
}

void DataManager::updateLabels(const std::vector<int64_t> &label_ids, const std::vector<QVariantMap> &data)
{
    std::vector<int64_t> image_ids = label_instances_->getImageIds(label_ids);
    label_instances_->updateLabelsData(label_ids, image_ids, data);
    image_labels_list_->updateLabels(image_ids, label_ids);
    image_labels_table_->updateLabels(image_ids, label_ids);
    // updateDatasetsStats();
}

void DataManager::deleteLabels(const std::vector<int64_t> &label_ids)
{
    std::vector<int64_t> image_ids = label_instances_->getImageIds(label_ids);
    label_instances_->deleteLabels(label_ids);
    image_instances_->deleteImagesLabelIds(image_ids, label_ids);
    image_labels_list_->deleteLabels(image_ids, label_ids);
    image_labels_table_->deleteLabels(image_ids, label_ids);
    updateDatasetsStats();
    image_info_->updateLabelInfo();
}

void DataManager::addTagClass(const QString &name)
{
    image_tags_->addTagClass(name);
}

void DataManager::updateDatasetsStats()
{
    if (image_instances_ == nullptr || datasets_ == nullptr)
        return;
    std::vector<int64_t>              dataset_ids, image_ids;
    std::vector<std::vector<int64_t>> images_label_ids;
    image_instances_->getAllDatasetsImagesLabels(dataset_ids, image_ids, images_label_ids);
    datasets_->setStats(dataset_ids, image_ids, images_label_ids);
}

} // namespace dltool::project
