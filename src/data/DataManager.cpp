#include "data/DataManager.h"

#include "common/Utils.h"
#include "data/CategoryStatisticsModel.h"
#include "data/DataFormat.h"
#include "data/DataIO.h"
#include "data/DataNameUtils.h"
#include "data/DatasetIO.h"
#include "data/GlobalFilter.h"
#include "data/ImageInstanceImageProvider.h"
#include "data/LabelData.h"
#include "data/LabelInstanceImageProvider.h"
#include "database/DataBase.h"
#include "ui/ProgressManager.h"
#include "ui/SignalHelper.h"

#include <spdlog/spdlog.h>

#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMetaType>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QThread>
#include <algorithm>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <utility>

using dltool::common::ensureDirectory;

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

    normalized = dltool::common::cleanPath(normalized);
#ifdef Q_OS_WIN
    normalized = normalized.toCaseFolded();
#endif
    return normalized;
}

void addProgressMessage(int level, const QString &message)
{
    QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection, Q_ARG(int, level),
                              Q_ARG(QString, message));
}

std::map<QString, QString> parseLabelClassGroupMap(const QVariantMap &groups)
{
    std::map<QString, QString> result;
    for (auto it = groups.cbegin(); it != groups.cend(); ++it)
    {
        const QString name = sanitizeName(it.key());
        if (name.isEmpty())
            continue;
        result[name] = normalizeLabelClassGroup(it.value().toString());
    }
    return result;
}

} // namespace

struct DataManager::PendingImportTask
{
    DataIO *importer{nullptr};
    QElapsedTimer elapsed_timer;

    int64_t dataset_id{0};
    int     data_format{-1};

    std::map<QString, int64_t> label_class_map;
    std::map<QString, int64_t> image_path_to_id;
    std::map<QString, int64_t> normalized_image_path_to_id;
    std::map<QString, QString> label_class_group_map;
    std::set<int64_t>          imported_image_id_set;
    std::vector<int64_t>       imported_image_ids;
    std::vector<int64_t>       deferred_dataset_image_ids;
    std::map<int64_t, int64_t> first_polygon_class_by_image_id;
    std::map<int64_t, int64_t> first_anomaly_polygon_class_by_image_id;
    std::map<int64_t, int64_t> folder_class_by_image_id;

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
    bool    deferred_ui_refresh{false};
    bool    deferred_image_model_refresh{false};
    bool    deferred_label_model_refresh{false};
};

DataManager::DataManager(const int method, dltool::database::ProjectDataBase *database, const QString &project_dir,
                         QObject *parent)
    : QObject(parent)
    , database_(database)
    , project_dir_(dltool::common::cleanPath(project_dir))
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
    label_instances_    = new LabelInstancesListModel(database_, image_instances_, label_classes_,
                                                      data::createLabelDataHelper(method), false, this);
    image_labels_list_  = new ImageLabelsListModel(image_instances_, label_instances_, label_classes_, this);
    image_labels_table_ = new ImageLabelsTableModel(image_instances_, label_instances_, label_classes_, this);
    image_tags_         = new ImageTagsListModel(database_, image_instances_, label_instances_, image_labels_list_, this);
    image_info_         = new ImageInfoListModel(datasets_, image_instances_, label_classes_, label_instances_, this);

    // Create GlobalFilter and initialize it with the models
    global_filter_ = new GlobalFilter(this, this);
    global_filter_->initializeFilterModules(this);

    // Create filter items models
    dataset_filter_items_     = new DatasetFilterItemsModel(this);
    tag_filter_items_         = new TagFilterItemsModel(this);
    label_class_filter_items_ = new LabelClassFilterItemsModel(this);
    custom_filter_items_      = new CustomFilterItemsModel(this);

    // Create CategoryStatisticsModel
    category_statistics_model_ = new CategoryStatisticsModel(label_instances_, label_classes_, image_instances_, this);

    // Populate filter items models from datasets and tags
    dataset_filter_items_->populateFromDatasets(datasets_);
    tag_filter_items_->populateFromTags(image_tags_);
    label_class_filter_items_->populateFromLabelClasses(label_classes_);
    custom_filter_items_->populateFromCustomConditions();

    connect(global_filter_, &GlobalFilter::customFilterSearchResultsChanged, this,
            [this](bool has_image_search_results, bool has_label_search_results)
            {
                custom_filter_items_->setSearchResultsAvailable(has_image_search_results, has_label_search_results);
            });

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
    connect(image_labels_list_->selection(), &QItemSelectionModel::selectionChanged, image_tags_,
            &ImageTagsListModel::updateStats);

    std::vector<int64_t> image_ids   = image_instances_->getAllImageIds();
    std::vector<int64_t> dataset_ids = image_instances_->getImagesDatasetIds(image_ids);

    std::vector<std::vector<int64_t>> images_label_ids(image_ids.size());
    std::vector<int64_t>              image_label_class_ids;
    image_label_class_ids.reserve(image_ids.size());
    for (const int64_t image_id : image_ids)
    {
        image_label_class_ids.push_back(image_instances_->getImageLabelClassId(image_id));
    }
    std::vector<std::vector<int64_t>> images_tag_ids = image_tags_->getImagesTagIds(image_ids);

    datasets_->addImages(dataset_ids, image_ids);
    datasets_->setStats(dataset_ids, image_ids, images_label_ids, image_label_class_ids);

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
        if (dataset_deletion_running_)
        {
            // Do not start a second read while the deletion transaction is still in
            // progress.  It could otherwise observe a partial transaction snapshot.
            labels_reload_after_dataset_deletion_ = true;
            return;
        }
        startAsyncLabelLoading();
        return;
    }

    if (!success || labels == nullptr)
    {
        spdlog::error("后台加载项目标注失败: {}", err_msg.toUtf8().constData());
        return;
    }

    label_instances_->replaceAllLabels(std::move(*labels));
    if (image_tags_ != nullptr)
    {
        image_tags_->applyTagsToLabels();
    }
    rebuildLabelRelations();
    if (global_filter_ != nullptr)
    {
        global_filter_->refresh();
    }

    spdlog::info("后台加载项目标注完成: {} 个标注, 耗时 {} ms", label_instances_->totalCount(), elapsed_ms);
}

void DataManager::rebuildLabelRelations(const bool notify_image_model)
{
    if (image_instances_ == nullptr || label_instances_ == nullptr || datasets_ == nullptr)
    {
        return;
    }

    std::vector<int64_t>              image_ids        = image_instances_->getAllImageIds();
    std::vector<int64_t>              dataset_ids      = image_instances_->getImagesDatasetIds(image_ids);
    std::vector<std::vector<int64_t>> images_label_ids = label_instances_->getImagesLabelIds(image_ids);
    std::vector<int64_t>              image_label_class_ids;
    image_label_class_ids.reserve(image_ids.size());
    for (const int64_t image_id : image_ids)
    {
        image_label_class_ids.push_back(image_instances_->getImageLabelClassId(image_id));
    }

    image_instances_->setImagesLabelIds(image_ids, images_label_ids, notify_image_model);
    datasets_->setStats(dataset_ids, image_ids, images_label_ids, image_label_class_ids);

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

std::vector<int64_t> DataManager::getAllDatasetIds() const
{
    if (datasets_ == nullptr)
        return {};
    return datasets_->getAllDatasetIds();
}

std::vector<int64_t> DataManager::getAllLabelClassIds() const
{
    if (label_classes_ == nullptr)
        return {};
    return label_classes_->getAllLabelClassIds();
}

int DataManager::getDatasetId(const QString &dataset_name) const
{
    return datasets_->getDatasetId(dataset_name);
}

QString DataManager::projectDir() const
{
    return project_dir_;
}

QString DataManager::providerCacheKey() const
{
    return projectDir();
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

int64_t DataManager::imageLabelClassId(int64_t image_id) const
{
    return image_instances_ ? image_instances_->getImageLabelClassId(image_id) : -1;
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

QString DataManager::labelClassGroup(int64_t label_class_id) const
{
    return label_classes_ ? label_classes_->getLabelClassGroup(static_cast<int>(label_class_id)) : QString();
}

QString DataManager::datasetName(int64_t dataset_id) const
{
    return datasets_ ? datasets_->getDatasetName(static_cast<int>(dataset_id)) : QString();
}

std::vector<int64_t> DataManager::imageLabelIds(int64_t image_id) const
{
    return label_instances_ ? label_instances_->getImageLabelIds(image_id) : std::vector<int64_t>{};
}

void DataManager::importMaskData(int64_t dataset_id, const QString &image_manifest_path,
                                 const QString &prediction_output_dir)
{
    importData(dataset_id, DataFormat::Mask, image_manifest_path, prediction_output_dir);
}

QMetaObject::Connection DataManager::connectImportFinished(QObject *context, ImportFinishedHandler handler)
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

QString DataManager::getDatasetName(const int dataset_id) const
{
    return datasets_->getDatasetName(dataset_id);
}

void DataManager::addDataset(const QString &name)
{
    const QString validation_error = isValidDatasetName(name);
    if (!validation_error.isEmpty())
    {
        spdlog::warn("添加数据集失败: {}", validation_error.toUtf8().constData());
        return;
    }
    datasets_->addDataset(name);
}

void DataManager::updateDataset(const int64_t dataset_id, const QString &name)
{
    const QString validation_error = isValidDatasetName(name, dataset_id);
    if (!validation_error.isEmpty())
    {
        spdlog::warn("更新数据集失败: {}", validation_error.toUtf8().constData());
        return;
    }
    datasets_->updateDataset(dataset_id, name);
}

QString DataManager::isValidName(const QString &name) const
{
    return invalidNameError(name);
}

QString DataManager::isValidDatasetName(const QString &name, const int64_t dataset_id) const
{
    const QString name_error = isValidName(name);
    if (!name_error.isEmpty())
    {
        return name_error;
    }

    const int existing_dataset_id = datasets_ ? datasets_->getDatasetId(name) : -1;
    if (existing_dataset_id != -1 && existing_dataset_id != dataset_id)
    {
        return QString("error:数据集名称已存在");
    }
    return QString();
}

QString DataManager::isValidClassName(const QString &name, const int64_t label_class_id) const
{
    const QString name_error = isValidName(name);
    if (!name_error.isEmpty())
    {
        return name_error;
    }

    const int existing_label_class_id = label_classes_ ? label_classes_->getLabelClassId(name) : -1;
    if (existing_label_class_id != -1 && existing_label_class_id != label_class_id)
    {
        return QString("error:类别名称已存在");
    }
    return QString();
}

void DataManager::deleteDatasets(const std::vector<int64_t> &dataset_ids)
{
    if (dataset_deletion_running_)
    {
        ui::SignalHelper::notifyWarn(QString("删除数据集"), QString("数据集删除任务正在进行中"));
        return;
    }
    if (import_running_)
    {
        ui::SignalHelper::notifyWarn(QString("删除数据集"), QString("导入任务正在进行中，请在导入完成后再删除数据集"));
        return;
    }
    if (database_ == nullptr)
    {
        const QString message = QString("项目数据库未初始化");
        spdlog::error("删除数据集失败: {}", message.toUtf8().constData());
        ui::SignalHelper::notifyError(QString("删除数据集"), message);
        return;
    }

    std::vector<int64_t> target_dataset_ids;
    target_dataset_ids.reserve(dataset_ids.size());
    for (const int64_t dataset_id : dataset_ids)
    {
        if (dataset_id >= 0)
        {
            target_dataset_ids.push_back(dataset_id);
        }
    }
    std::sort(target_dataset_ids.begin(), target_dataset_ids.end());
    target_dataset_ids.erase(std::unique(target_dataset_ids.begin(), target_dataset_ids.end()), target_dataset_ids.end());
    if (target_dataset_ids.empty())
    {
        return;
    }

    // A label-loading worker can be in flight while deletion starts.  Its result must
    // not reintroduce labels that this transaction removes.
    if (labels_loading_)
    {
        labels_changed_during_loading_ = true;
    }

    dataset_deletion_running_ = true;
    emit datasetDeletionRunningChanged();

    ui::ProgressManager::getInstance()->startTask(QString("删除数据集"));
    ui::ProgressManager::getInstance()->updateProgress(5);
    ui::ProgressManager::getInstance()->addMessage(
        spdlog::level::info, QString("正在删除 %1 个数据集及其图像、标注和标签").arg(target_dataset_ids.size()));

    const QString         database_path = database_->path();
    QPointer<DataManager> manager(this);
    QThread *worker_thread = QThread::create(
        [manager, database_path, target_dataset_ids]()
        {
            QElapsedTimer timer;
            timer.start();

            bool    success = false;
            QString err_msg;
            try
            {
                dltool::database::ProjectDataBase database(database_path);
                success = database.deleteDatasetsWithContents(target_dataset_ids, err_msg);
            }
            catch (const std::exception &e)
            {
                err_msg = QString::fromUtf8(e.what());
            }

            const qint64 elapsed_ms = timer.elapsed();
            if (manager)
            {
                QMetaObject::invokeMethod(
                    manager.data(),
                    [manager, target_dataset_ids, success, err_msg, elapsed_ms]()
                    {
                        if (manager)
                        {
                            manager->handleAsyncDatasetDeletion(target_dataset_ids, success, err_msg, elapsed_ms);
                        }
                    },
                    Qt::QueuedConnection);
            }
        });

    connect(worker_thread, &QThread::finished, worker_thread, &QObject::deleteLater);
    worker_thread->start();
}

void DataManager::handleAsyncDatasetDeletion(const std::vector<int64_t> &dataset_ids, const bool success,
                                             const QString &err_msg, const qint64 elapsed_ms)
{
    if (success)
    {
        // Only QAbstractItemModel state is touched on this thread.  The database has
        // already committed, so these helpers deliberately perform no database writes.
        const std::vector<int64_t> image_ids
            = image_instances_ != nullptr ? image_instances_->getImageIdsForDatasets(dataset_ids) : std::vector<int64_t>{};
        if (label_instances_ != nullptr)
        {
            label_instances_->removeLabelsForImagesFromMemory(image_ids);
        }
        if (image_tags_ != nullptr)
        {
            image_tags_->removeImagesTagsFromMemory(image_ids);
        }
        if (image_instances_ != nullptr)
        {
            image_instances_->removeImagesFromMemory(image_ids);
        }
        if (datasets_ != nullptr)
        {
            datasets_->removeDatasetsFromMemory(dataset_ids);
        }
        if (global_filter_ != nullptr)
        {
            global_filter_->refresh();
        }

        const QString message
            = QString("已删除 %1 个数据集，耗时 %2 ms").arg(dataset_ids.size()).arg(elapsed_ms);
        spdlog::info("{}", message.toUtf8().constData());
        ui::ProgressManager::getInstance()->updateProgress(100);
        ui::ProgressManager::getInstance()->addMessage(spdlog::level::info, message);
        ui::SignalHelper::notifySuccess(QString("删除数据集完成"), message);
    }
    else
    {
        const QString message = QString("删除数据集失败: %1").arg(err_msg);
        spdlog::error("{}", message.toUtf8().constData());
        ui::ProgressManager::getInstance()->addMessage(spdlog::level::err, message);
        ui::SignalHelper::notifyError(QString("删除数据集失败"), message);
    }

    ui::ProgressManager::getInstance()->completeTask();
    dataset_deletion_running_ = false;
    emit datasetDeletionRunningChanged();

    if (labels_reload_after_dataset_deletion_ && !labels_loading_)
    {
        labels_reload_after_dataset_deletion_ = false;
        startAsyncLabelLoading();
    }
}

void DataManager::importData(const int64_t dataset_id, const int data_format, const QString &image_dir,
                             const QString &data_dir)
{
    startImportData(dataset_id, data_format, image_dir, data_dir, {});
}

void DataManager::scanImportLabelClasses(const int data_format, const QString &image_dir, const QString &data_dir)
{
    if (import_running_)
    {
        const QString message = QString("已有导入任务正在运行");
        ui::SignalHelper::notifyWarn(QString("导入失败"), message);
        emit importLabelClassesScanned(false, {}, message);
        return;
    }

    if (!data::DataFormat::isImportDataFormatSupported(method_, data_format))
    {
        const QString message = QString("当前项目类型不支持该导入格式");
        ui::SignalHelper::notifyError(QString("导入失败"), message);
        emit importLabelClassesScanned(false, {}, message);
        return;
    }

    DataIO *scanner = DataIO::createIO(data_format, database_, this);
    if (!scanner)
    {
        const QString message = QString("不支持的数据格式");
        ui::SignalHelper::notifyError(QString("导入失败"), message);
        emit importLabelClassesScanned(false, {}, message);
        return;
    }

    scanner->setTargetMethod(method_);
    qRegisterMetaType<std::map<QString, QString>>("std::map<QString, QString>");

    QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "startTask", Qt::QueuedConnection,
                              Q_ARG(QString, "扫描导入类别"));

    connect(
        scanner, &DataIO::labelClassesScanned, this,
        [this, scanner](bool success, const std::map<QString, QString> &label_class_info, const QString &message)
        {
            QVariantList label_classes;
            if (success)
            {
                for (const auto &[name, color] : label_class_info)
                {
                    const int     label_class_id  = label_classes_ ? label_classes_->getLabelClassId(name) : -1;
                    const QString effective_color = label_class_id >= 0 && label_classes_
                                                      ? label_classes_->getLabelClassColor(label_class_id)
                                                      : color;
                    const QString group           = label_class_id >= 0 && label_classes_
                                                      ? label_classes_->getLabelClassGroup(label_class_id)
                                                      : defaultLabelClassGroup();

                    QVariantMap item;
                    item.insert(QStringLiteral("label_class_id"), label_class_id);
                    item.insert(QStringLiteral("name"), name);
                    item.insert(QStringLiteral("color"), effective_color);
                    item.insert(QStringLiteral("group"), normalizeLabelClassGroup(group));
                    item.insert(QStringLiteral("group_name"), labelClassGroupDisplayName(group));
                    item.insert(QStringLiteral("existing"), label_class_id >= 0);
                    label_classes.append(item);
                }
            }

            const int     level            = success ? spdlog::level::info : spdlog::level::err;
            const QString progress_message = message.isEmpty() ? QString("导入类别扫描完成") : message;
            QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection,
                                      Q_ARG(int, level), Q_ARG(QString, progress_message));
            QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "completeTask", Qt::QueuedConnection);

            if (!success)
            {
                ui::SignalHelper::notifyError(QString("导入失败"),
                                              message.isEmpty() ? QString("扫描导入类别失败") : message);
            }

            emit importLabelClassesScanned(success, label_classes, message);
            scanner->deleteLater();
        },
        Qt::QueuedConnection);

    scanner->startScanLabelClasses(image_dir, data_dir);
}

void DataManager::importDataWithLabelClassGroups(const int64_t dataset_id, const int data_format,
                                                 const QString &image_dir, const QString &data_dir,
                                                 const QVariantMap &label_class_groups)
{
    startImportData(dataset_id, data_format, image_dir, data_dir, parseLabelClassGroupMap(label_class_groups));
}

void DataManager::startImportData(const int64_t dataset_id, const int data_format, const QString &image_dir,
                                  const QString &data_dir, const std::map<QString, QString> &label_class_groups)
{
    if (import_running_)
    {
        const QString message = QString("已有导入任务正在运行");
        spdlog::warn("导入数据失败, 已有导入任务正在运行");
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "startTask", Qt::QueuedConnection,
                                  Q_ARG(QString, "导入数据"));
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection,
                                  Q_ARG(int, spdlog::level::warn), Q_ARG(QString, message));
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "completeTask", Qt::QueuedConnection);
        ui::SignalHelper::notifyWarn(QString("导入失败"), message);
        emit dataImportFinished(false, message);
        return;
    }

    // 验证数据格式是否支持当前项目类型
    if (!data::DataFormat::isImportDataFormatSupported(method_, data_format))
    {
        const QString message = QString("当前项目类型不支持该导入格式");
        spdlog::error("导入数据失败, 项目类型 {} 不支持数据格式: {}", method_, data_format);
        ui::SignalHelper::notifyError(QString("导入失败"), message);
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
        ui::SignalHelper::notifyError(QString("导入失败"), message);
        emit dataImportFinished(false, message);
        return;
    }

    // 使用工厂函数创建导入器
    // 重构后：DataManager 不再直接实例化具体的导入器类
    // 而是通过工厂函数获取，实现了依赖倒置原则
    DataIO *importer = DataIO::createIO(data_format, database_, this);
    if (!importer)
    {
        const QString message = QString("不支持的数据格式");
        spdlog::error("无法为格式 {} 创建导入器", data_format);
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection,
                                  Q_ARG(int, spdlog::level::err), Q_ARG(QString, message));
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "completeTask", Qt::QueuedConnection);
        ui::SignalHelper::notifyError(QString("导入失败"), message);
        emit dataImportFinished(false, message);
        return;
    }
    importer->setTargetMethod(method_);
    import_running_                             = true;
    pending_import_task_                        = std::make_unique<PendingImportTask>();
    pending_import_task_->importer              = importer;
    pending_import_task_->dataset_id            = dataset_id;
    pending_import_task_->data_format           = data_format;
    pending_import_task_->label_class_group_map = label_class_groups;
    pending_import_task_->elapsed_timer.start();

    qRegisterMetaType<std::vector<QString>>("std::vector<QString>");
    qRegisterMetaType<std::vector<int64_t>>("std::vector<int64_t>");
    qRegisterMetaType<std::map<QString, QString>>("std::map<QString, QString>");
    qRegisterMetaType<std::vector<ImportedLabel>>("std::vector<ImportedLabel>");

    // 导入器每解析出一批数据就交给 DataManager 写库。
    // BlockingQueuedConnection 可以限制后台线程速度，避免批次在主线程事件队列中大量堆积。
    connect(importer, &DataIO::dataBatchReady, this, &DataManager::handleDataBatchReady, Qt::BlockingQueuedConnection);
    connect(importer, &DataIO::importFinished, this, &DataManager::handleImportFinished, Qt::QueuedConnection);

    // 启动导入
    importer->startImport(dataset_id, image_dir, data_dir);
}

void DataManager::exportDatasets(const std::vector<int64_t> &dataset_ids, const int data_format,
                                 const QString &output_dir, const QVariantMap &options)
{
    if (!data::DataFormat::isExportDataFormatSupported(method_, data_format))
    {
        const QString message = QString("当前项目类型不支持该导出格式");
        spdlog::error("导出数据失败, 项目类型 {} 不支持数据格式: {}", method_, data_format);
        ui::SignalHelper::notifyError(QString("导出失败"), message);
        return;
    }

    if (output_dir.isEmpty())
    {
        const QString message = QString("输出目录为空");
        spdlog::error("导出数据失败, {}", message.toUtf8().constData());
        ui::SignalHelper::notifyError(QString("导出失败"), message);
        return;
    }

    if (dataset_ids.empty())
    {
        const QString message = QString("未选择数据集");
        spdlog::warn("导出数据失败, {}", message.toUtf8().constData());
        ui::SignalHelper::notifyWarn(QString("导出失败"), message);
        return;
    }

    QString err_msg;
    if (!ensureDirectory(output_dir, err_msg))
    {
        spdlog::error("导出数据失败, {}", err_msg.toUtf8().constData());
        ui::SignalHelper::notifyError(QString("导出失败"), err_msg);
        return;
    }

    QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "startTask", Qt::QueuedConnection,
                              Q_ARG(QString, "导出数据"));

    struct ExportBatchItem
    {
        ExportDataset dataset;
        QString       output_dir;
    };

    struct ExportBatchState
    {
        std::vector<ExportBatchItem> items;
        int                          current{0};
        int                          success_count{0};
        int                          failed_count{0};
    };

    auto state = std::make_shared<ExportBatchState>();
    for (const int64_t dataset_id : dataset_ids)
    {
        ExportDataset dataset = buildExportDataset(dataset_id);
        if (dataset.dataset_name.isEmpty())
        {
            addProgressMessage(spdlog::level::warn, QString("跳过不存在的数据集: %1").arg(dataset_id));
            continue;
        }
        if (dataset.images.empty())
        {
            spdlog::warn("跳过空数据集导出: {}", dataset_id);
            addProgressMessage(spdlog::level::warn, QString("跳过空数据集: %1").arg(dataset.dataset_name));
            continue;
        }

        const QString dataset_output_dir = QDir(output_dir).filePath(dataset.dataset_name);
        if (!ensureDirectory(dataset_output_dir, err_msg))
        {
            spdlog::error("创建数据集导出目录失败: {}", err_msg.toUtf8().constData());
            addProgressMessage(spdlog::level::err, err_msg);
            ++state->failed_count;
            continue;
        }

        state->items.push_back(ExportBatchItem{std::move(dataset), dataset_output_dir});
    }

    if (state->items.empty())
    {
        const QString message = QString("没有可导出的数据集");
        addProgressMessage(spdlog::level::err, message);
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "completeTask", Qt::QueuedConnection);
        ui::SignalHelper::notifyError(QString("导出失败"), message);
        return;
    }

    auto                                 start_next      = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weak_start_next = start_next;
    *start_next                                          = [this, data_format, options, state, weak_start_next]()
    {
        if (state->current >= static_cast<int>(state->items.size()))
        {
            const QString message
                = QString("导出完成: 成功 %1 个, 失败 %2 个").arg(state->success_count).arg(state->failed_count);
            const bool success = state->success_count > 0 && state->failed_count == 0;
            addProgressMessage(state->failed_count == 0 ? spdlog::level::info : spdlog::level::warn, message);
            QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "completeTask", Qt::QueuedConnection);
            if (success)
                ui::SignalHelper::notifySuccess(QString("导出完成"), message);
            else if (state->success_count > 0)
                ui::SignalHelper::notifyWarn(QString("导出完成"), message);
            else
                ui::SignalHelper::notifyError(QString("导出失败"), message);
            return;
        }

        const ExportBatchItem item = state->items[static_cast<size_t>(state->current++)];
        addProgressMessage(spdlog::level::info,
                           QString("开始导出数据集: %1 -> %2").arg(item.dataset.dataset_name, item.output_dir));

        DataIO *exporter = DataIO::createIO(data_format, nullptr, this);
        if (!exporter)
        {
            addProgressMessage(spdlog::level::err, QString("不支持的数据格式"));
            ++state->failed_count;
            if (auto next = weak_start_next.lock())
            {
                (*next)();
            }
            return;
        }
        exporter->setTargetMethod(method_);

        connect(
            exporter, &DataIO::exportFinished, this,
            [exporter, state, start_next = weak_start_next.lock()](bool success, const QString &message)
            {
                if (success)
                {
                    ++state->success_count;
                }
                else
                {
                    ++state->failed_count;
                }
                addProgressMessage(success ? spdlog::level::info : spdlog::level::err, message);
                exporter->deleteLater();
                if (start_next)
                {
                    (*start_next)();
                }
            },
            Qt::QueuedConnection);

        exporter->startExport(item.dataset, item.output_dir, options);
    };

    (*start_next)();
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

    if (label_classes_ == nullptr)
    {
        return dataset;
    }

    for (int row = 0; row < label_classes_->rowCount(); ++row)
    {
        const QModelIndex index = label_classes_->index(row, 0);
        const int64_t     label_class_id
            = label_classes_->data(index, LabelClassesListModel::LabelClassIdRole).toLongLong();
        if (label_class_ids.find(label_class_id) == label_class_ids.end())
        {
            continue;
        }

        ExportLabelClass export_class;
        export_class.id    = label_class_id;
        export_class.name  = label_classes_->data(index, LabelClassesListModel::NameRole).toString();
        export_class.color = label_classes_->data(index, LabelClassesListModel::ColorRole).toString();
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

void DataManager::copyToDataset(const std::vector<int64_t> &image_ids, const int64_t dataset_id)
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

    std::vector<int64_t> source_image_ids = image_ids;
    source_image_ids.erase(std::remove_if(source_image_ids.begin(), source_image_ids.end(),
                                          [](const int64_t image_id) { return image_id < 0; }),
                           source_image_ids.end());
    std::sort(source_image_ids.begin(), source_image_ids.end());
    source_image_ids.erase(std::unique(source_image_ids.begin(), source_image_ids.end()), source_image_ids.end());
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
    std::vector<int64_t>              source_image_label_class_ids;
    source_image_label_class_ids.reserve(source_image_ids.size());
    for (const int64_t image_id : source_image_ids)
    {
        source_image_label_class_ids.push_back(image_instances_->getImageLabelClassId(image_id));
    }

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
    image_instances_->setImageLabelClassIds(copied_image_ids, source_image_label_class_ids);

    std::map<int64_t, std::vector<int64_t>> copied_images_by_tag;
    for (size_t i = 0; i < copied_image_ids.size() && i < source_tag_ids.size(); ++i)
    {
        for (const int64_t tag_id : source_tag_ids[i])
        {
            copied_images_by_tag[tag_id].push_back(copied_image_ids[i]);
        }
    }
    for (const auto &[tag_id, tagged_image_ids] : copied_images_by_tag)
    {
        image_tags_->setImagesTag(tagged_image_ids, tag_id);
    }

    std::vector<int64_t>     copied_label_image_ids;
    std::vector<int64_t>     copied_label_class_ids;
    std::vector<QVariantMap> copied_label_data;
    std::vector<std::vector<int64_t>> copied_label_tag_ids;
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
            copied_label_tag_ids.emplace_back(label_instance->tagIds().begin(), label_instance->tagIds().end());
        }
    }

    if (!copied_label_image_ids.empty())
    {
        std::vector<int64_t> copied_label_ids;
        if (!addLabelsInternal(copied_label_image_ids, copied_label_class_ids, copied_label_data, nullptr, true,
                               &copied_label_ids))
        {
            updateDatasetsStats();
            image_info_->updateLabelInfo();
        }
        else
        {
            std::map<int64_t, std::vector<int64_t>> labels_by_tag;
            for (size_t i = 0; i < copied_label_ids.size() && i < copied_label_tag_ids.size(); ++i)
            {
                for (const int64_t tag_id : copied_label_tag_ids[i])
                {
                    labels_by_tag[tag_id].push_back(copied_label_ids[i]);
                }
            }
            for (const auto &[tag_id, label_ids] : labels_by_tag)
            {
                image_tags_->setLabelsTag(label_ids, tag_id);
            }
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

void DataManager::moveToDataset(const std::vector<int64_t> &image_ids, const int64_t dataset_id)
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

    std::vector<int64_t> selected_image_ids = image_ids;
    selected_image_ids.erase(std::remove_if(selected_image_ids.begin(), selected_image_ids.end(),
                                            [](const int64_t image_id) { return image_id < 0; }),
                             selected_image_ids.end());
    std::sort(selected_image_ids.begin(), selected_image_ids.end());
    selected_image_ids.erase(std::unique(selected_image_ids.begin(), selected_image_ids.end()),
                             selected_image_ids.end());
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
    addLabelClassWithGroup(name, color, shortcut, defaultLabelClassGroup());
}

void DataManager::addLabelClassWithGroup(const QString &name, const QString &color, const QString &shortcut,
                                         const QString &group)
{
    const QString validation_error = isValidClassName(name);
    if (!validation_error.isEmpty())
    {
        spdlog::warn("添加标签类别失败: {}", validation_error.toUtf8().constData());
        return;
    }
    if (label_classes_ != nullptr)
    {
        const QString label_error = label_classes_->isValid(-1, name, color, shortcut, -1);
        if (label_error.startsWith(QStringLiteral("error:")))
        {
            spdlog::warn("添加标签类别失败: {}", label_error.toUtf8().constData());
            return;
        }
    }
    label_classes_->addLabelClass(name, color, shortcut, group);
}

void DataManager::updateLabelClass(const int64_t label_class_id, const QString &name, const QString &color,
                                   const QString &shortcut, const int64_t ordinal_index)
{
    const QString group = label_classes_ ? label_classes_->getLabelClassGroup(static_cast<int>(label_class_id))
                                         : defaultLabelClassGroup();
    updateLabelClassWithGroup(label_class_id, name, color, shortcut, ordinal_index, group);
}

void DataManager::updateLabelClassWithGroup(const int64_t label_class_id, const QString &name, const QString &color,
                                            const QString &shortcut, const int64_t ordinal_index, const QString &group)
{
    const QString validation_error = isValidClassName(name, label_class_id);
    if (!validation_error.isEmpty())
    {
        spdlog::warn("更新标签类别失败: {}", validation_error.toUtf8().constData());
        return;
    }
    if (label_classes_ != nullptr)
    {
        const QString label_error = label_classes_->isValid(static_cast<int>(label_class_id), name, color, shortcut,
                                                            static_cast<int>(ordinal_index));
        if (label_error.startsWith(QStringLiteral("error:")))
        {
            spdlog::warn("更新标签类别失败: {}", label_error.toUtf8().constData());
            return;
        }
    }

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
    label_classes_->updateLabelClass(label_class_id, name, color, shortcut, ordinal_index, group);
    image_labels_list_->labelClassUpdated(label_class_id);
    image_labels_table_->labelClassUpdated(label_class_id);
    image_info_->updateLabelInfo();
}

void DataManager::updateLabelClassGroup(const int64_t label_class_id, const QString &group)
{
    if (label_classes_ == nullptr)
    {
        return;
    }
    if (!label_classes_->updateLabelClassGroup(label_class_id, group))
    {
        return;
    }
    image_labels_list_->labelClassUpdated(label_class_id);
    image_labels_table_->labelClassUpdated(label_class_id);
    image_info_->updateLabelInfo();
}

void DataManager::deleteLabelClass(const int64_t label_class_id)
{
    std::vector<int64_t> label_ids = label_instances_->getLabelIds(label_class_id);
    deleteLabels(label_ids);

    std::vector<int64_t> images_to_clear;
    std::vector<int64_t> clear_values;
    if (image_instances_ != nullptr)
    {
        for (const int64_t image_id : image_instances_->getAllImageIds())
        {
            if (image_instances_->getImageLabelClassId(image_id) == label_class_id)
            {
                images_to_clear.push_back(image_id);
                clear_values.push_back(-1);
            }
        }
        if (!images_to_clear.empty())
        {
            image_instances_->setImageLabelClassIds(images_to_clear, clear_values);
        }
    }

    label_classes_->deleteLabelClass(label_class_id);
    if (image_info_ != nullptr)
    {
        image_info_->updateLabelInfo();
    }
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
                                    const std::vector<QVariantMap> &data, QString *err_msg,
                                    const bool refresh_dependent_models, std::vector<int64_t> *added_label_ids)
{
    if (added_label_ids != nullptr)
    {
        added_label_ids->clear();
    }

    std::vector<int64_t> label_ids;
    if (!label_instances_->tryAddLabels(label_ids, image_ids, label_class_ids, data, err_msg,
                                        !refresh_dependent_models))
    {
        return false;
    }
    if (labels_loading_)
    {
        labels_changed_during_loading_ = true;
    }
    if (added_label_ids != nullptr)
    {
        *added_label_ids = label_ids;
    }

    if (!refresh_dependent_models)
    {
        return true;
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
    std::vector<std::vector<int64_t>> label_tag_ids;
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
        label_tag_ids.emplace_back(instance->tagIds().begin(), instance->tagIds().end());
    }

    if (!image_ids.empty())
    {
        std::vector<int64_t> duplicated_label_ids;
        if (!addLabelsInternal(image_ids, label_class_ids, labels_data, nullptr, true, &duplicated_label_ids)
            || image_tags_ == nullptr)
        {
            return;
        }

        std::map<int64_t, std::vector<int64_t>> labels_by_tag;
        for (size_t i = 0; i < duplicated_label_ids.size() && i < label_tag_ids.size(); ++i)
        {
            for (const int64_t tag_id : label_tag_ids[i])
            {
                labels_by_tag[tag_id].push_back(duplicated_label_ids[i]);
            }
        }
        for (const auto &[tag_id, label_ids] : labels_by_tag)
        {
            image_tags_->setLabelsTag(label_ids, tag_id);
        }
    }
}

bool DataManager::setImageLabelClass(const int64_t image_id, const int64_t label_class_id)
{
    if (image_instances_ == nullptr || label_classes_ == nullptr)
    {
        return false;
    }
    if (image_id < 0)
    {
        return false;
    }
    if (label_class_id >= 0 && label_classes_->getLabelClassName(static_cast<int>(label_class_id)).isEmpty())
    {
        return false;
    }

    const int64_t effective_label_class_id
        = method_ == core::DeepLearningMethod::AnomalyDetection && label_class_id >= 0
               && label_classes_->isUnlabeledLabelClass(static_cast<int>(label_class_id))
            ? -1
            : label_class_id;

    const bool ok = image_instances_->setImageLabelClassId(image_id, effective_label_class_id);
    if (ok)
    {
        updateDatasetsStats();
        if (image_info_ != nullptr)
        {
            image_info_->updateLabelInfo();
        }
    }
    return ok;
}

QVariantMap DataManager::getImageLevelLabelData(const int64_t image_id) const
{
    QVariantMap data;
    if (image_instances_ == nullptr || label_classes_ == nullptr || image_id < 0)
    {
        return data;
    }

    const int64_t label_class_id = image_instances_->getImageLabelClassId(image_id);
    if (label_class_id < 0)
    {
        return data;
    }

    const QString class_name = label_classes_->getLabelClassName(static_cast<int>(label_class_id));
    if (class_name.isEmpty())
    {
        return data;
    }

    const QString group = label_classes_->getLabelClassGroup(static_cast<int>(label_class_id));
    data.insert(QStringLiteral("label_class_id"), static_cast<qlonglong>(label_class_id));
    data.insert(QStringLiteral("label_class_name"), class_name);
    data.insert(QStringLiteral("color"), label_classes_->getLabelClassColor(static_cast<int>(label_class_id)));
    data.insert(QStringLiteral("group"), group);
    data.insert(QStringLiteral("group_name"), labelClassGroupDisplayName(group));
    return data;
}

void DataManager::refreshAnomalyImageClassesFromPolygons(const std::vector<int64_t> &image_ids, bool only_unset)
{
    if (method_ != core::DeepLearningMethod::AnomalyDetection || image_instances_ == nullptr
        || label_instances_ == nullptr || label_classes_ == nullptr)
    {
        return;
    }

    if (image_ids.empty())
    {
        return;
    }

    std::vector<int64_t> images_to_update;
    std::vector<int64_t> classes_to_set;
    images_to_update.reserve(image_ids.size());
    classes_to_set.reserve(image_ids.size());

    for (const int64_t image_id : image_ids)
    {
        if (only_unset && image_instances_->getImageLabelClassId(image_id) >= 0)
        {
            continue;
        }

        int64_t first_class_id         = -1;
        int64_t first_anomaly_class_id = -1;
        for (const int64_t label_id : label_instances_->getImageLabelIds(image_id))
        {
            const int64_t label_class_id = label_instances_->getLabelClassId(label_id);
            if (label_class_id < 0 || label_classes_->isUnlabeledLabelClass(static_cast<int>(label_class_id)))
            {
                continue;
            }
            if (first_class_id < 0)
            {
                first_class_id = label_class_id;
            }
            if (label_classes_->isAnomalyLabelClass(static_cast<int>(label_class_id)))
            {
                first_anomaly_class_id = label_class_id;
                break;
            }
        }

        const int64_t target_class_id = first_anomaly_class_id >= 0 ? first_anomaly_class_id : first_class_id;
        if (image_instances_->getImageLabelClassId(image_id) != target_class_id)
        {
            images_to_update.push_back(image_id);
            classes_to_set.push_back(target_class_id);
        }
    }

    if (!images_to_update.empty() && image_instances_->setImageLabelClassIds(images_to_update, classes_to_set))
    {
        updateDatasetsStats();
        if (image_info_ != nullptr)
        {
            image_info_->updateLabelInfo();
        }
    }
}

void DataManager::addTagClass(const QString &name)
{
    if (image_tags_ == nullptr || name.trimmed().isEmpty() || findTagClassId(name) >= 0)
    {
        return;
    }
    image_tags_->addTagClass(name.trimmed());
}

int64_t DataManager::findTagClassId(const QString &name) const
{
    return image_tags_ ? image_tags_->findTagClassId(name) : -1;
}

bool DataManager::setLabelsTag(const std::vector<int64_t> &label_ids, const int64_t tag_id)
{
    return image_tags_ != nullptr && image_tags_->addLabelsTag(label_ids, tag_id);
}

bool DataManager::deleteTagClass(const int64_t tag_id)
{
    return image_tags_ != nullptr && image_tags_->deleteTagClass(tag_id);
}

void DataManager::updateDatasetsStats()
{
    if (image_instances_ == nullptr || datasets_ == nullptr)
        return;
    std::vector<int64_t>              dataset_ids, image_ids;
    std::vector<std::vector<int64_t>> images_label_ids;
    image_instances_->getAllDatasetsImagesLabels(dataset_ids, image_ids, images_label_ids);
    std::vector<int64_t> image_label_class_ids;
    image_label_class_ids.reserve(image_ids.size());
    for (const int64_t image_id : image_ids)
    {
        image_label_class_ids.push_back(image_instances_->getImageLabelClassId(image_id));
    }
    datasets_->setStats(dataset_ids, image_ids, images_label_ids, image_label_class_ids);
}

void DataManager::handleDataBatchReady(int64_t dataset_id, std::vector<QString> image_paths,
                                       std::vector<int64_t> image_widths, std::vector<int64_t> image_heights,
                                       std::map<QString, QString> label_class_info, std::vector<ImportedLabel> labels,
                                       int64_t processed_images, int64_t total_images)
{
    Q_UNUSED(image_widths)
    Q_UNUSED(image_heights)

    DataIO *importer = qobject_cast<DataIO *>(sender());
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

    const bool anomaly_project = method_ == core::DeepLearningMethod::AnomalyDetection;
    const bool folder_import   = task.data_format == DataFormat::Folder;

    auto track_imported_image = [&](int64_t image_id)
    {
        if (image_id >= 0 && task.imported_image_id_set.insert(image_id).second)
        {
            task.imported_image_ids.push_back(image_id);
        }
    };

    auto ensure_label_class = [&](const QString &label_name, const QString &color) -> int64_t
    {
        if (label_name.isEmpty())
        {
            return -1;
        }

        auto cached_it = task.label_class_map.find(label_name);
        if (cached_it != task.label_class_map.end())
        {
            return cached_it->second;
        }

        const auto    group_it       = task.label_class_group_map.find(label_name);
        const QString selected_group = group_it != task.label_class_group_map.end()
                                         ? normalizeLabelClassGroup(group_it->second)
                                         : defaultLabelClassGroup();

        int64_t label_class_id = label_classes_ ? label_classes_->getLabelClassId(label_name) : -1;
        if (label_class_id < 0 && label_classes_ != nullptr)
        {
            const QString validation_error = isValidClassName(label_name);
            if (!validation_error.isEmpty())
            {
                spdlog::warn("导入时创建标签类别失败: {}, {}", label_name.toUtf8().constData(),
                             validation_error.toUtf8().constData());
            }
            else if (label_classes_->addLabelClass(label_name, color, QString(), selected_group))
            {
                label_class_id = label_classes_->getLabelClassId(label_name);
                spdlog::info("导入时创建新标签类别: {}, ID: {}, group={}", label_name.toUtf8().constData(),
                             label_class_id, selected_group.toUtf8().constData());
            }
            else
            {
                spdlog::warn("导入时创建标签类别失败: {}", label_name.toUtf8().constData());
            }
        }
        else if (anomaly_project && label_class_id >= 0 && group_it != task.label_class_group_map.end()
                 && label_classes_ != nullptr)
        {
            const QString current_group = label_classes_->getLabelClassGroup(static_cast<int>(label_class_id));
            if (normalizeLabelClassGroup(current_group) != selected_group)
                label_classes_->updateLabelClassGroup(label_class_id, selected_group);
        }

        if (label_class_id >= 0)
        {
            task.label_class_map[label_name] = label_class_id;
        }
        return label_class_id;
    };

    for (const auto &[label_name, color] : label_class_info)
    {
        ensure_label_class(label_name, color);
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
                track_imported_image(existing_it->second);
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

        if (!new_image_paths.empty()
            && !image_instances_->addImages(task.dataset_id, new_image_paths, image_ids, true))
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
        task.deferred_ui_refresh = task.deferred_ui_refresh || !image_ids.empty();
        task.deferred_image_model_refresh = task.deferred_image_model_refresh || !image_ids.empty();
        task.deferred_dataset_image_ids.insert(task.deferred_dataset_image_ids.end(), image_ids.begin(), image_ids.end());

        for (size_t i = 0; i < new_image_paths.size(); ++i)
        {
            task.normalized_image_path_to_id[new_normalized_paths[i]] = image_ids[i];
            const auto pending_it = pending_raw_paths_by_normalized_path.find(new_normalized_paths[i]);
            if (pending_it == pending_raw_paths_by_normalized_path.end())
                continue;

            for (const QString &raw_path : pending_it->second) task.image_path_to_id[raw_path] = image_ids[i];
            track_imported_image(image_ids[i]);
        }
    }

    std::vector<int64_t>       batch_label_image_ids;
    std::vector<int64_t>       batch_label_class_ids;
    std::vector<QVariantMap>   batch_label_data;
    std::map<int64_t, int64_t> batch_image_level_class_updates;

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
        const int64_t image_id = image_it->second;
        track_imported_image(image_id);

        const QString fallback_color = DatasetIO::generateDefaultColor(static_cast<int>(task.label_class_map.size()));
        const int64_t label_class_id = ensure_label_class(label.label_class_name, fallback_color);
        if (label_class_id < 0)
        {
            spdlog::warn("未找到标签类别，跳过标注: {}, 图像: {}", label.label_class_name.toUtf8().constData(),
                         label.image_path.toUtf8().constData());
            task.skipped_labels++;
            continue;
        }

        if (anomaly_project && folder_import)
        {
            const int64_t image_level_class_id
                = label_classes_->isUnlabeledLabelClass(static_cast<int>(label_class_id)) ? -1 : label_class_id;
            auto folder_class_it = task.folder_class_by_image_id.find(image_id);
            if (folder_class_it == task.folder_class_by_image_id.end())
            {
                folder_class_it = task.folder_class_by_image_id.emplace(image_id, image_level_class_id).first;
            }
            batch_image_level_class_updates[image_id] = folder_class_it->second;
            task.imported_labels++;
            continue;
        }

        if (label.data.isEmpty())
        {
            spdlog::warn("跳过空的标注数据: label_class={}, 图像: {}", label.label_class_name.toUtf8().constData(),
                         label.image_path.toUtf8().constData());
            task.skipped_labels++;
            continue;
        }

        if (anomaly_project)
        {
            if (task.first_polygon_class_by_image_id.find(image_id) == task.first_polygon_class_by_image_id.end())
            {
                task.first_polygon_class_by_image_id[image_id] = label_class_id;
            }
            if (label_classes_->isAnomalyLabelClass(static_cast<int>(label_class_id))
                && task.first_anomaly_polygon_class_by_image_id.find(image_id)
                       == task.first_anomaly_polygon_class_by_image_id.end())
            {
                task.first_anomaly_polygon_class_by_image_id[image_id] = label_class_id;
            }

            const auto anomaly_it                     = task.first_anomaly_polygon_class_by_image_id.find(image_id);
            batch_image_level_class_updates[image_id] = anomaly_it != task.first_anomaly_polygon_class_by_image_id.end()
                                                          ? anomaly_it->second
                                                          : task.first_polygon_class_by_image_id[image_id];
        }

        batch_label_image_ids.push_back(image_id);
        batch_label_class_ids.push_back(label_class_id);
        batch_label_data.push_back(label.data);
    }

    if (!batch_label_image_ids.empty())
    {
        QString label_err_msg;
        if (!addLabelsInternal(batch_label_image_ids, batch_label_class_ids, batch_label_data, &label_err_msg,
                               false))
        {
            err_msg = QString("添加标注失败，当前标注批次已跳过。待写入标注 %1 个").arg(batch_label_image_ids.size());
            if (!label_err_msg.isEmpty())
            {
                err_msg += QString("，原因: %1").arg(label_err_msg);
            }
            return false;
        }
        task.imported_labels += batch_label_image_ids.size();
        task.deferred_ui_refresh = true;
        task.deferred_label_model_refresh = true;
    }

    if (!batch_image_level_class_updates.empty())
    {
        std::vector<int64_t> image_level_image_ids;
        std::vector<int64_t> image_level_class_ids;
        image_level_image_ids.reserve(batch_image_level_class_updates.size());
        image_level_class_ids.reserve(batch_image_level_class_updates.size());
        for (const auto &[image_id, class_id] : batch_image_level_class_updates)
        {
            if (image_instances_->getImageLabelClassId(image_id) == class_id)
            {
                continue;
            }
            image_level_image_ids.push_back(image_id);
            image_level_class_ids.push_back(class_id);
        }

        if (!image_level_image_ids.empty()
            && image_instances_->setImageLabelClassIds(image_level_image_ids, image_level_class_ids))
        {
            task.deferred_ui_refresh = true;
        }
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
    finishBatchedImport(true, message);
}

void DataManager::finishBatchedImport(bool success, const QString &message)
{
    DataIO    *importer     = pending_import_task_ ? pending_import_task_->importer : nullptr;
    const bool has_warnings = success && pending_import_task_ && pending_import_task_->failed_batches > 0;
    const bool refresh_dependent_models
        = pending_import_task_ != nullptr && pending_import_task_->deferred_ui_refresh;
    const bool refresh_image_model
        = pending_import_task_ != nullptr && pending_import_task_->deferred_image_model_refresh;
    const bool refresh_label_model
        = pending_import_task_ != nullptr && pending_import_task_->deferred_label_model_refresh;

    if (pending_import_task_ != nullptr && datasets_ != nullptr
        && !pending_import_task_->deferred_dataset_image_ids.empty())
    {
        const std::vector<int64_t> dataset_ids(pending_import_task_->deferred_dataset_image_ids.size(),
                                               pending_import_task_->dataset_id);
        datasets_->addImages(dataset_ids, pending_import_task_->deferred_dataset_image_ids);
    }

    // Import batches only write their primary data.  Rebuild the derived image-label
    // relationships, dataset statistics and current-image models once at the end so
    // a large project does not perform the same full-project work for every batch.
    if (refresh_dependent_models)
    {
        rebuildLabelRelations(!refresh_image_model);
        if (global_filter_ != nullptr && global_filter_->isActive())
        {
            global_filter_->refresh();
        }
        else
        {
            if (refresh_image_model && image_instances_ != nullptr)
            {
                image_instances_->refreshModelFromMemory();
            }
            if (refresh_label_model && label_instances_ != nullptr)
            {
                label_instances_->refreshModelFromMemory();
            }
        }
    }

    const qint64 elapsed_ms = pending_import_task_ != nullptr && pending_import_task_->elapsed_timer.isValid()
                                 ? pending_import_task_->elapsed_timer.elapsed()
                                 : 0;
    const QString completed_message = QString("%1，耗时 %2 ms").arg(message).arg(elapsed_ms);
    if (success)
    {
        spdlog::info("{}", completed_message.toUtf8().constData());
    }
    else
    {
        spdlog::error("{}", completed_message.toUtf8().constData());
    }

    const int level = success ? spdlog::level::info : spdlog::level::err;
    if (success)
    {
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "updateProgress", Qt::QueuedConnection,
                                  Q_ARG(int, 100));
    }
    QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection, Q_ARG(int, level),
                              Q_ARG(QString, completed_message));
    QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "completeTask", Qt::QueuedConnection);

    if (importer)
    {
        importer->deleteLater();
    }
    pending_import_task_.reset();
    import_running_ = false;
    emit dataImportFinished(success, completed_message);

    // Send the toast after the final model refresh and dataImportFinished signal
    // have been delivered.  This makes the completion notification correspond to
    // the state already visible in QML rather than only to parsing completion.
    QMetaObject::invokeMethod(
        ui::SignalHelper::getInstance(),
        [success, has_warnings, completed_message]()
        {
            if (success && has_warnings)
                ui::SignalHelper::notifyWarn(QString("导入完成"), completed_message);
            else if (success)
                ui::SignalHelper::notifySuccess(QString("导入完成"), completed_message);
            else
                ui::SignalHelper::notifyError(QString("导入失败"), completed_message);
        },
        Qt::QueuedConnection);
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

    engine->removeImageProvider(QStringLiteral("imageinstance"));
    engine->removeImageProvider(QStringLiteral("labelinstance"));

    auto *image_instance_provider = new ImageInstanceImageProvider(image_instances_);

    // 创建 LabelInstanceImageProvider 实例，传入三个模型指针
    auto *label_instance_provider = new LabelInstanceImageProvider(label_instances_, image_instances_, label_classes_);

    // 注册到 QML 引擎（使用小写名称，因为 QML Image 会自动转换为小写）
    engine->addImageProvider("imageinstance", image_instance_provider);
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
