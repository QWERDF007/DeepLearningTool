#include "data/DataManager.h"

#include "data/CategoryStatisticsModel.h"
#include "data/DataFormat.h"
#include "data/DataExporter.h"
#include "data/DataImporter.h"
#include "data/DatasetIO.h"
#include "data/GlobalFilter.h"
#include "data/LabelData.h"
#include "data/LabelInstanceImageProvider.h"
#include "database/DataBase.h"
#include "ui/ProgressManager.h"

#include <spdlog/spdlog.h>

#include <QColor>
#include <QFileInfo>
#include <QMetaType>
#include <QQmlApplicationEngine>

#include <algorithm>
#include <cstddef>

namespace dltool::data {

namespace {

bool isFatalDatabaseError(const QString &message)
{
    return message.contains(QStringLiteral("database disk image is malformed"), Qt::CaseInsensitive)
           || message.contains(QStringLiteral("file is not a database"), Qt::CaseInsensitive);
}

} // namespace

struct DataManager::PendingImportTask
{
    DataImporter *importer{nullptr};

    int64_t dataset_id{0};

    std::map<QString, int64_t> label_class_map;
    std::map<QString, int64_t> image_path_to_id;

    size_t total_images{0};
    size_t processed_images{0};
    size_t imported_images{0};
    size_t imported_labels{0};
    size_t failed_batches{0};
    size_t failed_images{0};
    size_t failed_labels{0};
    int    skipped_labels{0};
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
                                                      data::createLabelDataHelper(method), this);
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

    if (import_running_)
    {
        spdlog::warn("导入数据失败, 已有导入任务正在运行");
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "startTask", Qt::QueuedConnection,
                                  Q_ARG(QString, "导入数据"));
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection,
                                  Q_ARG(int, spdlog::level::warn), Q_ARG(QString, "已有导入任务正在运行"));
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "completeTask", Qt::QueuedConnection);
        return;
    }

    // 验证数据格式是否支持
    if (!data::DataFormat::isDataFormatSupported(data_format))
    {
        spdlog::error("导入数据失败, 数据格式不支持: {}", data_format);
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
        const QString message = QStringLiteral("项目数据库检查失败，无法导入数据: %1").arg(db_check_err_msg);
        spdlog::error("{}", message.toStdString());
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection,
                                  Q_ARG(int, spdlog::level::err), Q_ARG(QString, message));
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "completeTask", Qt::QueuedConnection);
        return;
    }

    // 使用工厂函数创建导入器
    // 重构后：DataManager 不再直接实例化具体的导入器类
    // 而是通过工厂函数获取，实现了依赖倒置原则
    DataImporter *importer = DataImporter::createImporter(data_format, database_, this);
    if (!importer)
    {
        spdlog::error("无法为格式 {} 创建导入器", data_format);
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection,
                                  Q_ARG(int, spdlog::level::err), Q_ARG(QString, "不支持的数据格式"));
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "completeTask", Qt::QueuedConnection);
        return;
    }
    importer->setTargetMethod(method_);
    import_running_ = true;
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

    connect(exporter, &DataExporter::exportFinished, this,
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

bool DataManager::addLabelsInternal(const std::vector<int64_t> &image_ids,
                                    const std::vector<int64_t> &label_class_ids,
                                    const std::vector<QVariantMap> &data, QString *err_msg)
{
    std::vector<int64_t> label_ids;
    if (!label_instances_->tryAddLabels(label_ids, image_ids, label_class_ids, data, err_msg))
    {
        return false;
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
    std::vector<int64_t> image_ids = label_instances_->getImageIds(label_ids);
    label_instances_->updateLabelsData(label_ids, image_ids, data);
    image_labels_list_->updateLabels(image_ids, label_ids);
    image_labels_table_->updateLabels(image_ids, label_ids);
    // updateDatasetsStats();
}

void DataManager::updateLabelsClass(const std::vector<int64_t> &label_ids, const std::vector<int64_t> &label_class_ids)
{
    std::vector<int64_t> image_ids = label_instances_->getImageIds(label_ids);
    label_instances_->updateLabelsClass(label_ids, label_class_ids);
    image_labels_list_->updateLabels(image_ids, label_ids);
    image_labels_table_->updateLabels(image_ids, label_ids);
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

void DataManager::handleDataBatchReady(int64_t dataset_id, std::vector<QString> image_paths,
                                       std::vector<int64_t> image_widths, std::vector<int64_t> image_heights,
                                       std::map<QString, QString> label_class_info,
                                       std::vector<ImportedLabel> labels, int64_t processed_images,
                                       int64_t total_images)
{
    Q_UNUSED(image_widths)
    Q_UNUSED(image_heights)

    DataImporter *importer = qobject_cast<DataImporter *>(sender());
    if (!pending_import_task_ || pending_import_task_->importer != importer)
    {
        return;
    }

    PendingImportTask &task = *pending_import_task_;
    task.processed_images = std::max(task.processed_images, static_cast<size_t>(std::max<int64_t>(0, processed_images)));
    task.total_images     = std::max(task.total_images, static_cast<size_t>(std::max<int64_t>(0, total_images)));

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
            task.first_error_message = QStringLiteral("项目数据库已损坏，无法继续导入标注: %1").arg(err_msg);
            if (importer != nullptr)
            {
                importer->requestCancel();
            }
        }

        const QString progress_message
            = task.fatal_error
                  ? task.first_error_message
                  : QStringLiteral("批次写入失败，已跳过当前批次并继续导入后续数据: %1").arg(err_msg);
        spdlog::error("{}", progress_message.toStdString());
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection,
                                  Q_ARG(int, spdlog::level::err), Q_ARG(QString, progress_message));
        return;
    }

    QMetaObject::invokeMethod(
        ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection, Q_ARG(int, spdlog::level::info),
        Q_ARG(QString,
              QString("已写入 %1 个图像, %2 个标注")
                  .arg(task.imported_images)
                  .arg(task.imported_labels)));
}

bool DataManager::writeImportBatch(int64_t dataset_id, const std::vector<QString> &image_paths,
                                   const std::map<QString, QString> &label_class_info,
                                   const std::vector<ImportedLabel> &labels, QString &err_msg)
{
    if (!pending_import_task_)
    {
        err_msg = QStringLiteral("导入任务不存在");
        return false;
    }

    PendingImportTask &task = *pending_import_task_;
    if (dataset_id != task.dataset_id)
    {
        err_msg = QStringLiteral("导入批次的数据集 ID 不一致");
        return false;
    }

    for (const auto &[label_name, color] : label_class_info)
    {
        int64_t label_class_id = label_classes_->getLabelClassId(label_name);
        if (label_class_id < 0)
        {
            addLabelClass(label_name, color, QString());
            label_class_id = label_classes_->getLabelClassId(label_name);
            spdlog::debug("创建新标签类别: {}, ID: {}", label_name.toStdString(), label_class_id);
        }

        if (label_class_id >= 0)
        {
            task.label_class_map[label_name] = label_class_id;
        }
    }

    std::vector<int64_t> image_ids;
    if (!image_paths.empty())
    {
        if (!image_instances_->addImages(task.dataset_id, image_paths, image_ids))
        {
            err_msg = QStringLiteral("添加图像失败，当前批次已跳过。已导入 %1 个图像, %2 个标注")
                          .arg(task.imported_images)
                          .arg(task.imported_labels);
            return false;
        }

        if (image_ids.size() != image_paths.size())
        {
            err_msg = QStringLiteral("添加图像失败，返回的图像 ID 数量不一致");
            return false;
        }

        task.imported_images += image_ids.size();

        std::vector<int64_t> dataset_ids(image_ids.size(), task.dataset_id);
        datasets_->addImages(dataset_ids, image_ids);

        for (size_t i = 0; i < image_paths.size(); ++i)
        {
            task.image_path_to_id[image_paths[i]] = image_ids[i];
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
            spdlog::warn("未找到图像路径对应的 ID，跳过标注: {}, 标签类别: {}", label.image_path.toStdString(),
                         label.label_class_name.toStdString());
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
                class_it = task.label_class_map.find(label.label_class_name);
            }
        }

        if (class_it == task.label_class_map.end())
        {
            spdlog::warn("未找到标签类别，跳过标注: {}, 图像: {}", label.label_class_name.toStdString(),
                         label.image_path.toStdString());
            task.skipped_labels++;
            continue;
        }

        if (label.data.isEmpty())
        {
            spdlog::warn("跳过空的标注数据: label_class={}, 图像: {}", label.label_class_name.toStdString(),
                         label.image_path.toStdString());
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
            err_msg = QStringLiteral("添加标注失败，当前标注批次已跳过。待写入标注 %1 个")
                          .arg(batch_label_image_ids.size());
            if (!label_err_msg.isEmpty())
            {
                err_msg += QStringLiteral("，原因: %1").arg(label_err_msg);
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
            message = QStringLiteral("数据解析失败或没有数据");
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
    QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection,
                              Q_ARG(int, level), Q_ARG(QString, message));
    QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "completeTask", Qt::QueuedConnection);

    if (importer)
    {
        importer->deleteLater();
    }
    pending_import_task_.reset();
    import_running_ = false;
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
