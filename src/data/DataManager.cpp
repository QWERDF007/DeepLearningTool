#include "data/DataManager.h"

#include "DataExportService.h"
#include "DataManagerServices.h"
#include "DataImportService.h"
#include "ImageTransferService.h"
#include "DatasetSplitService.h"
#include "ClusterWritebackService.h"
#include "common/Utils.h"
#include "data/CategoryStatisticsModel.h"
#include "data/DataFormat.h"
#include "data/DataIO.h"
#include "data/DataNameUtils.h"
#include "data/DataOperationWorkflow.h"
#include "data/DatasetIO.h"
#include "data/DatasetSplitter.h"
#include "data/GlobalFilter.h"
#include "data/ImageInstanceImageProvider.h"
#include "data/LabelData.h"
#include "data/LabelInstanceImageProvider.h"
#include "database/DataBase.h"
#include "ui/ProgressManager.h"
#include "ui/SignalHelper.h"

#include <spdlog/spdlog.h>

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QMetaType>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QStringList>
#include <QThread>
#include <QUuid>
#include <algorithm>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>

using dltool::common::ensureDirectory;

namespace dltool::data {

namespace {

bool isFatalDatabaseError(const QString &message)
{
    return message.contains(QStringLiteral("database disk image is malformed"), Qt::CaseInsensitive)
        || message.contains(QStringLiteral("file is not a database"), Qt::CaseInsensitive);
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

namespace {

struct DatasetSplitRequest
{
    int                                                                label_data_method{-1};
    std::vector<dltool::database::ProjectDataBase::DatasetSplitTarget> targets;
};

} // namespace

DataManager::DataManager(const int method, dltool::database::ProjectDataBase *database, const QString &project_dir,
                         QObject *parent)
    : QObject(parent)
    , database_(database)
    , project_dir_(dltool::common::cleanPath(project_dir))
    , method_(method)
{
    init(method);
}

DataManager::~DataManager()
{
    shutdown();
}

bool DataManager::importRunning() const
{
    return import_service_ != nullptr && import_service_->importRunning();
}

bool DataManager::imageOperationRunning() const
{
    return (image_transfer_service_ != nullptr && image_transfer_service_->imageOperationRunning())
           || (cluster_writeback_service_ != nullptr && cluster_writeback_service_->imageOperationRunning());
}

DataManagerServices DataManager::makeServices()
{
    DataManagerServices services;
    services.database      = database_;
    services.datasets      = datasets_;
    services.image_source  = image_source_;
    services.label_classes = label_classes_;
    services.image_tags    = image_tags_;
    services.label_source  = label_source_;
    services.image_info      = image_info_;
    services.global_filter   = global_filter_;
    services.image_instances = image_instances_;
    services.label_instances = label_instances_;
    services.method        = method_;
    services.project_dir   = project_dir_;
    services.host          = this;
    services.shutting_down = [this]() { return shutting_down_; };
    services.is_labels_loading = [this]() { return labels_loading_; };
    services.mark_labels_changed_during_loading = [this]() { labels_changed_during_loading_ = true; };
    services.track_operation = [this](DataOperationWorkflow::HandlePtr handle)
    { return trackOperation(std::move(handle)); };
    services.set_data_operation_running = [this](const bool running) { setDataOperationRunning(running); };
    services.is_data_operation_running  = [this]() { return isDataOperationRunning(); };
    services.emit_data_import_finished  = [this](const bool success, const QString &message)
    { emit dataImportFinished(success, message); };
    services.emit_import_label_classes_scanned = [this](const bool success, QVariantList label_classes,
                                                        const QString &message)
    { emit importLabelClassesScanned(success, std::move(label_classes), message); };
    services.emit_image_operation_running_changed = [this]() { emit imageOperationRunningChanged(); };
    services.emit_dataset_split_finished          = [this](const bool success, const QString &message)
    { emit datasetSplitFinished(success, message); };
    services.emit_dataset_deletion_running_changed = [this]() { emit datasetDeletionRunningChanged(); };
    services.rebuild_label_relations = [this]() { rebuildLabelRelations(); };
    services.run_dataset_export_async = [this](QObject *context, DatasetExportRequest request,
                                               DataOperationWorkflow::Options options, DatasetExportWorkFn work,
                                               DataOperationWorkflow::Completion completion)
    {
        return runDatasetExportAsync(context, std::move(request), std::move(options), std::move(work),
                                     std::move(completion));
    };
    return services;
}

void DataManager::requestDataOperationCancel()
{
    if (export_service_)
    {
        export_service_->requestCancel();
    }

    const QList<DataIO *> io_children = findChildren<DataIO *>();
    for (DataIO *io : io_children)
    {
        if (io != nullptr)
        {
            io->requestCancel();
        }
    }

    for (const auto &handle : operation_handles_)
    {
        if (handle != nullptr)
        {
            handle->requestCancel();
        }
    }
}

void DataManager::cancelDataOperation()
{
    requestDataOperationCancel();
}

void DataManager::beginShutdown()
{
    shutting_down_ = true;
    requestDataOperationCancel();
}

void DataManager::shutdown()
{
    shutting_down_ = true;
    if (cleaned_up_)
        return;
    cleaned_up_ = true;

    // DataIO import/export workers can synchronously wait for a GUI-thread
    // batch callback.  Request cancellation first and keep the GUI event loop
    // able to drain those callbacks while waiting; otherwise project close
    // could deadlock with the importer.
    QList<QPointer<DataIO>> io_operations;
    const QList<DataIO *>   io_children = findChildren<DataIO *>();
    io_operations.reserve(io_children.size());
    for (DataIO *io : io_children)
    {
        if (io == nullptr)
            continue;
        io_operations.push_back(QPointer<DataIO>(io));
    }

    requestDataOperationCancel();

    waitForDataIoOperations(io_operations);
    if (import_service_)
        import_service_->resetImportSession();
    labels_loading_ = false;
    setDataOperationRunning(false);
    waitForOperations();
    operation_handles_.clear();

    // The worker and any synchronous batch hand-off have now converged.  The
    // remaining queued completion signals are intentionally discarded by the
    // shutdown guards below; leave the object in a stable terminal state
    // before Project releases the QObject graph.
}

void DataManager::waitForOperations()
{
    // Completion callbacks run on this object's thread.  A plain QThreadPool
    // wait would leave those callbacks queued and would not allow a feature
    // operation to start its final model update/copy step.
    for (;;)
    {
        bool pending = (import_service_ != nullptr && import_service_->importRunning()) || data_operation_running_;

        const QList<DataIO *> io_children = findChildren<DataIO *>();
        for (DataIO *io : io_children)
        {
            if (io != nullptr && !io->waitForDone(0))
            {
                pending = true;
                break;
            }
        }

        for (const auto &handle : operation_handles_)
        {
            if (handle != nullptr && (!handle->isFinished() || !handle->isCompletionFinished()))
            {
                pending = true;
                break;
            }
        }

        if (!pending)
        {
            // A completion callback may have just queued the next DataIO
            // operation.  Drain one event turn and re-check the complete set.
            if (QCoreApplication::instance() != nullptr)
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

            bool       has_live_handle = false;
            for (const auto &handle : operation_handles_)
            {
                if (handle != nullptr && (!handle->isFinished() || !handle->isCompletionFinished()))
                {
                    has_live_handle = true;
                    break;
                }
            }
            if ((import_service_ == nullptr || !import_service_->importRunning()) && !data_operation_running_
                && !has_live_handle)
                return;
            pending = true;
        }

        if (QCoreApplication::instance() != nullptr)
        {
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
        else
            QThread::yieldCurrentThread();
    }
}

DataOperationWorkflow::HandlePtr DataManager::trackOperation(DataOperationWorkflow::HandlePtr handle)
{
    if (handle == nullptr || shutting_down_)
        return {};

    operation_handles_.erase(
        std::remove_if(operation_handles_.begin(), operation_handles_.end(),
                       [](const DataOperationWorkflow::HandlePtr &candidate)
                       {
                           return candidate == nullptr || (candidate->isFinished() && candidate->isCompletionFinished());
                       }),
        operation_handles_.end());
    operation_handles_.push_back(handle);
    return handle;
}

void DataManager::waitForDataIoOperations(const QList<QPointer<DataIO>> &operations)
{
    for (;;)
    {
        bool pending = false;
        for (const QPointer<DataIO> &io : operations)
        {
            if (io != nullptr && !io->waitForDone(0))
            {
                pending = true;
                break;
            }
        }

        for (const auto &handle : operation_handles_)
        {
            if (handle != nullptr && !handle->isFinished())
            {
                pending = true;
                break;
            }
        }
        if (!pending)
            return;

        if (QCoreApplication::instance() != nullptr)
        {
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
        else
            QThread::yieldCurrentThread();
    }
}

void DataManager::init(const int method)
{
    datasets_      = new DatasetsListModel(database_, this);
    image_source_  = new ImageInstancesListModel(database_, this);
    label_classes_ = new LabelClassesListModel(database_, this);
    label_source_  = new LabelInstancesListModel(database_, image_source_, label_classes_,
                                                 data::createLabelDataHelper(method), false, this);

    // 筛选器只保存条件；可见图像和标注由代理模型负责。
    global_filter_ = new GlobalFilter(this, this);

    image_instances_    = new ImageInstancesViewModel(image_source_, global_filter_, this);
    label_instances_    = new LabelInstancesViewModel(label_source_, global_filter_, this);
    image_labels_list_  = new ImageLabelsListModel(image_instances_, label_source_, label_classes_, this);
    image_labels_table_ = new ImageLabelsTableModel(image_instances_, label_source_, label_classes_, this);
    image_tags_
        = new ImageTagsListModel(database_, image_source_, image_instances_, label_source_, image_labels_list_, this);
    shortcut_manager_ = new ShortcutManager(label_classes_, image_tags_, this);
    label_classes_->setShortcutManager(shortcut_manager_);
    selected_labels_info_ = new SelectedLabelsInfoModel(this, label_instances_, this);
    image_info_           = new ImageInfoListModel(datasets_, image_instances_, label_classes_, label_source_, this);

    // Create filter items models
    dataset_filter_items_     = new DatasetFilterItemsModel(this);
    image_tag_filter_items_   = new TagFilterItemsModel(TagFilterItemsModel::Target::Image, this);
    label_tag_filter_items_   = new TagFilterItemsModel(TagFilterItemsModel::Target::Label, this);
    label_class_filter_items_ = new LabelClassFilterItemsModel(this);
    custom_filter_items_      = new CustomFilterItemsModel(this);

    // Create CategoryStatisticsModel
    category_statistics_model_
        = new CategoryStatisticsModel(label_source_, label_classes_, image_source_, this, global_filter_);

    // Populate filter items models from datasets and tags
    dataset_filter_items_->populateFromDatasets(datasets_);
    image_tag_filter_items_->populateFromTags(image_tags_);
    label_tag_filter_items_->populateFromTags(image_tags_);
    label_class_filter_items_->populateFromLabelClasses(label_classes_);
    custom_filter_items_->populateFromCustomConditions();

    // 用例服务共享上下文：模型就绪后装配。
    export_service_            = std::make_unique<DataExportService>(makeServices());
    import_service_            = std::make_unique<DataImportService>(makeServices());
    image_transfer_service_    = std::make_unique<ImageTransferService>(makeServices());
    split_service_             = std::make_unique<DatasetSplitService>(makeServices());
    cluster_writeback_service_ = std::make_unique<ClusterWritebackService>(makeServices());

    connect(global_filter_, &GlobalFilter::customFilterSearchResultsChanged, this,
            [this](bool has_image_search_results, bool has_label_search_results)
            { custom_filter_items_->setSearchResultsAvailable(has_image_search_results, has_label_search_results); });
    connect(global_filter_, &GlobalFilter::regionSearchResultsChanged, this,
            [this]()
            {
                if (custom_filter_items_ != nullptr && global_filter_ != nullptr)
                {
                    custom_filter_items_->setRegionSearchResultAvailable(global_filter_->regionResultsReady());
                }
            });

    // Connect source model changes to refresh filter items models
    connect(datasets_, &QAbstractItemModel::rowsInserted, this,
            [this](const QModelIndex &, int, int) { dataset_filter_items_->populateFromDatasets(datasets_); });
    connect(datasets_, &QAbstractItemModel::rowsRemoved, this,
            [this](const QModelIndex &, int, int) { dataset_filter_items_->populateFromDatasets(datasets_); });
    connect(datasets_, &QAbstractItemModel::modelReset, this,
            [this]() { dataset_filter_items_->populateFromDatasets(datasets_); });

    auto repopulate_tags = [this]() {
        if (image_tag_filter_items_ != nullptr && label_tag_filter_items_ != nullptr && image_tags_ != nullptr)
        {
            image_tag_filter_items_->populateFromTags(image_tags_);
            label_tag_filter_items_->populateFromTags(image_tags_);
        }
    };
    connect(image_tags_, &ImageTagsListModel::tagRelationsChanged, this, repopulate_tags);
    connect(image_tags_, &QAbstractItemModel::rowsInserted, this,
            [repopulate_tags](const QModelIndex &, int, int) { repopulate_tags(); });
    connect(image_tags_, &QAbstractItemModel::rowsRemoved, this,
            [repopulate_tags](const QModelIndex &, int, int) { repopulate_tags(); });
    connect(image_tags_, &QAbstractItemModel::modelReset, this, repopulate_tags);

    connect(label_classes_, &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex &, int, int)
            { label_class_filter_items_->populateFromLabelClasses(label_classes_); });
    connect(label_classes_, &QAbstractItemModel::rowsRemoved, this, [this](const QModelIndex &, int, int)
            { label_class_filter_items_->populateFromLabelClasses(label_classes_); });
    connect(label_classes_, &QAbstractItemModel::modelReset, this,
            [this]() { label_class_filter_items_->populateFromLabelClasses(label_classes_); });

    connect(image_instances_, &ImageInstancesViewModel::currentImageChanged, image_labels_list_,
            &ImageLabelsListModel::onCurrentImageChanged);
    connect(image_instances_, &ImageInstancesViewModel::currentImageChanged, image_labels_table_,
            &ImageLabelsTableModel::onCurrentImageChanged);
    connect(image_instances_, &ImageInstancesViewModel::currentImageChanged, image_info_,
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

    image_tags_->applyTagsToImages();
    datasets_->rebuildImageStats(image_source_);

    startAsyncLabelLoading();
}

void DataManager::startAsyncLabelLoading()
{
    if (shutting_down_ || database_ == nullptr || labels_loading_)
    {
        return;
    }

    labels_loading_                 = true;
    labels_changed_during_loading_  = false;
    const QString database_path     = database_->path();
    const int     label_data_method = method_;
    auto          loaded_labels     = std::make_shared<std::vector<LoadedLabelInstance>>();

    DataOperationWorkflow::Options options;
    options.manage_progress = false;
    trackOperation(DataOperationWorkflow::startDatabase(
        this, database_path, std::move(options),
        [loaded_labels, label_data_method](dltool::database::ProjectDataBase &database,
                                           DataOperationWorkflow::Result     &result)
        {
            std::vector<int64_t>              label_ids;
            std::vector<int64_t>              image_ids;
            std::vector<int64_t>              label_class_ids;
            std::vector<int64_t>              label_types;
            std::vector<std::vector<uint8_t>> labels_data;

            result.success
                = database.getAllLabels(label_ids, image_ids, label_class_ids, label_types, labels_data, result.error);
            if (!result.success)
            {
                return;
            }

            LabelDataHelper helper = data::createLabelDataHelper(label_data_method);
            if (helper == nullptr)
            {
                result.error   = QString("标签数据工厂未初始化");
                result.success = false;
                return;
            }

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
        },
        [this, loaded_labels](const DataOperationWorkflow::Result &result)
        { commitLabelsLoaded(loaded_labels, result.success, result.error, result.elapsed_ms); }));
}

void DataManager::commitLabelsLoaded(std::shared_ptr<std::vector<LoadedLabelInstance>> labels, bool success,
                                     const QString &err_msg, qint64 elapsed_ms)
{
    if (shutting_down_)
        return;

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

    label_source_->replaceAllLabels(std::move(*labels));
    if (image_tags_ != nullptr)
    {
        image_tags_->applyTagsToLabels();
    }
    rebuildLabelRelations();
    if (global_filter_ != nullptr)
    {
        global_filter_->refresh();
    }

    spdlog::info("后台加载项目标注完成: {} 个标注, 耗时 {} ms", label_source_->totalCount(), elapsed_ms);
}

void DataManager::rebuildLabelRelations(const bool notify_image_model)
{
    if (image_source_ == nullptr || label_source_ == nullptr || datasets_ == nullptr)
    {
        return;
    }

    image_source_->syncAllLabelRelations(label_source_, notify_image_model);
    datasets_->rebuildImageStats(image_source_);

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

bool DataManager::ensureDataset(const QString &name, int64_t &dataset_id, QString &err_msg)
{
    dataset_id = -1;
    err_msg.clear();

    if (shutting_down_)
    {
        err_msg = QStringLiteral("数据管理器正在关闭");
        return false;
    }
    if (database_ == nullptr || datasets_ == nullptr)
    {
        err_msg = QStringLiteral("数据管理器未初始化");
        return false;
    }

    dataset_id = getDatasetId(name);
    if (dataset_id >= 0)
    {
        return true;
    }

    const QString validation_error = isValidDatasetName(name);
    if (!validation_error.isEmpty())
    {
        err_msg = validation_error;
        return false;
    }
    if (isDataOperationRunning())
    {
        err_msg = QStringLiteral("当前已有数据操作正在进行中");
        return false;
    }

    setDataOperationRunning(true);
    const bool added = datasets_->addDataset(name);
    setDataOperationRunning(false);
    if (!added)
    {
        err_msg = QStringLiteral("写入数据集失败: %1").arg(name);
        return false;
    }

    dataset_id = getDatasetId(name);
    if (dataset_id < 0)
    {
        err_msg = QStringLiteral("写入数据集后无法读取 ID: %1").arg(name);
        return false;
    }
    return true;
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
    std::vector<int64_t> image_ids;
    if (image_source_ == nullptr)
    {
        return image_ids;
    }

    const auto &images = image_source_->getAllImageInstances();
    image_ids.reserve(images.size());
    for (const auto &[image_id, _] : images)
    {
        image_ids.push_back(image_id);
    }
    return image_ids;
}

std::vector<int64_t> DataManager::imageIdsForDatasets(const std::vector<int64_t> &dataset_ids) const
{
    return image_source_ ? image_source_->getImageIdsForDatasets(dataset_ids) : std::vector<int64_t>{};
}

QString DataManager::imagePath(int64_t image_id) const
{
    return image_source_ ? image_source_->getImagePath(image_id) : QString();
}

int64_t DataManager::imageDatasetId(int64_t image_id) const
{
    return image_source_ ? image_source_->getImageDatasetId(image_id) : -1;
}

int64_t DataManager::imageLabelClassId(int64_t image_id) const
{
    return image_source_ ? image_source_->getImageLabelClassId(image_id) : -1;
}

std::vector<int64_t> DataManager::allLabelIds() const
{
    std::vector<int64_t> label_ids;
    if (label_source_ == nullptr)
    {
        return label_ids;
    }

    const auto &instances = label_source_->getAllLabelInstances();
    label_ids.reserve(instances.size());
    for (const auto &[label_id, _] : instances)
    {
        label_ids.push_back(label_id);
    }
    return label_ids;
}

int64_t DataManager::labelImageId(int64_t label_id) const
{
    return label_source_ ? label_source_->getImageId(label_id) : -1;
}

int64_t DataManager::labelClassId(int64_t label_id) const
{
    return label_source_ ? label_source_->getLabelClassId(label_id) : -1;
}

QVariantMap DataManager::labelData(int64_t label_id) const
{
    if (label_source_ == nullptr)
    {
        return {};
    }

    const LabelInstance *instance = label_source_->getLabelInstance(label_id);
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

QString DataManager::labelClassColor(int64_t label_class_id) const
{
    return label_classes_ ? label_classes_->getLabelClassColor(static_cast<int>(label_class_id)) : QString();
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
    return label_source_ ? label_source_->getImageLabelIds(image_id) : std::vector<int64_t>{};
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

void DataManager::clearRegionSearchResults()
{
    if (global_filter_ != nullptr)
    {
        global_filter_->clearRegionSearchResults();
    }
}

void DataManager::setRegionSearchResults(const std::vector<int64_t> &label_ids, bool enable_filter)
{
    if (global_filter_ != nullptr)
    {
        global_filter_->setRegionSearchResults(label_ids, enable_filter);
    }
}

bool DataManager::regionResultsReady() const
{
    return global_filter_ != nullptr && global_filter_->regionResultsReady();
}

QString DataManager::getDatasetName(const int dataset_id) const
{
    return datasets_->getDatasetName(dataset_id);
}

void DataManager::addDataset(const QString &name)
{
    if (shutting_down_)
        return;

    const QString validation_error = isValidDatasetName(name);
    if (!validation_error.isEmpty())
    {
        spdlog::warn("添加数据集失败: {}", validation_error.toUtf8().constData());
        return;
    }
    if (database_ == nullptr || datasets_ == nullptr)
    {
        return;
    }
    if (isDataOperationRunning())
    {
        ui::SignalHelper::notifyWarn(QString("添加数据集"), QString("当前已有数据操作正在进行中"));
        return;
    }

    setDataOperationRunning(true);
    auto                           dataset_id = std::make_shared<int64_t>(-1);
    DataOperationWorkflow::Options options;
    options.title           = QString("添加数据集");
    options.start_message   = QString("正在添加数据集: %1").arg(name);
    options.manage_progress = false;
    trackOperation(DataOperationWorkflow::startDatabase(
        this, database_->path(), std::move(options),
        [name, dataset_id](dltool::database::ProjectDataBase &database, DataOperationWorkflow::Result &result)
        { result.success = database.addDataset(name, *dataset_id, result.error); },
        [this, name, dataset_id](const DataOperationWorkflow::Result &result)
        {
            if (result.success)
            {
                datasets_->addDatasetFromMemory(*dataset_id, name);
                const QString message = QString("已添加数据集: %1，耗时 %2 ms").arg(name).arg(result.elapsed_ms);
                spdlog::info("{}", message.toUtf8().constData());
            }
            else
            {
                const QString message = QString("添加数据集失败: %1").arg(result.error);
                spdlog::error("{}", message.toUtf8().constData());
                ui::SignalHelper::notifyError(QString("添加数据集失败"), message);
            }
            setDataOperationRunning(false);
        }));
}

void DataManager::updateDataset(const int64_t dataset_id, const QString &name)
{
    if (shutting_down_)
        return;

    const QString validation_error = isValidDatasetName(name, dataset_id);
    if (!validation_error.isEmpty())
    {
        spdlog::warn("更新数据集失败: {}", validation_error.toUtf8().constData());
        return;
    }
    if (database_ == nullptr || datasets_ == nullptr)
    {
        return;
    }
    if (isDataOperationRunning())
    {
        ui::SignalHelper::notifyWarn(QString("更新数据集"), QString("当前已有数据操作正在进行中"));
        return;
    }

    setDataOperationRunning(true);
    DataOperationWorkflow::Options options;
    options.title           = QString("更新数据集");
    options.start_message   = QString("正在更新数据集: %1").arg(name);
    options.manage_progress = false;
    trackOperation(DataOperationWorkflow::startDatabase(
        this, database_->path(), std::move(options),
        [dataset_id, name](dltool::database::ProjectDataBase &database, DataOperationWorkflow::Result &result)
        { result.success = database.updateDataset(dataset_id, name, result.error); },
        [this, dataset_id, name](const DataOperationWorkflow::Result &result)
        {
            if (result.success)
            {
                datasets_->updateDatasetFromMemory(dataset_id, name);
                const QString message = QString("已更新数据集: %1，耗时 %2 ms").arg(name).arg(result.elapsed_ms);
                spdlog::info("{}", message.toUtf8().constData());
            }
            else
            {
                const QString message = QString("更新数据集失败: %1").arg(result.error);
                spdlog::error("{}", message.toUtf8().constData());
                ui::SignalHelper::notifyError(QString("更新数据集失败"), message);
            }
            setDataOperationRunning(false);
        }));
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

QString DataManager::isValidTagName(const QString &name, const int64_t tag_id) const
{
    if (image_tags_ == nullptr)
    {
        return QString("error:Tag 模型不可用");
    }

    const QString normalized_name = name.trimmed();
    const QString name_error      = isValidName(normalized_name);
    if (!name_error.isEmpty())
    {
        return name_error;
    }

    const int64_t existing_tag_id = findTagClassId(normalized_name);
    if (existing_tag_id >= 0 && existing_tag_id != tag_id)
    {
        return QString("error:Tag 名称已存在");
    }
    return QString();
}

QString DataManager::isValidTag(const QString &name, const QString &shortcut, const int64_t tag_id) const
{
    const QString name_error = isValidTagName(name, tag_id);
    if (!name_error.isEmpty())
    {
        return name_error;
    }
    return shortcut_manager_ ? shortcut_manager_->validateTagShortcut(shortcut, tag_id)
                             : QString("error:快捷键管理器不可用");
}

void DataManager::deleteDatasets(const std::vector<int64_t> &dataset_ids)
{
    if (shutting_down_)
        return;

    if (isDataOperationRunning())
    {
        ui::SignalHelper::notifyWarn(QString("删除数据集"), QString("数据集删除任务正在进行中"));
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
    target_dataset_ids.erase(std::unique(target_dataset_ids.begin(), target_dataset_ids.end()),
                             target_dataset_ids.end());
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

    setDataOperationRunning(true);
    dataset_deletion_running_ = true;
    emit datasetDeletionRunningChanged();

    DataOperationWorkflow::Options options;
    options.title         = QString("删除数据集");
    options.start_message = QString("正在删除 %1 个数据集及其图像、标注和标签").arg(target_dataset_ids.size());
    trackOperation(DataOperationWorkflow::startDatabase(
        this, database_->path(), std::move(options),
        [target_dataset_ids](dltool::database::ProjectDataBase &database, DataOperationWorkflow::Result &result)
        { result.success = database.deleteDatasetsWithContents(target_dataset_ids, result.error); },
        [this, target_dataset_ids](const DataOperationWorkflow::Result &result)
        { commitDatasetDeletion(target_dataset_ids, result.success, result.error, result.elapsed_ms); }));
}

void DataManager::commitDatasetDeletion(const std::vector<int64_t> &dataset_ids, const bool success,
                                        const QString &err_msg, const qint64 elapsed_ms)
{
    if (shutting_down_)
        return;

    if (success)
    {
        // Only QAbstractItemModel state is touched on this thread.  The database has
        // already committed, so these helpers deliberately perform no database writes.
        const std::vector<int64_t> image_ids
            = image_source_ != nullptr ? image_source_->getImageIdsForDatasets(dataset_ids) : std::vector<int64_t>{};
        image_instances_->beginBulkUpdate();
        label_instances_->beginBulkUpdate();
        if (label_source_ != nullptr)
        {
            label_source_->removeLabelsForImagesFromMemory(image_ids);
        }
        if (image_tags_ != nullptr)
        {
            image_tags_->removeImagesTagsFromMemory(image_ids);
        }
        if (image_source_ != nullptr)
        {
            image_source_->removeImagesFromMemory(image_ids);
        }
        if (datasets_ != nullptr)
        {
            datasets_->removeDatasetsFromMemory(dataset_ids);
        }
        if (global_filter_ != nullptr)
        {
            global_filter_->refresh();
        }
        label_instances_->endBulkUpdate();
        image_instances_->endBulkUpdate();

        const QString message = QString("已删除 %1 个数据集，耗时 %2 ms").arg(dataset_ids.size()).arg(elapsed_ms);
        spdlog::info("{}", message.toUtf8().constData());
        ui::SignalHelper::notifySuccess(QString("删除数据集完成"), message);
    }
    else
    {
        const QString message = QString("删除数据集失败: %1").arg(err_msg);
        spdlog::error("{}", message.toUtf8().constData());
        ui::SignalHelper::notifyError(QString("删除数据集失败"), message);
    }

    dataset_deletion_running_ = false;
    setDataOperationRunning(false);
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
    if (import_service_)
        import_service_->importData(dataset_id, data_format, image_dir, data_dir, {});
}

void DataManager::scanImportLabelClasses(const int data_format, const QString &image_dir, const QString &data_dir)
{
    if (import_service_)
        import_service_->scanImportLabelClasses(data_format, image_dir, data_dir);
}

void DataManager::importDataWithLabelClassGroups(const int64_t dataset_id, const int data_format,
                                                 const QString &image_dir, const QString &data_dir,
                                                 const QVariantMap &label_class_groups)
{
    if (import_service_)
        import_service_->importData(dataset_id, data_format, image_dir, data_dir,
                                    parseLabelClassGroupMap(label_class_groups));
}

void DataManager::exportDatasets(const std::vector<int64_t> &dataset_ids, const int data_format,
                                 const QString &output_dir, const QVariantMap &options)
{
    if (export_service_)
        export_service_->exportDatasets(dataset_ids, data_format, output_dir, options);
}


void DataManager::copyToDataset(const std::vector<int64_t> &image_ids, const int64_t dataset_id)
{
    copyToDatasetAsync(image_ids, dataset_id, nullptr, {}, true);
}

bool DataManager::copyToDatasetAsync(const std::vector<int64_t> &image_ids, const int64_t dataset_id,
                                     QObject *callback_context, ImageOperationCompletion completion,
                                     const bool notify_user)
{
    if (image_transfer_service_)
        return image_transfer_service_->copyToDatasetAsync(image_ids, dataset_id, callback_context,
                                                           std::move(completion), notify_user);
    return false;
}

bool DataManager::moveToDatasetAsync(const std::vector<int64_t> &image_ids, const int64_t dataset_id,
                                     QObject *callback_context, ImageOperationCompletion completion,
                                     const bool notify_user)
{
    if (image_transfer_service_)
        return image_transfer_service_->moveToDatasetAsync(image_ids, dataset_id, callback_context,
                                                           std::move(completion), notify_user);
    return false;
}

void DataManager::deleteSelectedImages()
{
    if (image_transfer_service_)
        image_transfer_service_->deleteSelectedImages();
}



void DataManager::moveToDataset(const std::vector<int64_t> &image_ids, const int64_t dataset_id)
{
    moveToDatasetAsync(image_ids, dataset_id, nullptr, {}, true);
}



void DataManager::splitDataset(const int64_t dataset_id, const double train_ratio, const double validation_ratio,
                               const double test_ratio, const bool use_validation)
{
    if (split_service_)
        split_service_->splitDataset(dataset_id, train_ratio, validation_ratio, test_ratio, use_validation);
}

bool DataManager::writebackClusterAsync(const ClusterWritebackRequest &request, QObject *callback_context,
                                        ClusterWritebackCompletion completion)
{
    if (cluster_writeback_service_)
        return cluster_writeback_service_->writebackClusterAsync(request, callback_context, std::move(completion));
    return false;
}

void DataManager::addLabelClass(const QString &name, const QString &color, const QString &shortcut)
{
    addLabelClassWithGroup(name, color, shortcut, defaultLabelClassGroup());
}

void DataManager::addLabelClassWithGroup(const QString &name, const QString &color, const QString &shortcut,
                                         const QString &group)
{
    const QString normalized_shortcut = ShortcutManager::normalizedShortcut(shortcut);
    if (isDataOperationRunning())
    {
        ui::SignalHelper::notifyWarn(QString("添加标签类别"), QString("当前已有数据操作正在进行中"));
        return;
    }
    const QString validation_error = isValidClassName(name);
    if (!validation_error.isEmpty())
    {
        spdlog::warn("添加标签类别失败: {}", validation_error.toUtf8().constData());
        return;
    }
    if (label_classes_ != nullptr)
    {
        const QString label_error = label_classes_->isValid(-1, name, color, normalized_shortcut, -1);
        if (label_error.startsWith(QStringLiteral("error:")))
        {
            spdlog::warn("添加标签类别失败: {}", label_error.toUtf8().constData());
            return;
        }
    }
    label_classes_->addLabelClass(name, color, normalized_shortcut, group);
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
    const QString normalized_shortcut = ShortcutManager::normalizedShortcut(shortcut);
    if (isDataOperationRunning())
    {
        ui::SignalHelper::notifyWarn(QString("更新标签类别"), QString("当前已有数据操作正在进行中"));
        return;
    }
    const QString validation_error = isValidClassName(name, label_class_id);
    if (!validation_error.isEmpty())
    {
        spdlog::warn("更新标签类别失败: {}", validation_error.toUtf8().constData());
        return;
    }
    if (label_classes_ != nullptr)
    {
        const QString label_error = label_classes_->isValid(static_cast<int>(label_class_id), name, color,
                                                            normalized_shortcut, static_cast<int>(ordinal_index));
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
    label_classes_->updateLabelClass(label_class_id, name, color, normalized_shortcut, ordinal_index, group);
    image_labels_list_->labelClassUpdated(label_class_id);
    image_labels_table_->labelClassUpdated(label_class_id);
    image_info_->updateLabelInfo();
}

void DataManager::updateLabelClassGroup(const int64_t label_class_id, const QString &group)
{
    if (isDataOperationRunning())
    {
        ui::SignalHelper::notifyWarn(QString("更新标签类别分组"), QString("当前已有数据操作正在进行中"));
        return;
    }
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
    if (isDataOperationRunning())
    {
        ui::SignalHelper::notifyWarn(QString("删除标签类别"), QString("当前已有数据操作正在进行中"));
        return;
    }
    std::vector<int64_t> label_ids = label_source_->getLabelIds(label_class_id);
    deleteLabels(label_ids);

    std::vector<int64_t> images_to_clear;
    std::vector<int64_t> clear_values;
    if (image_source_ != nullptr)
    {
        for (const auto &[image_id, image] : image_source_->getAllImageInstances())
        {
            if (image != nullptr && image->imageLabelClassId() == label_class_id)
            {
                images_to_clear.push_back(image_id);
                clear_values.push_back(-1);
            }
        }
        if (!images_to_clear.empty())
        {
            image_source_->setImageLabelClassIds(images_to_clear, clear_values);
            datasets_->syncImageLabelState(image_source_, images_to_clear);
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
    if (isDataOperationRunning())
    {
        ui::SignalHelper::notifyWarn(QString("添加标注"), QString("当前已有数据操作正在进行中"));
        return;
    }
    addLabelsInternal(image_ids, label_class_ids, data);
}

bool DataManager::addLabelsWithIds(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_class_ids,
                                   const std::vector<QVariantMap> &data, std::vector<int64_t> *added_label_ids,
                                   QString *err_msg)
{
    if (isDataOperationRunning())
    {
        if (err_msg != nullptr)
        {
            *err_msg = QStringLiteral("当前已有数据操作正在进行中");
        }
        ui::SignalHelper::notifyWarn(QString("添加标注"), QString("当前已有数据操作正在进行中"));
        return false;
    }
    return addLabelsInternal(image_ids, label_class_ids, data, err_msg, true, added_label_ids);
}

bool DataManager::addLabel(const int64_t image_id, const int64_t label_class_id, const QVariantMap &data)
{
    if (isDataOperationRunning())
    {
        ui::SignalHelper::notifyWarn(QString("添加标注"), QString("当前已有数据操作正在进行中"));
        return false;
    }
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
    if (!label_source_->tryAddLabels(label_ids, image_ids, label_class_ids, data, err_msg, !refresh_dependent_models))
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

    image_source_->addImagesLabelIds(image_ids, label_ids);
    image_labels_list_->addLabels(image_ids, label_ids);
    image_labels_table_->addLabels(image_ids, label_ids);
    datasets_->syncImageLabelState(image_source_, image_ids);
    image_info_->updateLabelInfo();
    return true;
}

void DataManager::updateLabels(const std::vector<int64_t> &label_ids, const std::vector<QVariantMap> &data)
{
    if (isDataOperationRunning())
    {
        ui::SignalHelper::notifyWarn(QString("更新标注"), QString("当前已有数据操作正在进行中"));
        return;
    }
    if (labels_loading_)
    {
        labels_changed_during_loading_ = true;
    }
    std::vector<int64_t> image_ids = label_source_->getImageIds(label_ids);
    label_source_->updateLabelsData(label_ids, image_ids, data);
    image_labels_list_->updateLabels(image_ids, label_ids);
    image_labels_table_->updateLabels(image_ids, label_ids);
}

void DataManager::updateLabelsClass(const std::vector<int64_t> &label_ids, const std::vector<int64_t> &label_class_ids)
{
    if (isDataOperationRunning())
    {
        ui::SignalHelper::notifyWarn(QString("更新标注类别"), QString("当前已有数据操作正在进行中"));
        return;
    }
    if (labels_loading_)
    {
        labels_changed_during_loading_ = true;
    }
    std::vector<int64_t> image_ids = label_source_->getImageIds(label_ids);
    label_source_->updateLabelsClass(label_ids, label_class_ids);
    image_labels_list_->updateLabels(image_ids, label_ids);
    image_labels_table_->updateLabels(image_ids, label_ids);
}

void DataManager::deleteLabels(const std::vector<int64_t> &label_ids)
{
    if (isDataOperationRunning())
    {
        ui::SignalHelper::notifyWarn(QString("删除标注"), QString("当前已有数据操作正在进行中"));
        return;
    }
    if (labels_loading_)
    {
        labels_changed_during_loading_ = true;
    }
    std::vector<int64_t> image_ids = label_source_->getImageIds(label_ids);
    label_source_->deleteLabels(label_ids);
    image_source_->deleteImagesLabelIds(image_ids, label_ids);
    image_labels_list_->deleteLabels(image_ids, label_ids);
    image_labels_table_->deleteLabels(image_ids, label_ids);
    datasets_->syncImageLabelState(image_source_, image_ids);
    image_info_->updateLabelInfo();
    if (global_filter_ != nullptr)
    {
        global_filter_->refresh();
    }
}

void DataManager::duplicateSelectedLabels()
{
    if (isDataOperationRunning())
    {
        ui::SignalHelper::notifyWarn(QString("复制标注"), QString("当前已有数据操作正在进行中"));
        return;
    }
    if (image_labels_list_ == nullptr || label_source_ == nullptr || image_instances_ == nullptr)
    {
        return;
    }

    const std::vector<int64_t> selected_label_ids = image_labels_list_->getSelectedLabelIds();
    if (selected_label_ids.empty())
    {
        return;
    }

    const int64_t                          current_image_id = image_instances_->currentImageId();
    std::vector<int64_t>                   image_ids;
    std::vector<int64_t>                   label_class_ids;
    std::vector<QVariantMap>               labels_data;
    std::vector<const std::set<int64_t> *> label_tag_sets;
    image_ids.reserve(selected_label_ids.size());
    label_class_ids.reserve(selected_label_ids.size());
    labels_data.reserve(selected_label_ids.size());
    label_tag_sets.reserve(selected_label_ids.size());

    for (const int64_t label_id : selected_label_ids)
    {
        LabelInstance *instance = label_source_->getLabelInstance(label_id);
        if (instance == nullptr || instance->imageId() != current_image_id || instance->data() == nullptr)
        {
            continue;
        }

        QVariantMap data = instance->data()->dataMap();
        image_ids.push_back(current_image_id);
        label_class_ids.push_back(instance->labelClassId());
        labels_data.push_back(data);
        label_tag_sets.push_back(&instance->tagIds());
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
        for (size_t i = 0; i < duplicated_label_ids.size() && i < label_tag_sets.size(); ++i)
        {
            if (label_tag_sets[i] == nullptr)
            {
                continue;
            }
            for (const int64_t tag_id : *label_tag_sets[i])
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
    if (isDataOperationRunning())
    {
        ui::SignalHelper::notifyWarn(QString("设置图像类别"), QString("当前已有数据操作正在进行中"));
        return false;
    }
    if (image_source_ == nullptr || label_classes_ == nullptr)
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

    const bool ok = image_source_->setImageLabelClassId(image_id, effective_label_class_id);
    if (ok)
    {
        datasets_->syncImageLabelState(image_source_, {image_id});
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
    if (image_source_ == nullptr || label_classes_ == nullptr || image_id < 0)
    {
        return data;
    }

    const int64_t label_class_id = image_source_->getImageLabelClassId(image_id);
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
    if (isDataOperationRunning())
    {
        ui::SignalHelper::notifyWarn(QString("刷新图像类别"), QString("当前已有数据操作正在进行中"));
        return;
    }
    if (method_ != core::DeepLearningMethod::AnomalyDetection || image_source_ == nullptr || label_source_ == nullptr
        || label_classes_ == nullptr)
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
        if (only_unset && image_source_->getImageLabelClassId(image_id) >= 0)
        {
            continue;
        }

        int64_t first_class_id         = -1;
        int64_t first_anomaly_class_id = -1;
        for (const int64_t label_id : label_source_->getImageLabelIds(image_id))
        {
            const int64_t label_class_id = label_source_->getLabelClassId(label_id);
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
        if (image_source_->getImageLabelClassId(image_id) != target_class_id)
        {
            images_to_update.push_back(image_id);
            classes_to_set.push_back(target_class_id);
        }
    }

    if (!images_to_update.empty() && image_source_->setImageLabelClassIds(images_to_update, classes_to_set))
    {
        datasets_->syncImageLabelState(image_source_, images_to_update);
        if (image_info_ != nullptr)
        {
            image_info_->updateLabelInfo();
        }
    }
}

void DataManager::addTagClass(const QString &name, const QString &shortcut)
{
    const QString normalized_name     = name.trimmed();
    const QString normalized_shortcut = ShortcutManager::normalizedShortcut(shortcut);
    const QString validation_error    = isValidTag(normalized_name, normalized_shortcut);
    if (!validation_error.isEmpty())
    {
        spdlog::warn("添加 Tag 失败: {}", validation_error.toUtf8().constData());
        return;
    }
    if (isDataOperationRunning())
    {
        ui::SignalHelper::notifyWarn(QString("添加 Tag"), QString("当前已有数据操作正在进行中"));
        return;
    }
    if (image_tags_ == nullptr)
    {
        return;
    }
    image_tags_->addTagClass(normalized_name, normalized_shortcut);
}

bool DataManager::updateTagClass(const int64_t tag_id, const QString &name, const QString &shortcut)
{
    const QString normalized_name     = name.trimmed();
    const QString normalized_shortcut = ShortcutManager::normalizedShortcut(shortcut);
    const QString validation_error    = isValidTag(normalized_name, normalized_shortcut, tag_id);
    if (!validation_error.isEmpty())
    {
        spdlog::warn("更新 Tag 失败: {}", validation_error.toUtf8().constData());
        return false;
    }
    if (isDataOperationRunning())
    {
        ui::SignalHelper::notifyWarn(QString("更新 Tag"), QString("当前已有数据操作正在进行中"));
        return false;
    }
    return image_tags_ != nullptr && image_tags_->updateTagClass(tag_id, normalized_name, normalized_shortcut);
}

int64_t DataManager::findTagClassId(const QString &name) const
{
    return image_tags_ ? image_tags_->findTagClassId(name) : -1;
}

bool DataManager::setLabelsTag(const std::vector<int64_t> &label_ids, const int64_t tag_id)
{
    if (isDataOperationRunning())
    {
        ui::SignalHelper::notifyWarn(QString("设置标注 Tag"), QString("当前已有数据操作正在进行中"));
        return false;
    }
    return image_tags_ != nullptr && image_tags_->addLabelsTag(label_ids, tag_id);
}

std::set<int64_t> DataManager::labelTagIds(const int64_t label_id) const
{
    if (label_source_ == nullptr)
    {
        return {};
    }
    const auto *instance = label_source_->getLabelInstance(label_id);
    return instance != nullptr ? instance->tagIds() : std::set<int64_t>{};
}

bool DataManager::deleteTagClass(const int64_t tag_id)
{
    if (isDataOperationRunning())
    {
        ui::SignalHelper::notifyWarn(QString("删除 Tag"), QString("当前已有数据操作正在进行中"));
        return false;
    }
    return image_tags_ != nullptr && image_tags_->deleteTagClass(tag_id);
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

    auto *image_instance_provider = new ImageInstanceImageProvider(image_source_);

    // 创建 LabelInstanceImageProvider 实例，传入三个模型指针
    auto *label_instance_provider = new LabelInstanceImageProvider(label_source_, image_source_, label_classes_);

    // 注册到 QML 引擎（使用小写名称，因为 QML Image 会自动转换为小写）
    engine->addImageProvider("imageinstance", image_instance_provider);
    engine->addImageProvider("labelinstance", label_instance_provider);
}

QString DataManager::getImageName(const int64_t image_id) const
{
    return image_source_->getImageName(image_id);
}

QString DataManager::getImagePath(const int64_t image_id) const
{
    return image_source_->getImagePath(image_id);
}

QSize DataManager::imageSize(const int64_t image_id) const
{
    return image_source_ != nullptr ? image_source_->imageSize(image_id) : QSize();
}

QHash<int64_t, QSize> DataManager::imageDimensionsSnapshot() const
{
    return image_source_ != nullptr ? image_source_->cachedImageSizes() : QHash<int64_t, QSize>();
}

QString DataManager::getImageDatasetName(const int64_t image_id) const
{
    const int64_t dataset_id = image_source_->getImageDatasetId(image_id);
    return datasets_->getDatasetName(dataset_id);
}

QString DataManager::getImageTagName(const int64_t image_id) const
{
    const std::set<int64_t> &tag_ids = image_source_->getImageTagIds(image_id);
    if (tag_ids.empty())
        return QString();
    QString tag_names;
    for (int64_t tag_id : tag_ids)
    {
        tag_names.append(image_tags_->getTagClassName(tag_id) + ";");
    }
    return tag_names;
}

QString DataManager::getLabelTagName(const int64_t label_id) const
{
    if (label_source_ == nullptr || image_tags_ == nullptr)
    {
        return QString();
    }
    const LabelInstance *label = label_source_->getLabelInstance(label_id);
    if (label == nullptr || label->tagIds().empty())
    {
        return QString();
    }
    QString tag_names;
    for (const int64_t tag_id : label->tagIds())
    {
        tag_names.append(image_tags_->getTagClassName(tag_id) + ";");
    }
    return tag_names;
}

} // namespace dltool::data
