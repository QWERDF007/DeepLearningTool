#include "data/DataManager.h"

#include "data/DataBase.h"
#include "data/DataFormat.h"
#include "data/LabelData.h"
#include "data/LabelMeImporter.h"
#include "ui/ProgressManager.h"

#include <spdlog/spdlog.h>

#include <QColor>

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

    // 根据数据格式调用相应的导入函数
    if (data_format == data::DataFormat::getDataFormat("LabelMe"))
    {
        importLabelMeData(dataset_id, image_dir, data_dir);
        return;
    }

    // 默认导入逻辑（仅导入图像）
    std::vector<int64_t> image_ids;
    image_instances_->addImages(dataset_id, image_dir, image_ids);
    std::vector<int64_t> dataset_ids(image_ids.size(), dataset_id);
    datasets_->addImages(dataset_ids, image_ids);
    updateDatasetsStats();
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

void DataManager::importLabelMeData(const int64_t dataset_id, const QString &image_dir, const QString &data_dir)
{
    // 显示进度对话框
    QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "startTask", Qt::QueuedConnection,
                              Q_ARG(QString, "导入 LabelMe 数据"));

    // 创建导入器
    auto *importer = new LabelMeImporter(database_, this);

    // 连接进度更新信号 - 使用 QueuedConnection 确保在主线程更新 UI
    connect(
        importer, &LabelMeImporter::progressUpdated, this,
        [](int progress, const QString &message)
        {
            QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "updateProgress", Qt::QueuedConnection,
                                      Q_ARG(int, progress));
            QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection,
                                      Q_ARG(int, spdlog::level::info), Q_ARG(QString, message));
        },
        Qt::QueuedConnection);

    // 连接数据解析完成信号 - 在主线程中处理数据
    connect(
        importer, &LabelMeImporter::dataParsed, this,
        [this, importer](bool success, int64_t dataset_id, std::vector<LabelMeImporter::LabelMeData> parsed_data,
                         std::set<QString> label_class_names)
        {
            if (!success || parsed_data.empty())
            {
                spdlog::error("数据解析失败或没有数据");
                QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection,
                                          Q_ARG(int, spdlog::level::err), Q_ARG(QString, "数据解析失败"));
                QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "completeTask", Qt::QueuedConnection);
                importer->deleteLater();
                return;
            }

            spdlog::info("开始在主线程中导入数据，图像数量: {}", parsed_data.size());
            QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection,
                                      Q_ARG(int, spdlog::level::info), Q_ARG(QString, "开始导入数据到数据库..."));

            // 1. 创建缺失的标签类别
            std::map<QString, int64_t> label_class_map;
            for (const auto &label_name : label_class_names)
            {
                // 检查标签类别是否已存在
                int64_t label_class_id = label_classes_->getLabelClassId(label_name);
                if (label_class_id < 0)
                {
                    // 创建新的标签类别
                    QString color    = generateDefaultColor(static_cast<int>(label_class_map.size()));
                    QString shortcut = "";
                    addLabelClass(label_name, color, shortcut);
                    label_class_id = label_classes_->getLabelClassId(label_name);
                }
                label_class_map[label_name] = label_class_id;
            }

            // 2. 准备图像路径
            std::vector<QString> image_paths;
            for (const auto &data : parsed_data)
            {
                image_paths.push_back(data.image_path);
            }

            // 3. 批量添加图像
            std::vector<int64_t> image_ids;
            if (!image_instances_->addImages(dataset_id, image_paths, image_ids))
            {
                spdlog::error("添加图像失败");
                QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection,
                                          Q_ARG(int, spdlog::level::err), Q_ARG(QString, "添加图像失败"));
                QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "completeTask", Qt::QueuedConnection);
                importer->deleteLater();
                return;
            }

            spdlog::info("成功导入 {} 个图像", image_ids.size());

            // 4. 为每个图像添加标注
            std::vector<int64_t>     all_label_image_ids;
            std::vector<int64_t>     all_label_class_ids;
            std::vector<QVariantMap> all_label_data;

            for (size_t i = 0; i < parsed_data.size() && i < image_ids.size(); ++i)
            {
                const auto &data     = parsed_data[i];
                int64_t     image_id = image_ids[i];

                for (const auto &shape : data.shapes)
                {
                    // 查找标签类别 ID
                    auto it = label_class_map.find(shape.label);
                    if (it == label_class_map.end())
                    {
                        spdlog::warn("未找到标签类别: {}", shape.label.toStdString());
                        continue;
                    }

                    int64_t label_class_id = it->second;

                    // 转换形状为标注数据
                    QVariantMap label_data_map = convertShapeToLabelData(shape, data.image_width, data.image_height);
                    if (label_data_map.isEmpty())
                    {
                        spdlog::warn("跳过无效的形状: label={}, shape_type={}", shape.label.toStdString(),
                                     shape.shape_type.toStdString());
                        continue;
                    }

                    all_label_image_ids.push_back(image_id);
                    all_label_class_ids.push_back(label_class_id);
                    all_label_data.push_back(label_data_map);
                }
            }

            // 5. 批量添加标注
            if (!all_label_image_ids.empty())
            {
                addLabels(all_label_image_ids, all_label_class_ids, all_label_data);
                spdlog::info("成功导入 {} 个标注", all_label_image_ids.size());
            }

            // 6. 更新数据集中的图像
            std::vector<int64_t> dataset_ids(image_ids.size(), dataset_id);
            datasets_->addImages(dataset_ids, image_ids);

            // 7. 更新统计信息
            updateDatasetsStats();

            spdlog::info("导入完成");
            QMetaObject::invokeMethod(
                ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection, Q_ARG(int, spdlog::level::info),
                Q_ARG(QString,
                      QString("导入完成: %1 个图像, %2 个标注").arg(image_ids.size()).arg(all_label_image_ids.size())));
            QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "completeTask", Qt::QueuedConnection);
            importer->deleteLater();
        },
        Qt::QueuedConnection);

    // 启动导入
    importer->startImport(dataset_id, image_dir, data_dir);
}

QVariantMap DataManager::convertShapeToLabelData(const LabelMeImporter::LabelMeShape &shape, int image_width,
                                                 int image_height)
{
    QVariantMap label_data;

    // 检查图像尺寸是否有效
    if (image_width <= 0 || image_height <= 0)
    {
        spdlog::warn("图像尺寸无效: width={}, height={}", image_width, image_height);
        return label_data;
    }

    // 处理 rectangle 类型
    if (shape.shape_type == "rectangle")
    {
        if (shape.points.size() < 2)
        {
            spdlog::warn("rectangle 类型的 shape 点数不足: {}", shape.points.size());
            return label_data;
        }

        QPointF p1 = shape.points[0];
        QPointF p2 = shape.points[1];

        double x_min = std::min(p1.x(), p2.x());
        double y_min = std::min(p1.y(), p2.y());
        double x_max = std::max(p1.x(), p2.x());
        double y_max = std::max(p1.y(), p2.y());

        double width  = x_max - x_min;
        double height = y_max - y_min;

        label_data["x"]      = x_min;
        label_data["y"]      = y_min;
        label_data["width"]  = width;
        label_data["height"] = height;
    }
    else if (shape.shape_type == "polygon")
    {
        if (shape.points.empty())
        {
            spdlog::warn("polygon 类型的 shape 没有点");
            return label_data;
        }

        double x_min = shape.points[0].x();
        double y_min = shape.points[0].y();
        double x_max = shape.points[0].x();
        double y_max = shape.points[0].y();

        for (const auto &point : shape.points)
        {
            x_min = std::min(x_min, point.x());
            y_min = std::min(y_min, point.y());
            x_max = std::max(x_max, point.x());
            y_max = std::max(y_max, point.y());
        }

        double width  = x_max - x_min;
        double height = y_max - y_min;

        label_data["x"]      = x_min;
        label_data["y"]      = y_min;
        label_data["width"]  = width;
        label_data["height"] = height;
    }
    else
    {
        spdlog::warn("不支持的 shape_type: {}, label: {}", shape.shape_type.toStdString(), shape.label.toStdString());
        return label_data;
    }

    return label_data;
}

QString DataManager::generateDefaultColor(int index)
{
    const double golden_ratio = 0.618033988749895;
    double       hue          = fmod(index * golden_ratio, 1.0);
    QColor       color        = QColor::fromHsvF(hue, 0.8, 0.9);
    return color.name();
}

} // namespace dltool::data
