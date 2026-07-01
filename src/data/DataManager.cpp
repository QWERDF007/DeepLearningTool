#include "data/DataManager.h"

#include "data/CategoryStatisticsModel.h"
#include "data/DataExporter.h"
#include "data/DataFormat.h"
#include "data/DataImporter.h"
#include "data/DatasetIO.h"
#include "data/GlobalFilter.h"
#include "data/LabelData.h"
#include "data/LabelInstanceImageProvider.h"
#include "database/DataBase.h"
#include "ui/ProgressManager.h"

#include <spdlog/spdlog.h>

#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMetaType>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QThread>
#include <algorithm>
#include <cstddef>
#include <map>
#include <memory>
#include <set>
#include <utility>

namespace dltool::data {

namespace {

bool isFatalDatabaseError(const QString &message)
{
    return message.contains(QStringLiteral("database disk image is malformed"), Qt::CaseInsensitive)
        || message.contains(QStringLiteral("file is not a database"), Qt::CaseInsensitive);
}

QString normalizedImagePath(const QString &path)
{
    QFileInfo file_info(path);
    QString   normalized = file_info.exists() ? file_info.canonicalFilePath() : file_info.absoluteFilePath();
    if (normalized.isEmpty())
        normalized = path;

    normalized = QDir::cleanPath(QDir::fromNativeSeparators(normalized));
#ifdef Q_OS_WIN
    normalized = normalized.toCaseFolded();
#endif
    return normalized;
}

QString clusterDatasetName(const QString &source_dataset_name, const int64_t cluster_id)
{
    if (cluster_id < 0)
        return QStringLiteral("%1-noise").arg(source_dataset_name);
    return QStringLiteral("%1-%2").arg(source_dataset_name).arg(cluster_id);
}

} // namespace

struct DataManager::PendingImportTask
{
    DataImporter *importer{nullptr};

    int64_t dataset_id{0};

    std::map<QString, int64_t> label_class_map;
    std::map<QString, int64_t> image_path_to_id;
    std::map<QString, int64_t> normalized_image_path_to_id;

    size_t  total_images{0};
    size_t  processed_images{0};
    size_t  imported_images{0};
    size_t  imported_labels{0};
    size_t  failed_batches{0};
    size_t  failed_images{0};
    size_t  failed_labels{0};
    int     skipped_labels{0};
    QString first_error_message;
    bool    fatal_error{false};
};

DataManager::DataManager(const int method, dltool::database::ProjectDataBase *database, QObject *parent)
    : QObject(parent)
    , database_(database)
    , method_(method)
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
                                                      data::createLabelDataHelper(method), false, this);
    image_labels_list_  = new ImageLabelsListModel(image_instances_, label_instances_, label_classes_, this);
    image_labels_table_ = new ImageLabelsTableModel(image_instances_, label_instances_, label_classes_, this);
    image_info_         = new ImageInfoListModel(datasets_, image_instances_, label_classes_, label_instances_, this);

    // Create GlobalFilter and initialize it with the models
    global_filter_ = new GlobalFilter(this, this);
    global_filter_->initializeFilterModules(this);

    // Create filter items models
    dataset_filter_items_     = new DatasetFilterItemsModel(this);
    tag_filter_items_         = new TagFilterItemsModel(this);
    label_class_filter_items_ = new LabelClassFilterItemsModel(this);

    // Create CategoryStatisticsModel
    category_statistics_model_ = new CategoryStatisticsModel(label_instances_, label_classes_, this);

    // Populate filter items models from datasets and tags
    dataset_filter_items_->populateFromDatasets(datasets_);
    tag_filter_items_->populateFromTags(image_tags_);
    label_class_filter_items_->populateFromLabelClasses(label_classes_);

    // Connect source model changes to refresh filter items models
    connect(datasets_, &QAbstractItemModel::rowsInserted, this,
            [this](const QModelIndex &, int, int) { dataset_filter_items_->populateFromDatasets(datasets_); });
    connect(datasets_, &QAbstractItemModel::rowsRemoved, this,
            [this](const QModelIndex &, int, int) { dataset_filter_items_->populateFromDatasets(datasets_); });
    connect(datasets_, &QAbstractItemModel::modelReset, this,
            [this]() { dataset_filter_items_->populateFromDatasets(datasets_); });

    connect(image_tags_, &QAbstractItemModel::rowsInserted, this,
            [this](const QModelIndex &, int, int) { tag_filter_items_->populateFromTags(image_tags_); });
    connect(image_tags_, &QAbstractItemModel::rowsRemoved, this,
            [this](const QModelIndex &, int, int) { tag_filter_items_->populateFromTags(image_tags_); });
    connect(image_tags_, &QAbstractItemModel::modelReset, this,
            [this]() { tag_filter_items_->populateFromTags(image_tags_); });

    connect(label_classes_, &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex &, int, int)
            { label_class_filter_items_->populateFromLabelClasses(label_classes_); });
    connect(label_classes_, &QAbstractItemModel::rowsRemoved, this, [this](const QModelIndex &, int, int)
            { label_class_filter_items_->populateFromLabelClasses(label_classes_); });
    connect(label_classes_, &QAbstractItemModel::modelReset, this,
            [this]() { label_class_filter_items_->populateFromLabelClasses(label_classes_); });

    connect(image_instances_, &ImageInstancesListModel::currentImageChanged, image_labels_list_,
            &ImageLabelsListModel::onCurrentImageChanged);
    connect(image_instances_, &ImageInstancesListModel::currentImageChanged, image_labels_table_,
            &ImageLabelsTableModel::onCurrentImageChanged);
    connect(image_instances_, &ImageInstancesListModel::currentImageChanged, image_info_,
            &ImageInfoListModel::onCurrentImageChanged);
    connect(global_filter_, &GlobalFilter::filterApplied, image_labels_list_,
            &ImageLabelsListModel::onCurrentImageChanged);
    connect(global_filter_, &GlobalFilter::filterApplied, image_labels_table_,
            &ImageLabelsTableModel::onCurrentImageChanged);

    connect(image_instances_->selection(), &QItemSelectionModel::selectionChanged, image_tags_,
            &ImageTagsListModel::updateStats);
    connect(image_instances_->selection(), &QItemSelectionModel::currentChanged, image_tags_,
            &ImageTagsListModel::updateStats);

    std::vector<int64_t> image_ids   = image_instances_->getAllImageIds();
    std::vector<int64_t> dataset_ids = image_instances_->getImagesDatasetIds(image_ids);

    std::vector<std::vector<int64_t>> images_label_ids(image_ids.size());
    std::vector<std::vector<int64_t>> images_tag_ids = image_tags_->getImagesTagIds(image_ids);

    datasets_->addImages(dataset_ids, image_ids);
    datasets_->setStats(dataset_ids, image_ids, images_label_ids);

    image_instances_->setImagesLabelIds(image_ids, images_label_ids);
    image_instances_->addImagesTagIds(image_ids, images_tag_ids);

    startAsyncLabelLoading();
}

void DataManager::startAsyncLabelLoading()
{
    if (database_ == nullptr || labels_loading_)
    {
        return;
    }

    labels_loading_                         = true;
    labels_changed_during_loading_          = false;
    const QString         database_path     = database_->path();
    const int             label_data_method = method_;
    QPointer<DataManager> manager(this);

    QThread *worker_thread = QThread::create(
        [manager, database_path, label_data_method]()
        {
            QElapsedTimer timer;
            timer.start();

            auto    loaded_labels = std::make_shared<std::vector<LoadedLabelInstance>>();
            bool    success       = false;
            QString err_msg;

            try
            {
                dltool::database::ProjectDataBase database(database_path);
                std::vector<int64_t>              label_ids;
                std::vector<int64_t>              image_ids;
                std::vector<int64_t>              label_class_ids;
                std::vector<int64_t>              label_types;
                std::vector<std::vector<uint8_t>> labels_data;

                success
                    = database.getAllLabels(label_ids, image_ids, label_class_ids, label_types, labels_data, err_msg);
                if (success)
                {
                    LabelDataHelper helper = data::createLabelDataHelper(label_data_method);
                    if (helper == nullptr)
                    {
                        success = false;
                        err_msg = QString("标签数据工厂未初始化");
                    }
                    else
                    {
                        loaded_labels->reserve(label_ids.size());
                        for (size_t i = 0; i < label_ids.size(); ++i)
                        {
                            LabelData label_data = helper->createLabelData();
                            label_data->fromBlob(labels_data[i]);

                            LoadedLabelInstance label;
                            label.label_id       = label_ids[i];
                            label.image_id       = image_ids[i];
                            label.label_class_id = label_class_ids[i];
                            label.data           = std::move(label_data);
                            loaded_labels->push_back(std::move(label));
                        }
                    }
                }
            }
            catch (const std::exception &e)
            {
                success = false;
                err_msg = QString::fromUtf8(e.what());
            }

            const qint64 elapsed_ms = timer.elapsed();
            if (manager)
            {
                QMetaObject::invokeMethod(
                    manager.data(),
                    [manager, loaded_labels, success, err_msg, elapsed_ms]()
                    {
                        if (manager)
                        {
                            manager->handleAsyncLabelsLoaded(loaded_labels, success, err_msg, elapsed_ms);
                        }
                    },
                    Qt::QueuedConnection);
            }
        });

    connect(worker_thread, &QThread::finished, worker_thread, &QObject::deleteLater);
    worker_thread->start();
}

void DataManager::handleAsyncLabelsLoaded(std::shared_ptr<std::vector<LoadedLabelInstance>> labels, bool success,
                                          const QString &err_msg, qint64 elapsed_ms)
{
    labels_loading_ = false;

    if (labels_changed_during_loading_)
    {
        labels_changed_during_loading_ = false;
        spdlog::info("项目标注后台加载期间发生修改，丢弃当前结果并重新加载");
        startAsyncLabelLoading();
        return;
    }

    if (!success || labels == nullptr)
    {
        spdlog::error("后台加载项目标注失败: {}", err_msg.toUtf8().constData());
        return;
    }

    label_instances_->replaceAllLabels(std::move(*labels));
    rebuildLabelRelations();

    spdlog::info("后台加载项目标注完成: {} 个标注, 耗时 {} ms", label_instances_->totalCount(), elapsed_ms);
}

void DataManager::rebuildLabelRelations()
{
    if (image_instances_ == nullptr || label_instances_ == nullptr || datasets_ == nullptr)
    {
        return;
    }

    std::vector<int64_t>              image_ids        = image_instances_->getAllImageIds();
    std::vector<int64_t>              dataset_ids      = image_instances_->getImagesDatasetIds(image_ids);
    std::vector<std::vector<int64_t>> images_label_ids = label_instances_->getImagesLabelIds(image_ids);

    image_instances_->setImagesLabelIds(image_ids, images_label_ids);
    datasets_->setStats(dataset_ids, image_ids, images_label_ids);

    if (image_labels_list_ != nullptr)
    {
        image_labels_list_->onCurrentImageChanged();
    }
    if (image_labels_table_ != nullptr)
    {
        image_labels_table_->onCurrentImageChanged();
    }
    if (image_info_ != nullptr)
    {
        image_info_->updateLabelInfo();
    }
}

QList<QString> DataManager::getAllDatasetsName() const
{
    return datasets_->getAllDatasetsName();
}

QVariantList DataManager::getAllLabelClassIds() const
{
    QVariantList ids;
    if (label_classes_ == nullptr)
        return ids;

    ids.reserve(label_classes_->rowCount());
    for (int row = 0; row < label_classes_->rowCount(); ++row)
    {
        ids.push_back(label_classes_->data(label_classes_->index(row, 0), LabelClassesListModel::LabelClassIdRole));
    }
    return ids;
}

int DataManager::getDatasetId(const QString &dataset_name) const
{
    return datasets_->getDatasetId(dataset_name);
}

QString DataManager::databasePath() const
{
    return database_ ? database_->path() : QString();
}

std::vector<int64_t> DataManager::selectedImageIds() const
{
    return image_instances_ ? image_instances_->getSelectedImagesId() : std::vector<int64_t>{};
}

std::vector<int64_t> DataManager::allImageIds() const
{
    return image_instances_ ? image_instances_->getAllImageIds() : std::vector<int64_t>{};
}

QString DataManager::imagePath(int64_t image_id) const
{
    return image_instances_ ? image_instances_->getImagePath(image_id) : QString();
}

int64_t DataManager::imageDatasetId(int64_t image_id) const
{
    return image_instances_ ? image_instances_->getImageDatasetId(image_id) : -1;
}

std::vector<int64_t> DataManager::allLabelIds() const
{
    std::vector<int64_t> label_ids;
    if (label_instances_ == nullptr)
    {
        return label_ids;
    }

    const auto &instances = label_instances_->getAllLabelInstances();
    label_ids.reserve(instances.size());
    for (const auto &[label_id, _] : instances)
    {
        label_ids.push_back(label_id);
    }
    return label_ids;
}

int64_t DataManager::labelImageId(int64_t label_id) const
{
    return label_instances_ ? label_instances_->getImageId(label_id) : -1;
}

int64_t DataManager::labelClassId(int64_t label_id) const
{
    return label_instances_ ? label_instances_->getLabelClassId(label_id) : -1;
}

QVariantMap DataManager::labelData(int64_t label_id) const
{
    if (label_instances_ == nullptr)
    {
        return {};
    }

    const LabelInstance *instance = label_instances_->getLabelInstance(label_id);
    if (instance == nullptr || instance->data() == nullptr)
    {
        return {};
    }
    return instance->data()->dataMap();
}

QString DataManager::labelClassName(int64_t label_class_id) const
{
    return label_classes_ ? label_classes_->getLabelClassName(static_cast<int>(label_class_id)) : QString();
}

QString DataManager::datasetName(int64_t dataset_id) const
{
    return datasets_ ? datasets_->getDatasetName(static_cast<int>(dataset_id)) : QString();
}

void DataManager::importMaskData(int64_t dataset_id, const QString &image_manifest_path,
                                 const QString &prediction_output_dir)
{
    importData(dataset_id, DataFormat::Mask, image_manifest_path, prediction_output_dir);
}

QMetaObject::Connection DataManager::connectImportFinished(
    QObject *context, dltool::feature::FewShotLearningDataProvider::ImportFinishedHandler handler)
{
    return connect(this, &DataManager::dataImportFinished, context, std::move(handler));
}

void DataManager::disconnectImportFinished(const QMetaObject::Connection &connection)
{
    QObject::disconnect(connection);
}

void DataManager::clearImageSearchResults()
{
    if (global_filter_ != nullptr)
    {
        global_filter_->clearImageSearchResults();
    }
}

void DataManager::setImageSearchResults(const std::vector<int64_t> &image_ids, bool enable_filter)
{
    if (global_filter_ != nullptr)
    {
        global_filter_->setImageSearchResults(image_ids, enable_filter);
    }
}

void DataManager::clearLabelSearchResults()
{
    if (global_filter_ != nullptr)
    {
        global_filter_->clearLabelSearchResults();
    }
}

void DataManager::setLabelSearchResults(const std::vector<int64_t> &label_ids, bool enable_filter)
{
    if (global_filter_ != nullptr)
    {
        global_filter_->setLabelSearchResults(label_ids, enable_filter);
    }
}

bool DataManager::applyImageClusterAssignments(
    const std::vector<dltool::feature::ImageSearchDataProvider::ImageClusterAssignment> &assignments,
    const bool include_noise,
    dltool::feature::ImageSearchDataProvider::ImageClusterApplyMode apply_mode,
    dltool::feature::ImageSearchDataProvider::ImageClusterApplyResult &result,
    QString &err_msg)
{
    result = {};
    err_msg.clear();

    if (datasets_ == nullptr || image_instances_ == nullptr)
    {
        err_msg = QStringLiteral("数据模型未初始化");
        return false;
    }
    if (assignments.empty())
    {
        err_msg = QStringLiteral("没有图像聚类结果");
        return false;
    }

    struct PendingClusterImage
    {
        int64_t image_id{0};
        int64_t source_dataset_id{-1};
        QString target_dataset_name;
        QString path;
    };

    std::vector<PendingClusterImage> pending_images;
    std::set<QString>                target_dataset_names;
    std::map<int64_t, QString>       source_dataset_names;
    pending_images.reserve(assignments.size());

    for (const auto &assignment : assignments)
    {
        if (assignment.cluster_id < 0 && !include_noise)
        {
            ++result.skipped_noise_count;
            continue;
        }

        ImageInstance *image = image_instances_->getImageInstance(assignment.image_id);
        if (image == nullptr)
        {
            spdlog::warn("跳过无效聚类图像 ID: {}", assignment.image_id);
            continue;
        }

        const int64_t source_dataset_id = image->datasetId();
        if (source_dataset_id < 0)
        {
            spdlog::warn("跳过无效聚类图像数据集: image_id={}", assignment.image_id);
            continue;
        }

        auto source_name_it = source_dataset_names.find(source_dataset_id);
        if (source_name_it == source_dataset_names.end())
        {
            source_name_it = source_dataset_names
                                 .emplace(source_dataset_id,
                                          datasets_->getDatasetName(static_cast<int>(source_dataset_id)))
                                 .first;
        }

        const QString &source_dataset_name = source_name_it->second;
        if (source_dataset_name.isEmpty())
        {
            spdlog::warn("跳过无效聚类图像数据集名称: image_id={}, dataset_id={}", assignment.image_id,
                         source_dataset_id);
            continue;
        }

        const QString target_name = clusterDatasetName(source_dataset_name, assignment.cluster_id);
        target_dataset_names.insert(target_name);
        pending_images.push_back({assignment.image_id, source_dataset_id, target_name, image->path()});
    }

    std::map<QString, int64_t> target_dataset_ids_by_name;
    std::vector<QString>       missing_dataset_names;
    for (const QString &target_name : target_dataset_names)
    {
        const int64_t existing_dataset_id = datasets_->getDatasetId(target_name);
        if (existing_dataset_id >= 0)
        {
            target_dataset_ids_by_name[target_name] = existing_dataset_id;
            continue;
        }
        missing_dataset_names.push_back(target_name);
    }

    if (!missing_dataset_names.empty())
    {
        std::vector<int64_t> created_dataset_ids;
        if (!datasets_->addDatasets(missing_dataset_names, created_dataset_ids))
        {
            err_msg = QStringLiteral("批量创建聚类数据集失败");
            return false;
        }
        if (created_dataset_ids.size() != missing_dataset_names.size())
        {
            err_msg = QStringLiteral("批量创建聚类数据集后返回数量不一致");
            return false;
        }
        for (size_t i = 0; i < missing_dataset_names.size(); ++i)
        {
            target_dataset_ids_by_name[missing_dataset_names[i]] = created_dataset_ids[i];
        }
    }

    std::vector<int64_t> planned_target_dataset_ids;
    std::set<int64_t>    unique_target_dataset_ids;
    planned_target_dataset_ids.reserve(pending_images.size());

    for (const PendingClusterImage &pending_image : pending_images)
    {
        const auto target_it = target_dataset_ids_by_name.find(pending_image.target_dataset_name);
        if (target_it == target_dataset_ids_by_name.end())
        {
            err_msg = QStringLiteral("未找到聚类目标数据集: %1").arg(pending_image.target_dataset_name);
            return false;
        }

        const int64_t target_dataset_id = target_it->second;
        unique_target_dataset_ids.insert(target_dataset_id);
        planned_target_dataset_ids.push_back(target_dataset_id);
    }

    result.target_dataset_count = unique_target_dataset_ids.size();

    if (apply_mode == dltool::feature::ImageSearchDataProvider::ImageClusterApplyMode::Copy)
    {
        if (label_instances_ == nullptr || image_tags_ == nullptr)
        {
            err_msg = QStringLiteral("标签或标注模型未初始化");
            return false;
        }

        std::vector<int64_t> copy_source_image_ids;
        std::vector<int64_t> copy_target_dataset_ids;
        std::vector<QString> copy_paths;
        copy_source_image_ids.reserve(pending_images.size());
        copy_target_dataset_ids.reserve(pending_images.size());
        copy_paths.reserve(pending_images.size());

        for (size_t i = 0; i < pending_images.size(); ++i)
        {
            const PendingClusterImage &pending_image = pending_images[i];
            if (pending_image.path.isEmpty())
            {
                err_msg = QStringLiteral("复制聚类图像失败，图像路径为空: image_id=%1").arg(pending_image.image_id);
                return false;
            }
            copy_source_image_ids.push_back(pending_image.image_id);
            copy_target_dataset_ids.push_back(planned_target_dataset_ids[i]);
            copy_paths.push_back(pending_image.path);
        }

        if (!copy_source_image_ids.empty())
        {
            const auto source_label_ids = image_instances_->getLabelIds(copy_source_image_ids);
            const auto source_tag_ids   = image_tags_->getImagesTagIds(copy_source_image_ids);

            std::vector<int64_t> copied_image_ids;
            if (!image_instances_->addImages(copy_target_dataset_ids, copy_paths, copied_image_ids))
            {
                err_msg = QStringLiteral("批量复制聚类图像到数据集失败");
                return false;
            }
            if (copied_image_ids.size() != copy_source_image_ids.size())
            {
                err_msg = QStringLiteral("批量复制聚类图像后返回数量不一致");
                return false;
            }

            datasets_->addImages(copy_target_dataset_ids, copied_image_ids);
            result.copied_image_count = copied_image_ids.size();

            std::map<int64_t, std::vector<int64_t>> copied_images_by_tag;
            for (size_t i = 0; i < copied_image_ids.size() && i < source_tag_ids.size(); ++i)
            {
                for (const int64_t tag_id : source_tag_ids[i])
                {
                    copied_images_by_tag[tag_id].push_back(copied_image_ids[i]);
                }
            }
            for (const auto &[tag_id, image_ids] : copied_images_by_tag)
            {
                if (!image_tags_->setImagesTag(image_ids, tag_id))
                {
                    updateDatasetsStats();
                    if (image_info_ != nullptr)
                        image_info_->updateLabelInfo();
                    if (global_filter_ != nullptr)
                        global_filter_->refresh();
                    err_msg = QStringLiteral("复制聚类图像标签失败: tag_id=%1").arg(tag_id);
                    return false;
                }
            }

            std::vector<int64_t>     copied_label_image_ids;
            std::vector<int64_t>     copied_label_class_ids;
            std::vector<QVariantMap> copied_label_data;
            for (size_t i = 0; i < copied_image_ids.size() && i < source_label_ids.size(); ++i)
            {
                for (const int64_t label_id : source_label_ids[i])
                {
                    const LabelInstance *label_instance = label_instances_->getLabelInstance(label_id);
                    if (label_instance == nullptr || label_instance->data() == nullptr)
                    {
                        continue;
                    }
                    copied_label_image_ids.push_back(copied_image_ids[i]);
                    copied_label_class_ids.push_back(label_instance->labelClassId());
                    copied_label_data.push_back(label_instance->data()->dataMap());
                }
            }

            if (!copied_label_image_ids.empty())
            {
                QString label_err_msg;
                if (!addLabelsInternal(copied_label_image_ids, copied_label_class_ids, copied_label_data,
                                       &label_err_msg))
                {
                    updateDatasetsStats();
                    if (image_info_ != nullptr)
                        image_info_->updateLabelInfo();
                    if (global_filter_ != nullptr)
                        global_filter_->refresh();
                    err_msg = QStringLiteral("复制聚类图像标注失败");
                    if (!label_err_msg.isEmpty())
                        err_msg += QStringLiteral(": %1").arg(label_err_msg);
                    return false;
                }
            }
            else
            {
                updateDatasetsStats();
                if (image_info_ != nullptr)
                    image_info_->updateLabelInfo();
            }
        }
    }
    else
    {
        std::vector<int64_t> moved_image_ids;
        std::vector<int64_t> source_dataset_ids;
        std::vector<int64_t> target_dataset_ids;
        moved_image_ids.reserve(pending_images.size());
        source_dataset_ids.reserve(pending_images.size());
        target_dataset_ids.reserve(pending_images.size());

        for (size_t i = 0; i < pending_images.size(); ++i)
        {
            const PendingClusterImage &pending_image = pending_images[i];
            const int64_t              target_dataset_id = planned_target_dataset_ids[i];
            if (target_dataset_id == pending_image.source_dataset_id)
                continue;

            moved_image_ids.push_back(pending_image.image_id);
            source_dataset_ids.push_back(pending_image.source_dataset_id);
            target_dataset_ids.push_back(target_dataset_id);
        }

        if (!moved_image_ids.empty())
        {
            const auto moved_images_label_ids = image_instances_->getLabelIds(moved_image_ids);
            if (!image_instances_->updateImagesDataset(moved_image_ids, target_dataset_ids))
            {
                err_msg = QStringLiteral("批量移动聚类图像到数据集失败");
                return false;
            }

            datasets_->moveImages(source_dataset_ids, target_dataset_ids, moved_image_ids, moved_images_label_ids);
            result.moved_image_count = moved_image_ids.size();
        }
    }

    if (image_info_ != nullptr)
        image_info_->onCurrentImageChanged();

    if (global_filter_ != nullptr)
        global_filter_->refresh();

    return true;
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

    if (import_running_)
    {
        const QString message = QStringLiteral("已有导入任务正在运行");
        spdlog::warn("导入数据失败, 已有导入任务正在运行");
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "startTask", Qt::QueuedConnection,
                                  Q_ARG(QString, "导入数据"));
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection,
                                  Q_ARG(int, spdlog::level::warn), Q_ARG(QString, message));
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "completeTask", Qt::QueuedConnection);
        emit dataImportFinished(false, message);
        return;
    }

    // 验证数据格式是否支持
    if (!data::DataFormat::isDataFormatSupported(data_format))
    {
        const QString message = QString("数据格式不支持");
        spdlog::error("导入数据失败, 数据格式不支持: {}", data_format);
        emit dataImportFinished(false, message);
        return;
    }

    // 显示进度对话框
    // ui::ProgressManager::getInstance()->startTask("导入数据");
    // 下面这样会在 UI 线程 (ProgressManager 所在线程) 中调用, 异步调用
    QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "startTask", Qt::QueuedConnection,
                              Q_ARG(QString, "导入数据"));

    QString db_check_err_msg;
    if (database_ == nullptr || !database_->checkIntegrity(db_check_err_msg))
    {
        const QString message = QString("项目数据库检查失败，无法导入数据: %1").arg(db_check_err_msg);
        spdlog::error("{}", message.toUtf8().constData());
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection,
                                  Q_ARG(int, spdlog::level::err), Q_ARG(QString, message));
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "completeTask", Qt::QueuedConnection);
        emit dataImportFinished(false, message);
        return;
    }

    // 使用工厂函数创建导入器
    // 重构后：DataManager 不再直接实例化具体的导入器类
    // 而是通过工厂函数获取，实现了依赖倒置原则
    DataImporter *importer = DataImporter::createImporter(data_format, database_, this);
    if (!importer)
    {
        const QString message = QStringLiteral("不支持的数据格式");
        spdlog::error("无法为格式 {} 创建导入器", data_format);
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection,
                                  Q_ARG(int, spdlog::level::err), Q_ARG(QString, message));
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "completeTask", Qt::QueuedConnection);
        emit dataImportFinished(false, message);
        return;
    }
    importer->setTargetMethod(method_);
    import_running_                  = true;
    pending_import_task_             = std::make_unique<PendingImportTask>();
    pending_import_task_->importer   = importer;
    pending_import_task_->dataset_id = dataset_id;

    qRegisterMetaType<std::vector<QString>>("std::vector<QString>");
    qRegisterMetaType<std::vector<int64_t>>("std::vector<int64_t>");
    qRegisterMetaType<std::map<QString, QString>>("std::map<QString, QString>");
    qRegisterMetaType<std::vector<ImportedLabel>>("std::vector<ImportedLabel>");

    // 导入器每解析出一批数据就交给 DataManager 写库。
    // BlockingQueuedConnection 可以限制后台线程速度，避免批次在主线程事件队列中大量堆积。
    connect(importer, &DataImporter::dataBatchReady, this, &DataManager::handleDataBatchReady,
            Qt::BlockingQueuedConnection);
    connect(importer, &DataImporter::importFinished, this, &DataManager::handleImportFinished, Qt::QueuedConnection);

    // 启动导入
    importer->startImport(dataset_id, image_dir, data_dir);
}

void DataManager::exportDataset(const int64_t dataset_id, const int data_format, const QString &output_dir)
{
    qInfo() << __FUNCTION__ << __LINE__ << "dataset_id" << dataset_id << "data_format" << data_format << "output_dir"
            << output_dir;

    if (!data::DataFormat::isDataFormatSupported(data_format))
    {
        spdlog::error("导出数据失败, 数据格式不支持: {}", data_format);
        return;
    }

    if (output_dir.isEmpty())
    {
        spdlog::error("导出数据失败, 输出目录为空");
        return;
    }

    ExportDataset dataset = buildExportDataset(dataset_id);
    if (dataset.images.empty())
    {
        spdlog::warn("导出数据失败, 数据集为空: {}", dataset_id);
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "startTask", Qt::QueuedConnection,
                                  Q_ARG(QString, "导出数据"));
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection,
                                  Q_ARG(int, spdlog::level::err), Q_ARG(QString, "数据集没有可导出的图像"));
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "completeTask", Qt::QueuedConnection);
        return;
    }

    QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "startTask", Qt::QueuedConnection,
                              Q_ARG(QString, "导出数据"));

    DataExporter *exporter = DataExporter::createExporter(data_format, this);
    if (!exporter)
    {
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection,
                                  Q_ARG(int, spdlog::level::err), Q_ARG(QString, "不支持的数据格式"));
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "completeTask", Qt::QueuedConnection);
        return;
    }

    connect(
        exporter, &DataExporter::exportFinished, this,
        [exporter](bool success, const QString &message)
        {
            const int level = success ? spdlog::level::info : spdlog::level::err;
            QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection,
                                      Q_ARG(int, level), Q_ARG(QString, message));
            QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "completeTask", Qt::QueuedConnection);
            exporter->deleteLater();
        },
        Qt::QueuedConnection);

    exporter->startExport(dataset, output_dir);
}

ExportDataset DataManager::buildExportDataset(const int64_t dataset_id) const
{
    ExportDataset dataset;
    dataset.dataset_id   = dataset_id;
    dataset.dataset_name = datasets_->getDatasetName(dataset_id);

    std::set<int64_t> image_id_set;
    for (const int64_t image_id : image_instances_->getAllImageIds())
    {
        ImageInstance *image = image_instances_->getImageInstance(image_id);
        if (image == nullptr || image->datasetId() != dataset_id)
        {
            continue;
        }

        QSize image_size = image->imageSize();
        if (!image_size.isValid())
        {
            int width  = 0;
            int height = 0;
            if (DatasetIO::getImageDimensions(image->path(), width, height))
            {
                image_size = QSize(width, height);
            }
        }

        ExportImage export_image;
        export_image.dataset_id = dataset_id;
        export_image.image_id   = image->imageId();
        export_image.path       = image->path();
        export_image.width      = image_size.width();
        export_image.height     = image_size.height();
        dataset.images.push_back(export_image);
        image_id_set.insert(image->imageId());
    }

    std::set<int64_t> label_class_ids;
    for (const auto &[label_id, label_instance] : label_instances_->getAllLabelInstances())
    {
        if (label_instance == nullptr || image_id_set.find(label_instance->imageId()) == image_id_set.end())
        {
            continue;
        }

        ExportLabel export_label;
        export_label.label_id       = label_id;
        export_label.image_id       = label_instance->imageId();
        export_label.label_class_id = label_instance->labelClassId();
        export_label.data           = label_instance->data() ? label_instance->data()->dataMap() : QVariantMap();
        dataset.labels.push_back(export_label);
        label_class_ids.insert(export_label.label_class_id);
    }

    for (const int64_t label_class_id : label_class_ids)
    {
        ExportLabelClass export_class;
        export_class.id    = label_class_id;
        export_class.name  = label_classes_->getLabelClassName(label_class_id);
        export_class.color = label_classes_->getLabelClassColor(label_class_id);
        dataset.label_classes.push_back(export_class);
    }

    return dataset;
}

void DataManager::deleteSelectedImages()
{
    std::vector<int64_t>              image_ids        = image_instances_->getSelectedImagesId();
    std::vector<int64_t>              dataset_ids      = image_instances_->getDatasetIds(image_ids);
    std::vector<std::vector<int64_t>> images_label_ids = image_instances_->getLabelIds(image_ids);
    std::vector<int64_t>              label_ids;
    for (const std::vector<int64_t> &label_ids_vec : images_label_ids)
    {
        label_ids.insert(label_ids.end(), label_ids_vec.begin(), label_ids_vec.end());
    }
    datasets_->deleteImages(dataset_ids, image_ids);
    image_tags_->removeImagesTags(image_ids);
    image_instances_->deleteImages(image_ids);
    label_instances_->deleteLabels(label_ids);
    updateDatasetsStats();
}

void DataManager::copySelectedImagesToDataset(const int64_t dataset_id)
{
    if (datasets_ == nullptr || image_instances_ == nullptr || label_instances_ == nullptr || image_tags_ == nullptr)
    {
        return;
    }
    if (dataset_id < 0 || datasets_->getDatasetName(dataset_id).isEmpty())
    {
        spdlog::warn("复制图像失败, 目标数据集无效: {}", dataset_id);
        return;
    }

    const std::vector<int64_t> source_image_ids = image_instances_->getSelectedImagesId();
    if (source_image_ids.empty())
    {
        return;
    }

    std::vector<QString> image_paths;
    image_paths.reserve(source_image_ids.size());
    for (const int64_t image_id : source_image_ids)
    {
        const QString path = image_instances_->getImagePath(image_id);
        if (!path.isEmpty())
        {
            image_paths.push_back(path);
        }
    }
    if (image_paths.size() != source_image_ids.size())
    {
        spdlog::warn("复制图像失败, 选中图像中存在无效路径");
        return;
    }

    std::vector<std::vector<int64_t>> source_label_ids = image_instances_->getLabelIds(source_image_ids);
    std::vector<std::vector<int64_t>> source_tag_ids   = image_tags_->getImagesTagIds(source_image_ids);

    std::vector<int64_t> copied_image_ids;
    if (!image_instances_->addImages(dataset_id, image_paths, copied_image_ids))
    {
        return;
    }
    if (copied_image_ids.size() != source_image_ids.size())
    {
        spdlog::error("复制图像失败, 新图像 ID 数量不一致");
        return;
    }

    datasets_->addImages(std::vector<int64_t>(copied_image_ids.size(), dataset_id), copied_image_ids);

    std::map<int64_t, std::vector<int64_t>> copied_images_by_tag;
    for (size_t i = 0; i < copied_image_ids.size() && i < source_tag_ids.size(); ++i)
    {
        for (const int64_t tag_id : source_tag_ids[i])
        {
            copied_images_by_tag[tag_id].push_back(copied_image_ids[i]);
        }
    }
    for (const auto &[tag_id, image_ids] : copied_images_by_tag)
    {
        image_tags_->setImagesTag(image_ids, tag_id);
    }

    std::vector<int64_t>     copied_label_image_ids;
    std::vector<int64_t>     copied_label_class_ids;
    std::vector<QVariantMap> copied_label_data;
    for (size_t i = 0; i < copied_image_ids.size() && i < source_label_ids.size(); ++i)
    {
        for (const int64_t label_id : source_label_ids[i])
        {
            const LabelInstance *label_instance = label_instances_->getLabelInstance(label_id);
            if (label_instance == nullptr || label_instance->data() == nullptr)
            {
                continue;
            }
            copied_label_image_ids.push_back(copied_image_ids[i]);
            copied_label_class_ids.push_back(label_instance->labelClassId());
            copied_label_data.push_back(label_instance->data()->dataMap());
        }
    }

    if (!copied_label_image_ids.empty())
    {
        if (!addLabelsInternal(copied_label_image_ids, copied_label_class_ids, copied_label_data))
        {
            updateDatasetsStats();
            image_info_->updateLabelInfo();
        }
    }
    else
    {
        updateDatasetsStats();
        image_info_->updateLabelInfo();
    }

    if (global_filter_ != nullptr)
    {
        global_filter_->refresh();
    }
}

void DataManager::moveSelectedImagesToDataset(const int64_t dataset_id)
{
    if (datasets_ == nullptr || image_instances_ == nullptr)
    {
        return;
    }
    if (dataset_id < 0 || datasets_->getDatasetName(dataset_id).isEmpty())
    {
        spdlog::warn("移动图像失败, 目标数据集无效: {}", dataset_id);
        return;
    }

    const std::vector<int64_t> selected_image_ids = image_instances_->getSelectedImagesId();
    if (selected_image_ids.empty())
    {
        return;
    }

    std::vector<int64_t> source_dataset_ids;
    std::vector<int64_t> moved_image_ids;
    source_dataset_ids.reserve(selected_image_ids.size());
    moved_image_ids.reserve(selected_image_ids.size());
    for (const int64_t image_id : selected_image_ids)
    {
        const int64_t source_dataset_id = image_instances_->getImageDatasetId(image_id);
        if (source_dataset_id < 0 || source_dataset_id == dataset_id)
        {
            continue;
        }
        source_dataset_ids.push_back(source_dataset_id);
        moved_image_ids.push_back(image_id);
    }
    if (moved_image_ids.empty())
    {
        return;
    }

    if (!image_instances_->updateImagesDataset(moved_image_ids, dataset_id))
    {
        return;
    }

    datasets_->deleteImages(source_dataset_ids, moved_image_ids);
    datasets_->addImages(std::vector<int64_t>(moved_image_ids.size(), dataset_id), moved_image_ids);
    updateDatasetsStats();
    image_info_->onCurrentImageChanged();

    if (global_filter_ != nullptr)
    {
        global_filter_->refresh();
    }
}

void DataManager::addLabelClass(const QString &name, const QString &color, const QString &shortcut)
{
    label_classes_->addLabelClass(name, color, shortcut);
}

void DataManager::updateLabelClass(const int64_t label_class_id, const QString &name, const QString &color,
                                   const QString &shortcut, const int64_t ordinal_index)
{
    // 获取当前的 ordinal_index
    int64_t current_ordinal = -1;
    for (int i = 0; i < label_classes_->rowCount(); ++i)
    {
        QModelIndex idx = label_classes_->index(i, 0);
        if (label_classes_->data(idx, LabelClassesListModel::LabelClassIdRole).toLongLong() == label_class_id)
        {
            current_ordinal = label_classes_->data(idx, LabelClassesListModel::OrdinalIndexRole).toLongLong();
            break;
        }
    }

    // 如果 ordinal_index 改变了，先进行重排序
    if (current_ordinal != -1 && current_ordinal != ordinal_index)
    {
        label_classes_->reorderLabelClass(label_class_id, ordinal_index);
    }

    // 更新其他属性（名称、颜色、快捷键），使用新的 ordinal_index
    label_classes_->updateLabelClass(label_class_id, name, color, shortcut, ordinal_index);
    label_instances_->labelClassUpdated(label_class_id);
    image_labels_list_->labelClassUpdated(label_class_id);
    image_labels_table_->labelClassUpdated(label_class_id);
}

void DataManager::deleteLabelClass(const int64_t label_class_id)
{
    std::vector<int64_t> label_ids = label_instances_->getLabelIds(label_class_id);
    deleteLabels(label_ids);
    label_classes_->deleteLabelClass(label_class_id);
}

void DataManager::addLabels(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_class_ids,
                            const std::vector<QVariantMap> &data)
{
    addLabelsInternal(image_ids, label_class_ids, data);
}

bool DataManager::addLabel(const int64_t image_id, const int64_t label_class_id, const QVariantMap &data)
{
    if (image_id < 0)
    {
        spdlog::warn("添加标注失败: 当前图像无效, image_id={}", image_id);
        return false;
    }
    if (label_class_id < 0)
    {
        spdlog::warn("添加标注失败: 当前标签类别无效, label_class_id={}", label_class_id);
        return false;
    }

    QString    err_msg;
    const bool ok = addLabelsInternal({image_id}, {label_class_id}, {data}, &err_msg);
    if (!ok)
    {
        spdlog::error("添加标注失败: image_id={}, label_class_id={}, error={}", image_id, label_class_id,
                      err_msg.toUtf8().constData());
    }
    return ok;
}

bool DataManager::addLabelsInternal(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_class_ids,
                                    const std::vector<QVariantMap> &data, QString *err_msg)
{
    std::vector<int64_t> label_ids;
    if (!label_instances_->tryAddLabels(label_ids, image_ids, label_class_ids, data, err_msg))
    {
        return false;
    }
    if (labels_loading_)
    {
        labels_changed_during_loading_ = true;
    }
    image_instances_->addImagesLabelIds(image_ids, label_ids);
    image_labels_list_->addLabels(image_ids, label_ids);
    image_labels_table_->addLabels(image_ids, label_ids);
    updateDatasetsStats();
    image_info_->updateLabelInfo();
    return true;
}

void DataManager::updateLabels(const std::vector<int64_t> &label_ids, const std::vector<QVariantMap> &data)
{
    if (labels_loading_)
    {
        labels_changed_during_loading_ = true;
    }
    std::vector<int64_t> image_ids = label_instances_->getImageIds(label_ids);
    label_instances_->updateLabelsData(label_ids, image_ids, data);
    image_labels_list_->updateLabels(image_ids, label_ids);
    image_labels_table_->updateLabels(image_ids, label_ids);
    // updateDatasetsStats();
}

void DataManager::updateLabelsClass(const std::vector<int64_t> &label_ids, const std::vector<int64_t> &label_class_ids)
{
    if (labels_loading_)
    {
        labels_changed_during_loading_ = true;
    }
    std::vector<int64_t> image_ids = label_instances_->getImageIds(label_ids);
    label_instances_->updateLabelsClass(label_ids, label_class_ids);
    image_labels_list_->updateLabels(image_ids, label_ids);
    image_labels_table_->updateLabels(image_ids, label_ids);
}

void DataManager::deleteLabels(const std::vector<int64_t> &label_ids)
{
    if (labels_loading_)
    {
        labels_changed_during_loading_ = true;
    }
    std::vector<int64_t> image_ids = label_instances_->getImageIds(label_ids);
    label_instances_->deleteLabels(label_ids);
    image_instances_->deleteImagesLabelIds(image_ids, label_ids);
    image_labels_list_->deleteLabels(image_ids, label_ids);
    image_labels_table_->deleteLabels(image_ids, label_ids);
    updateDatasetsStats();
    image_info_->updateLabelInfo();
    if (global_filter_ != nullptr)
    {
        global_filter_->refresh();
    }
}

void DataManager::duplicateSelectedLabels()
{
    if (image_labels_list_ == nullptr || label_instances_ == nullptr || image_instances_ == nullptr)
    {
        return;
    }

    const std::vector<int64_t> selected_label_ids = image_labels_list_->getSelectedLabelIds();
    if (selected_label_ids.empty())
    {
        return;
    }

    const int64_t            current_image_id = image_instances_->getCurrentImageId();
    std::vector<int64_t>     image_ids;
    std::vector<int64_t>     label_class_ids;
    std::vector<QVariantMap> labels_data;
    image_ids.reserve(selected_label_ids.size());
    label_class_ids.reserve(selected_label_ids.size());
    labels_data.reserve(selected_label_ids.size());

    for (const int64_t label_id : selected_label_ids)
    {
        LabelInstance *instance = label_instances_->getLabelInstance(label_id);
        if (instance == nullptr || instance->imageId() != current_image_id || instance->data() == nullptr)
        {
            continue;
        }

        QVariantMap data = instance->data()->dataMap();
        image_ids.push_back(current_image_id);
        label_class_ids.push_back(instance->labelClassId());
        labels_data.push_back(data);
    }

    if (!image_ids.empty())
    {
        addLabelsInternal(image_ids, label_class_ids, labels_data);
    }
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

void DataManager::handleDataBatchReady(int64_t dataset_id, std::vector<QString> image_paths,
                                       std::vector<int64_t> image_widths, std::vector<int64_t> image_heights,
                                       std::map<QString, QString> label_class_info, std::vector<ImportedLabel> labels,
                                       int64_t processed_images, int64_t total_images)
{
    Q_UNUSED(image_widths)
    Q_UNUSED(image_heights)

    DataImporter *importer = qobject_cast<DataImporter *>(sender());
    if (!pending_import_task_ || pending_import_task_->importer != importer)
    {
        return;
    }

    PendingImportTask &task = *pending_import_task_;
    task.processed_images
        = std::max(task.processed_images, static_cast<size_t>(std::max<int64_t>(0, processed_images)));
    task.total_images = std::max(task.total_images, static_cast<size_t>(std::max<int64_t>(0, total_images)));

    QString err_msg;
    if (!writeImportBatch(dataset_id, image_paths, label_class_info, labels, err_msg))
    {
        task.failed_batches++;
        task.failed_images += image_paths.size();
        task.failed_labels += labels.size();
        task.skipped_labels += static_cast<int>(labels.size());
        if (task.first_error_message.isEmpty())
        {
            task.first_error_message = err_msg;
        }

        if (isFatalDatabaseError(err_msg))
        {
            task.fatal_error         = true;
            task.first_error_message = QString("项目数据库已损坏，无法继续导入标注: %1").arg(err_msg);
            if (importer != nullptr)
            {
                importer->requestCancel();
            }
        }

        const QString progress_message = task.fatal_error
                                           ? task.first_error_message
                                           : QString("批次写入失败，已跳过当前批次并继续导入后续数据: %1").arg(err_msg);
        spdlog::error("{}", progress_message.toUtf8().constData());
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection,
                                  Q_ARG(int, spdlog::level::err), Q_ARG(QString, progress_message));
        return;
    }

    QMetaObject::invokeMethod(
        ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection, Q_ARG(int, spdlog::level::info),
        Q_ARG(QString, QString("已写入 %1 个图像, %2 个标注").arg(task.imported_images).arg(task.imported_labels)));
}

bool DataManager::writeImportBatch(int64_t dataset_id, const std::vector<QString> &image_paths,
                                   const std::map<QString, QString> &label_class_info,
                                   const std::vector<ImportedLabel> &labels, QString &err_msg)
{
    if (!pending_import_task_)
    {
        err_msg = QString("导入任务不存在");
        return false;
    }

    PendingImportTask &task = *pending_import_task_;
    if (dataset_id != task.dataset_id)
    {
        err_msg = QString("导入批次的数据集 ID 不一致");
        return false;
    }

    for (const auto &[label_name, color] : label_class_info)
    {
        int64_t label_class_id = label_classes_->getLabelClassId(label_name);
        if (label_class_id < 0)
        {
            addLabelClass(label_name, color, QString());
            label_class_id = label_classes_->getLabelClassId(label_name);
            spdlog::debug("创建新标签类别: {}, ID: {}", label_name.toUtf8().constData(), label_class_id);
        }

        if (label_class_id >= 0)
        {
            task.label_class_map[label_name] = label_class_id;
        }
    }

    std::vector<int64_t> image_ids;
    if (!image_paths.empty())
    {
        if (task.normalized_image_path_to_id.empty())
        {
            for (const int64_t image_id : image_instances_->getAllImageIds())
            {
                if (image_instances_->getImageDatasetId(image_id) != task.dataset_id)
                    continue;

                const QString normalized_path = normalizedImagePath(image_instances_->getImagePath(image_id));
                if (!normalized_path.isEmpty())
                    task.normalized_image_path_to_id[normalized_path] = image_id;
            }
        }

        std::vector<QString>                    new_image_paths;
        std::vector<QString>                    new_normalized_paths;
        std::map<QString, std::vector<QString>> pending_raw_paths_by_normalized_path;

        for (const QString &image_path : image_paths)
        {
            const QString normalized_path = normalizedImagePath(image_path);
            const auto    existing_it     = task.normalized_image_path_to_id.find(normalized_path);
            if (existing_it != task.normalized_image_path_to_id.end())
            {
                task.image_path_to_id[image_path] = existing_it->second;
                continue;
            }

            auto pending_it = pending_raw_paths_by_normalized_path.find(normalized_path);
            if (pending_it == pending_raw_paths_by_normalized_path.end())
            {
                new_image_paths.push_back(image_path);
                new_normalized_paths.push_back(normalized_path);
                pending_it
                    = pending_raw_paths_by_normalized_path.emplace(normalized_path, std::vector<QString>{}).first;
            }
            pending_it->second.push_back(image_path);
        }

        if (!new_image_paths.empty() && !image_instances_->addImages(task.dataset_id, new_image_paths, image_ids))
        {
            err_msg = QString("添加图像失败，当前批次已跳过。已导入 %1 个图像, %2 个标注")
                          .arg(task.imported_images)
                          .arg(task.imported_labels);
            return false;
        }

        if (image_ids.size() != new_image_paths.size())
        {
            err_msg = QString("添加图像失败，返回的图像 ID 数量不一致");
            return false;
        }

        task.imported_images += image_ids.size();

        std::vector<int64_t> dataset_ids(image_ids.size(), task.dataset_id);
        datasets_->addImages(dataset_ids, image_ids);

        for (size_t i = 0; i < new_image_paths.size(); ++i)
        {
            task.normalized_image_path_to_id[new_normalized_paths[i]] = image_ids[i];
            const auto pending_it = pending_raw_paths_by_normalized_path.find(new_normalized_paths[i]);
            if (pending_it == pending_raw_paths_by_normalized_path.end())
                continue;

            for (const QString &raw_path : pending_it->second) task.image_path_to_id[raw_path] = image_ids[i];
        }
    }

    std::vector<int64_t>     batch_label_image_ids;
    std::vector<int64_t>     batch_label_class_ids;
    std::vector<QVariantMap> batch_label_data;

    for (const ImportedLabel &label : labels)
    {
        auto image_it = task.image_path_to_id.find(label.image_path);
        if (image_it == task.image_path_to_id.end())
        {
            spdlog::warn("未找到图像路径对应的 ID，跳过标注: {}, 标签类别: {}", label.image_path.toUtf8().constData(),
                         label.label_class_name.toUtf8().constData());
            task.skipped_labels++;
            continue;
        }

        auto class_it = task.label_class_map.find(label.label_class_name);
        if (class_it == task.label_class_map.end())
        {
            const QString fallback_color
                = DatasetIO::generateDefaultColor(static_cast<int>(task.label_class_map.size()));
            addLabelClass(label.label_class_name, fallback_color, QString());
            const int64_t label_class_id = label_classes_->getLabelClassId(label.label_class_name);
            if (label_class_id >= 0)
            {
                task.label_class_map[label.label_class_name] = label_class_id;
                class_it                                     = task.label_class_map.find(label.label_class_name);
            }
        }

        if (class_it == task.label_class_map.end())
        {
            spdlog::warn("未找到标签类别，跳过标注: {}, 图像: {}", label.label_class_name.toUtf8().constData(),
                         label.image_path.toUtf8().constData());
            task.skipped_labels++;
            continue;
        }

        if (label.data.isEmpty())
        {
            spdlog::warn("跳过空的标注数据: label_class={}, 图像: {}", label.label_class_name.toUtf8().constData(),
                         label.image_path.toUtf8().constData());
            task.skipped_labels++;
            continue;
        }

        batch_label_image_ids.push_back(image_it->second);
        batch_label_class_ids.push_back(class_it->second);
        batch_label_data.push_back(label.data);
    }

    if (!batch_label_image_ids.empty())
    {
        QString label_err_msg;
        if (!addLabelsInternal(batch_label_image_ids, batch_label_class_ids, batch_label_data, &label_err_msg))
        {
            err_msg = QString("添加标注失败，当前标注批次已跳过。待写入标注 %1 个").arg(batch_label_image_ids.size());
            if (!label_err_msg.isEmpty())
            {
                err_msg += QString("，原因: %1").arg(label_err_msg);
            }
            return false;
        }
        task.imported_labels += batch_label_image_ids.size();
    }
    else
    {
        updateDatasetsStats();
        image_info_->updateLabelInfo();
    }

    return true;
}

void DataManager::handleImportFinished(bool success, std::vector<int64_t> image_ids,
                                       std::vector<int64_t> label_class_ids)
{
    Q_UNUSED(image_ids)
    Q_UNUSED(label_class_ids)

    if (!pending_import_task_)
    {
        import_running_ = false;
        return;
    }

    PendingImportTask &task = *pending_import_task_;
    if (!success)
    {
        QString message = task.first_error_message;
        if (message.isEmpty())
        {
            message = QString("数据解析失败或没有数据");
        }
        finishBatchedImport(false, message);
        return;
    }

    QString message = QString("导入完成: %1 个图像, %2 个标注, 跳过标注 %3 个")
                          .arg(task.imported_images)
                          .arg(task.imported_labels)
                          .arg(task.skipped_labels);
    if (task.failed_batches > 0)
    {
        message += QString("，写入失败批次 %1 个, 失败图像 %2 个, 失败标注 %3 个")
                       .arg(task.failed_batches)
                       .arg(task.failed_images)
                       .arg(task.failed_labels);
    }
    spdlog::info("数据导入完成: 图像={}, 标注={}, 跳过标注={}, 写入失败批次={}, 失败图像={}, 失败标注={}",
                 task.imported_images, task.imported_labels, task.skipped_labels, task.failed_batches,
                 task.failed_images, task.failed_labels);
    finishBatchedImport(true, message);
}

void DataManager::finishBatchedImport(bool success, const QString &message)
{
    DataImporter *importer = pending_import_task_ ? pending_import_task_->importer : nullptr;

    const int level = success ? spdlog::level::info : spdlog::level::err;
    if (success)
    {
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "updateProgress", Qt::QueuedConnection,
                                  Q_ARG(int, 100));
    }
    QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection, Q_ARG(int, level),
                              Q_ARG(QString, message));
    QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "completeTask", Qt::QueuedConnection);

    if (importer)
    {
        importer->deleteLater();
    }
    pending_import_task_.reset();
    import_running_ = false;
    emit dataImportFinished(success, message);
}

void DataManager::initializeQmlEngine(QQmlApplicationEngine *engine)
{
    if (!engine)
    {
        // 尝试从 QObject 上下文获取引擎
        QQmlEngine *qml_engine
            = QQmlEngine::contextForObject(this) ? QQmlEngine::contextForObject(this)->engine() : nullptr;
        if (!qml_engine)
        {
            return;
        }
        engine = qobject_cast<QQmlApplicationEngine *>(qml_engine);
        if (!engine)
        {
            return;
        }
    }

    // 创建 LabelInstanceImageProvider 实例，传入三个模型指针
    auto *label_instance_provider = new LabelInstanceImageProvider(label_instances_, image_instances_, label_classes_);

    // 注册到 QML 引擎（使用小写名称，因为 QML Image 会自动转换为小写）
    engine->addImageProvider("labelinstance", label_instance_provider);
}

QString DataManager::getImageName(const int64_t image_id) const
{
    return image_instances_->getImageName(image_id);
}

QString DataManager::getImagePath(const int64_t image_id) const
{
    return image_instances_->getImagePath(image_id);
}

QString DataManager::getImageDatasetName(const int64_t image_id) const
{
    const int64_t dataset_id = image_instances_->getImageDatasetId(image_id);
    return datasets_->getDatasetName(dataset_id);
}

QString DataManager::getImageTagName(const int64_t image_id) const
{
    const std::set<int64_t> tag_ids = image_instances_->getImageTagIds(image_id);
    if (tag_ids.empty())
        return QString();
    QString tag_names;
    for (int64_t tag_id : tag_ids)
    {
        tag_names.append(image_tags_->getTagClassName(tag_id) + ";");
    }
    return tag_names;
}

} // namespace dltool::data
