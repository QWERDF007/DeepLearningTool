#include "data/DatasetSelectionStatisticsModel.h"

#include "data/CategoryStatisticsCalculator.h"
#include "data/DataManager.h"
#include "data/DataSelectionTreeModel.h"
#include "data/Images.h"
#include "data/LabelClasses.h"
#include "data/Labels.h"

#include <QAbstractItemModel>
#include <QLocale>
#include <QMetaObject>

#include <spdlog/spdlog.h>

namespace dltool::data {

DatasetSelectionStatisticsModel::DatasetSelectionStatisticsModel(QObject *parent)
    : QObject(parent)
{
    QVariantMap empty_dataset;
    empty_dataset.insert(QStringLiteral("data"), QVariantList{});
    empty_dataset.insert(QStringLiteral("backgroundColor"), QVariantList{});
    empty_dataset.insert(QStringLiteral("tooltips"), QVariantList{});

    QVariantList empty_datasets;
    empty_datasets.push_back(empty_dataset);

    image_chart_data_.insert(QStringLiteral("labels"), QVariantList{});
    image_chart_data_.insert(QStringLiteral("datasets"), empty_datasets);
    instance_chart_data_ = image_chart_data_;
}

void DatasetSelectionStatisticsModel::setDataManager(DataManager *data_manager)
{
    if (data_manager_ == data_manager)
        return;

    disconnectDataSources();
    data_manager_ = data_manager;
    connectDataSources();
    if (data_manager_ != nullptr)
    {
        connect(data_manager_, &QObject::destroyed, this,
                [this]()
                {
                    data_manager_ = nullptr;
                    disconnectDataSources();
                    scheduleRefresh();
                });
    }
    emit dataManagerChanged();
    scheduleRefresh();
}

void DatasetSelectionStatisticsModel::setSelectionModel(DataSelectionTreeModel *selection_model)
{
    if (selection_model_ == selection_model)
        return;

    if (selection_model_ != nullptr)
        disconnect(selection_model_, nullptr, this, nullptr);

    selection_model_ = selection_model;
    if (selection_model_ != nullptr)
    {
        connect(selection_model_, &DataSelectionTreeModel::selectionChanged, this,
                &DatasetSelectionStatisticsModel::scheduleRefresh);
        connect(selection_model_, &QObject::destroyed, this,
                [this]()
                {
                    selection_model_ = nullptr;
                    emit selectionModelChanged();
                    scheduleRefresh();
                });
    }

    emit selectionModelChanged();
    scheduleRefresh();
}

void DatasetSelectionStatisticsModel::connectDataSources()
{
    if (data_manager_ == nullptr)
        return;

    image_instances_ = data_manager_->imageInstances();
    label_classes_   = data_manager_->labelClasses();
    label_instances_ = data_manager_->labelInstances();

    connectSourceModel(image_instances_);
    connectSourceModel(label_classes_);
    connectSourceModel(label_instances_);

    if (image_instances_ != nullptr)
    {
        connect(image_instances_, &ImageInstancesListModel::statsChanged, this,
                &DatasetSelectionStatisticsModel::scheduleRefresh);
        connect(image_instances_, &QObject::destroyed, this, [this]() { image_instances_ = nullptr; });
    }
    if (label_classes_ != nullptr)
    {
        connect(label_classes_, &QObject::destroyed, this, [this]() { label_classes_ = nullptr; });
    }
    if (label_instances_ != nullptr)
    {
        connect(label_instances_, &QObject::destroyed, this, [this]() { label_instances_ = nullptr; });
    }
}

void DatasetSelectionStatisticsModel::disconnectDataSources()
{
    if (image_instances_ != nullptr)
        disconnect(image_instances_, nullptr, this, nullptr);
    if (label_classes_ != nullptr)
        disconnect(label_classes_, nullptr, this, nullptr);
    if (label_instances_ != nullptr)
        disconnect(label_instances_, nullptr, this, nullptr);

    image_instances_ = nullptr;
    label_classes_   = nullptr;
    label_instances_ = nullptr;
}

void DatasetSelectionStatisticsModel::connectSourceModel(QAbstractItemModel *model)
{
    if (model == nullptr)
        return;

    connect(model, &QAbstractItemModel::modelReset, this, &DatasetSelectionStatisticsModel::scheduleRefresh);
    connect(model, &QAbstractItemModel::rowsInserted, this, &DatasetSelectionStatisticsModel::scheduleRefresh);
    connect(model, &QAbstractItemModel::rowsRemoved, this, &DatasetSelectionStatisticsModel::scheduleRefresh);
    connect(model, &QAbstractItemModel::rowsMoved, this, &DatasetSelectionStatisticsModel::scheduleRefresh);
    connect(model, &QAbstractItemModel::layoutChanged, this, &DatasetSelectionStatisticsModel::scheduleRefresh);
    connect(model, &QAbstractItemModel::dataChanged, this, &DatasetSelectionStatisticsModel::scheduleRefresh);
}

void DatasetSelectionStatisticsModel::scheduleRefresh()
{
    if (refresh_pending_)
        return;

    refresh_pending_ = true;
    QMetaObject::invokeMethod(this,
                              [this]()
                              {
                                  refresh_pending_ = false;
                                  refresh();
                              },
                              Qt::QueuedConnection);
}

void DatasetSelectionStatisticsModel::refresh()
{
    try
    {
        const CategoryStatisticsResult result
            = calculateCategoryStatistics(label_instances_, label_classes_, image_instances_,
                                          CategoryStatisticsSource::AllData,
                                          [this](const int64_t image_id, const int64_t label_class_id)
                                          {
                                              if (selection_model_ == nullptr || image_instances_ == nullptr)
                                                  return false;
                                              const int64_t dataset_id = image_instances_->getImageDatasetId(image_id);
                                              return selection_model_->isNodeSelected(dataset_id, label_class_id);
                                          });

        total_instances_     = result.total_instances;
        total_images_        = result.total_images;

        auto build_chart_data = [&result](const bool image_dimension)
        {
            QVariantList labels;
            QVariantList values;
            QVariantList colors;
            QVariantList tooltips;

            for (const CategoryStatisticsItem &item : result.items)
            {
                const int count = image_dimension ? item.image_count : item.instance_count;
                if (count <= 0)
                    continue;

                labels.push_back(item.name);
                values.push_back(count);
                colors.push_back(item.color);
                const double percentage = image_dimension ? item.image_percentage : item.instance_percentage;
                const QString percentage_text
                    = QLocale::system().toString(percentage * 100.0, 'f', 2) + QStringLiteral("%");
                tooltips.push_back(QStringLiteral("%1\n数量：%2\n占比：%3")
                                       .arg(item.name)
                                       .arg(QLocale::system().toString(count))
                                       .arg(percentage_text));
            }

            QVariantMap dataset;
            dataset.insert(QStringLiteral("label"), image_dimension ? QStringLiteral("图像") : QStringLiteral("实例"));
            dataset.insert(QStringLiteral("data"), values);
            dataset.insert(QStringLiteral("backgroundColor"), colors);
            dataset.insert(QStringLiteral("tooltips"), tooltips);
            dataset.insert(QStringLiteral("hoverOffset"), 4);

            QVariantList datasets;
            datasets.push_back(dataset);

            QVariantMap chart;
            chart.insert(QStringLiteral("labels"), labels);
            chart.insert(QStringLiteral("datasets"), datasets);
            return chart;
        };

        image_chart_data_    = build_chart_data(true);
        instance_chart_data_ = build_chart_data(false);

        emit totalInstancesChanged();
        emit totalImagesChanged();
        emit chartDataChanged();
    }
    catch (const std::exception &e)
    {
        spdlog::error("刷新 DatasetSelectionStatisticsModel 失败: {}", e.what());
    }
}

QVariantMap DatasetSelectionStatisticsModel::chartData(const bool image_dimension) const
{
    return image_dimension ? image_chart_data_ : instance_chart_data_;
}

} // namespace dltool::data
