#include "model/ModelDatasetSelection.h"

#include "data/DataSelectionTreeModel.h"
#include "model/IModel.h"

#include <QModelIndex>
#include <QString>
#include <QVariantList>
#include <algorithm>
#include <map>

namespace dltool::model {

namespace {

enum class DatasetSelectionField
{
    DatasetIds,
    LabelClasses,
    DatasetId,
    LabelClassId,
};

enum class DatasetSelectionSplit
{
    Train,
    Validation,
    Test,
};

const std::map<DatasetSelectionField, QString> &datasetSelectionFieldNames()
{
    static const std::map<DatasetSelectionField, QString> names = {
        {    DatasetSelectionField::DatasetIds,     QStringLiteral("dataset_ids")},
        {  DatasetSelectionField::LabelClasses,   QStringLiteral("label_classes")},
        {     DatasetSelectionField::DatasetId,      QStringLiteral("dataset_id")},
        {DatasetSelectionField::LabelClassId, QStringLiteral("label_class_id")},
    };
    return names;
}

const std::map<DatasetSelectionSplit, QString> &datasetSelectionSplitNames()
{
    static const std::map<DatasetSelectionSplit, QString> names = {
        {     DatasetSelectionSplit::Train,      QStringLiteral("train")},
        {DatasetSelectionSplit::Validation, QStringLiteral("validation")},
        {      DatasetSelectionSplit::Test,       QStringLiteral("test")},
    };
    return names;
}

QString datasetSelectionFieldName(DatasetSelectionField field)
{
    const auto &names = datasetSelectionFieldNames();
    const auto  found = names.find(field);
    return found != names.end() ? found->second : QString();
}

QString datasetSelectionSplitName(DatasetSelectionSplit split)
{
    const auto &names = datasetSelectionSplitNames();
    const auto  found = names.find(split);
    return found != names.end() ? found->second : QString();
}

QObject *datasetSelectionObject(IModel *model, DatasetSelectionSplit split)
{
    if (model == nullptr)
        return nullptr;

    switch (split)
    {
    case DatasetSelectionSplit::Train:
        return model->trainDatasetViewModel();
    case DatasetSelectionSplit::Validation:
        return model->validationDatasetViewModel();
    case DatasetSelectionSplit::Test:
        return model->testDatasetViewModel();
    }
    return nullptr;
}

void applyDatasetSelectionMap(QObject *selection_object, const QVariantMap &selection)
{
    auto *selection_model = qobject_cast<dltool::data::DataSelectionTreeModel *>(selection_object);
    if (selection_model == nullptr)
        return;

    selection_model->clearSelection();
    const QVariantList dataset_ids =
        selection.value(datasetSelectionFieldName(DatasetSelectionField::DatasetIds)).toList();
    for (const QVariant &dataset_value : dataset_ids)
    {
        bool         ok = false;
        const qint64 dataset_id = dataset_value.toLongLong(&ok);
        if (ok && dataset_id >= 0)
            selection_model->setNodeSelected(dataset_id, -1, true);
    }

    const QVariantList label_classes =
        selection.value(datasetSelectionFieldName(DatasetSelectionField::LabelClasses)).toList();
    for (const QVariant &entry : label_classes)
    {
        const QVariantMap map = entry.toMap();
        bool              dataset_ok = false;
        bool              class_ok = false;
        const qint64      dataset_id =
            map.value(datasetSelectionFieldName(DatasetSelectionField::DatasetId)).toLongLong(&dataset_ok);
        const qint64 label_class_id =
            map.value(datasetSelectionFieldName(DatasetSelectionField::LabelClassId)).toLongLong(&class_ok);
        if (dataset_ok && class_ok && dataset_id >= 0 && label_class_id >= 0)
            selection_model->setNodeSelected(dataset_id, label_class_id, true);
    }
}

} // namespace

bool ModelDatasetSelection::isEmpty() const
{
    return dataset_ids.empty() && label_classes.empty();
}

bool ModelDatasetSelection::containsDataset(qint64 dataset_id) const
{
    if (dataset_id < 0)
        return false;
    if (dataset_ids.find(dataset_id) != dataset_ids.end())
        return true;
    return std::any_of(label_classes.begin(), label_classes.end(),
                       [dataset_id](const auto &entry) { return entry.first == dataset_id; });
}

bool ModelDatasetSelection::containsLabelClass(qint64 dataset_id, qint64 label_class_id) const
{
    return dataset_id >= 0 && label_class_id >= 0
        && label_classes.find({dataset_id, label_class_id}) != label_classes.end();
}

bool ModelDatasetSelection::contains(qint64 dataset_id, qint64 label_class_id) const
{
    if (dataset_id < 0)
        return false;
    if (dataset_ids.find(dataset_id) != dataset_ids.end())
        return true;
    return containsLabelClass(dataset_id, label_class_id);
}

namespace {

ModelDatasetSelection modelDatasetSelection(QObject *selection_object)
{
    ModelDatasetSelection selection;
    auto                 *selection_model = qobject_cast<dltool::data::DataSelectionTreeModel *>(selection_object);
    if (selection_model == nullptr)
        return selection;

    for (int dataset_row = 0; dataset_row < selection_model->rowCount(); ++dataset_row)
    {
        const QModelIndex dataset_index = selection_model->index(dataset_row, 0);
        bool              dataset_ok = false;
        qint64            dataset_id =
            selection_model->data(dataset_index, dltool::data::DataSelectionTreeModel::DatasetIdRole)
                .toLongLong(&dataset_ok);
        if (!dataset_ok || dataset_id < 0)
        {
            dataset_id = selection_model->data(dataset_index, dltool::data::DataSelectionTreeModel::ItemIdRole)
                             .toLongLong(&dataset_ok);
        }
        if (!dataset_ok || dataset_id < 0)
            continue;

        if (selection_model->isNodeSelected(dataset_id, -1))
        {
            selection.dataset_ids.insert(dataset_id);
            continue;
        }

        for (int class_row = 0; class_row < selection_model->rowCount(dataset_index); ++class_row)
        {
            const QModelIndex class_index = selection_model->index(class_row, 0, dataset_index);
            bool              class_ok = false;
            qint64            label_class_id =
                selection_model->data(class_index, dltool::data::DataSelectionTreeModel::LabelClassIdRole)
                    .toLongLong(&class_ok);
            if (!class_ok || label_class_id < 0)
            {
                label_class_id = selection_model->data(class_index, dltool::data::DataSelectionTreeModel::ItemIdRole)
                                     .toLongLong(&class_ok);
            }
            if (class_ok && label_class_id >= 0 && selection_model->isNodeSelected(dataset_id, label_class_id))
                selection.label_classes.insert({dataset_id, label_class_id});
        }
    }
    return selection;
}

ModelDatasetSelections modelDatasetSelectionsSnapshot(IModel *model)
{
    ModelDatasetSelections selections;
    if (model == nullptr)
        return selections;

    selections.train      = modelDatasetSelection(model->trainDatasetViewModel());
    selections.validation = modelDatasetSelection(model->validationDatasetViewModel());
    selections.test       = modelDatasetSelection(model->testDatasetViewModel());
    return selections;
}

QVariantMap modelDatasetSelectionMap(const ModelDatasetSelection &selection)
{
    QVariantList dataset_ids;
    dataset_ids.reserve(static_cast<int>(selection.dataset_ids.size()));
    for (qint64 dataset_id : selection.dataset_ids)
        dataset_ids.append(dataset_id);

    QVariantList label_classes;
    label_classes.reserve(static_cast<int>(selection.label_classes.size()));
    for (const auto &[dataset_id, label_class_id] : selection.label_classes)
    {
        label_classes.append(QVariantMap{
            {     datasetSelectionFieldName(DatasetSelectionField::DatasetId),      dataset_id},
            {datasetSelectionFieldName(DatasetSelectionField::LabelClassId), label_class_id},
        });
    }

    return {
        {  datasetSelectionFieldName(DatasetSelectionField::DatasetIds),   dataset_ids},
        {datasetSelectionFieldName(DatasetSelectionField::LabelClasses), label_classes},
    };
}

} // namespace

QVariantMap modelDatasetSelections(IModel *model)
{
    const ModelDatasetSelections selections = modelDatasetSelectionsSnapshot(model);
    QVariantMap                  result;
    result.insert(datasetSelectionSplitName(DatasetSelectionSplit::Train), modelDatasetSelectionMap(selections.train));
    result.insert(datasetSelectionSplitName(DatasetSelectionSplit::Validation),
                  modelDatasetSelectionMap(selections.validation));
    result.insert(datasetSelectionSplitName(DatasetSelectionSplit::Test), modelDatasetSelectionMap(selections.test));
    return result;
}

void applyModelDatasetSelections(IModel *model, const QVariantMap &dataset_selections)
{
    if (model == nullptr || dataset_selections.isEmpty())
        return;

    for (const DatasetSelectionSplit split :
         {DatasetSelectionSplit::Train, DatasetSelectionSplit::Validation, DatasetSelectionSplit::Test})
    {
        const QString split_name = datasetSelectionSplitName(split);
        if (dataset_selections.contains(split_name))
            applyDatasetSelectionMap(datasetSelectionObject(model, split), dataset_selections.value(split_name).toMap());
    }
}

} // namespace dltool::model
