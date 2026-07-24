#include "data/DatasetViewModelFactory.h"

#include "data/DataManager.h"
#include "data/DataSelectionTreeModel.h"

#include <QQmlEngine>

namespace dltool::data {

DataSelectionTreeModel *DatasetViewModelFactory::createDatasetSelectionModel(const DataManager *data_manager,
                                                                             QObject *owner)
{
    if (data_manager == nullptr)
        return nullptr;

    auto *model = new DataSelectionTreeModel(owner != nullptr ? owner : const_cast<DataManager *>(data_manager));
    model->setDatasetClassSourceModels(data_manager->datasets(), data_manager->labelClasses(),
                                       data_manager->imageSource(), data_manager->labelSource());
    QQmlEngine::setObjectOwnership(model, QQmlEngine::CppOwnership);
    return model;
}

DataSelectionTreeModel *DatasetViewModelFactory::createLabelClassSelectionModel(const DataManager *data_manager,
                                                                                QObject *owner)
{
    if (data_manager == nullptr)
        return nullptr;

    auto *model = new DataSelectionTreeModel(owner != nullptr ? owner : const_cast<DataManager *>(data_manager));
    model->setSourceModel(data_manager->labelClasses());
    model->setIdRole(LabelClassesListModel::LabelClassIdRole);
    model->setNameRole(LabelClassesListModel::NameRole);
    model->setColorRole(LabelClassesListModel::ColorRole);
    QQmlEngine::setObjectOwnership(model, QQmlEngine::CppOwnership);
    return model;
}

} // namespace dltool::data
