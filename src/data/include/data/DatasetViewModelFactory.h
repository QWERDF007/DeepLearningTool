#pragma once

#include "dltool/data/Export.h"

class QObject;

namespace dltool::data {

class DataManager;
class DataSelectionTreeModel;

class DATA_API DatasetViewModelFactory
{
public:
    static DataSelectionTreeModel *createDatasetSelectionModel(const DataManager *data_manager,
                                                               QObject *owner = nullptr);
    static DataSelectionTreeModel *createLabelClassSelectionModel(const DataManager *data_manager,
                                                                  QObject *owner = nullptr);
};

} // namespace dltool::data
