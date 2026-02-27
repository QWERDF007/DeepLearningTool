#include "data/DataManager.h"

#include "data/DataBase.h"
#include "data/DataFormat.h"
#include "data/DataImporter.h"
#include "data/LabelData.h"
#include "data/LabelInstanceImageProvider.h"
#include "ui/ProgressManager.h"

#include <spdlog/spdlog.h>

#include <QColor>
#include <QQmlApplicationEngine>
#include <QTimer>

namespace dltool::data {

DataManager::DataManager(const int method, data::ProjectDataBase *database, QObject *parent)
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

    // 延迟注册 ImageProvider，等待 QML 上下文可用
    qInfo() << "DataManager::init - About to schedule QTimer for ImageProvider registration";
    QTimer::singleShot(0, this,
                       [this]()
                       {
                           qInfo() << "DataManager: QTimer fired - Attempting to register ImageProvider";
                           initializeQmlEngine(nullptr);
                       });
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

    // 连接信号 - 使用 QueuedConnection 确保在主线程更新 UI
    // 重构后：使用 dataReady 信号接收完整的处理后数据
    // 进度更新现在由 DataImporter 内部处理，不再需要连接 progressUpdated 信号
    connect(importer, &DataImporter::dataReady, this, &DataManager::handleDataReady, Qt::QueuedConnection);

    // 启动导入
    importer->startImport(dataset_id, image_dir, data_dir);
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

void DataManager::handleDataReady(bool success, int64_t dataset_id, std::vector<QString> image_paths,
                                  std::vector<int64_t> image_widths, std::vector<int64_t> image_heights,
                                  std::map<QString, QString> label_class_info, std::vector<ImportedLabel> labels)
{
    // 获取发送信号的导入器，用于稍后删除
    DataImporter *importer = qobject_cast<DataImporter *>(sender());

    if (!success || image_paths.empty())
    {
        spdlog::error("数据解析失败或没有数据");
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection,
                                  Q_ARG(int, spdlog::level::err), Q_ARG(QString, "数据解析失败"));
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "completeTask", Qt::QueuedConnection);
        if (importer)
        {
            importer->deleteLater();
        }
        return;
    }

    spdlog::info("开始在主线程中导入数据，图像数量: {}", image_paths.size());
    QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection,
                              Q_ARG(int, spdlog::level::info), Q_ARG(QString, "开始导入数据到数据库..."));

    // 重构后：DataManager 只负责数据库操作
    // 所有格式特定的处理逻辑都在导入器中完成

    // 1. 创建缺失的标签类别
    std::map<QString, int64_t> label_class_map;
    int                        created_label_classes  = 0;
    int                        existing_label_classes = 0;

    for (const auto &[label_name, color] : label_class_info)
    {
        // 检查标签类别是否已存在
        int64_t label_class_id = label_classes_->getLabelClassId(label_name);
        if (label_class_id < 0)
        {
            // 创建新的标签类别
            QString shortcut = "";
            addLabelClass(label_name, color, shortcut);
            label_class_id = label_classes_->getLabelClassId(label_name);
            created_label_classes++;
            spdlog::debug("创建新标签类别: {}, ID: {}", label_name.toStdString(), label_class_id);
        }
        else
        {
            existing_label_classes++;
            spdlog::debug("使用已存在的标签类别: {}, ID: {}", label_name.toStdString(), label_class_id);
        }
        label_class_map[label_name] = label_class_id;
    }

    spdlog::info("标签类别处理完成: 总数={}, 新创建={}, 已存在={}", label_class_info.size(), created_label_classes,
                 existing_label_classes);

    // 2. 批量添加图像
    std::vector<int64_t> image_ids;
    if (!image_instances_->addImages(dataset_id, image_paths, image_ids))
    {
        spdlog::error("添加图像失败: dataset_id={}, 图像数量={}", dataset_id, image_paths.size());
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection,
                                  Q_ARG(int, spdlog::level::err), Q_ARG(QString, "添加图像失败"));
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "completeTask", Qt::QueuedConnection);
        if (importer)
        {
            importer->deleteLater();
        }
        return;
    }

    spdlog::info("成功导入 {} 个图像到数据库", image_ids.size());

    // 3. 创建图像路径到 ID 的映射
    std::map<QString, int64_t> image_path_to_id;
    for (size_t i = 0; i < image_paths.size() && i < image_ids.size(); ++i)
    {
        image_path_to_id[image_paths[i]] = image_ids[i];
    }

    // 4. 为每个图像添加标注
    std::vector<int64_t>     all_label_image_ids;
    std::vector<int64_t>     all_label_class_ids;
    std::vector<QVariantMap> all_label_data;
    int                      skipped_labels = 0;

    for (const auto &label : labels)
    {
        // 查找图像 ID
        auto image_it = image_path_to_id.find(label.image_path);
        if (image_it == image_path_to_id.end())
        {
            spdlog::warn("未找到图像路径对应的 ID，跳过标注: {}, 标签类别: {}", label.image_path.toStdString(),
                         label.label_class_name.toStdString());
            skipped_labels++;
            continue;
        }
        int64_t image_id = image_it->second;

        // 查找标签类别 ID
        auto class_it = label_class_map.find(label.label_class_name);
        if (class_it == label_class_map.end())
        {
            spdlog::warn("未找到标签类别，跳过标注: {}, 图像: {}", label.label_class_name.toStdString(),
                         label.image_path.toStdString());
            skipped_labels++;
            continue;
        }
        int64_t label_class_id = class_it->second;

        if (label.data.isEmpty())
        {
            spdlog::warn("跳过空的标注数据: label_class={}, 图像: {}", label.label_class_name.toStdString(),
                         label.image_path.toStdString());
            skipped_labels++;
            continue;
        }

        all_label_image_ids.push_back(image_id);
        all_label_class_ids.push_back(label_class_id);
        all_label_data.push_back(label.data);
    }

    spdlog::info("标注数据准备完成: 总数={}, 有效={}, 跳过={}", labels.size(), all_label_image_ids.size(),
                 skipped_labels);

    // 5. 批量添加标注
    if (!all_label_image_ids.empty())
    {
        addLabels(all_label_image_ids, all_label_class_ids, all_label_data);
        spdlog::info("成功导入 {} 个标注到数据库", all_label_image_ids.size());
    }
    else
    {
        spdlog::info("没有标注需要导入");
    }

    // 6. 更新数据集中的图像
    std::vector<int64_t> dataset_ids(image_ids.size(), dataset_id);
    datasets_->addImages(dataset_ids, image_ids);

    // 7. 更新统计信息
    updateDatasetsStats();

    spdlog::info("数据导入完成: 图像={}, 标注={}, 标签类别={}", image_ids.size(), all_label_image_ids.size(),
                 label_class_info.size());
    QMetaObject::invokeMethod(
        ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection, Q_ARG(int, spdlog::level::info),
        Q_ARG(QString,
              QString("导入完成: %1 个图像, %2 个标注").arg(image_ids.size()).arg(all_label_image_ids.size())));
    QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "completeTask", Qt::QueuedConnection);

    // 清理导入器
    if (importer)
    {
        importer->deleteLater();
    }
}

void DataManager::initializeQmlEngine(QQmlApplicationEngine *engine)
{
    qInfo() << "DataManager::initializeQmlEngine called with engine:" << (void *)engine;

    if (!engine)
    {
        qInfo() << "DataManager::initializeQmlEngine: engine parameter is null, trying to get from QML context";
        // 尝试从 QObject 上下文获取引擎
        QQmlEngine *qmlEngine
            = QQmlEngine::contextForObject(this) ? QQmlEngine::contextForObject(this)->engine() : nullptr;
        if (!qmlEngine)
        {
            qInfo() << "DataManager::initializeQmlEngine: could not get QML engine from context";
            return;
        }
        engine = qobject_cast<QQmlApplicationEngine *>(qmlEngine);
        if (!engine)
        {
            qInfo() << "DataManager::initializeQmlEngine: QML engine is not QQmlApplicationEngine";
            return;
        }
        qInfo() << "DataManager::initializeQmlEngine: got engine from context:" << (void *)engine;
    }

    qInfo() << "Creating LabelInstanceImageProvider with models: label_instances=" << (void *)label_instances_
            << "image_instances=" << (void *)image_instances_ << "label_classes=" << (void *)label_classes_;

    // 创建 LabelInstanceImageProvider 实例，传入三个模型指针
    auto *labelInstanceProvider = new LabelInstanceImageProvider(label_instances_, image_instances_, label_classes_);

    qInfo() << "Registering LabelInstanceImageProvider with name 'labelinstance'";

    // 注册到 QML 引擎（使用小写名称，因为 QML Image 会自动转换为小写）
    engine->addImageProvider("labelinstance", labelInstanceProvider);

    qInfo() << "LabelInstanceImageProvider registered successfully with name 'labelinstance'";
}

} // namespace dltool::data
