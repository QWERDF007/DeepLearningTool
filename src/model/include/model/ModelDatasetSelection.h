#pragma once

#include "dltool/model/Export.h"

#include <QVariantMap>
#include <set>
#include <utility>

namespace dltool::model {

class IModel;

struct MODEL_API ModelDatasetSelection
{
    std::set<qint64>                    dataset_ids;
    std::set<std::pair<qint64, qint64>> label_classes;

    bool isEmpty() const;
    bool containsDataset(qint64 dataset_id) const;
    bool containsLabelClass(qint64 dataset_id, qint64 label_class_id) const;
    bool contains(qint64 dataset_id, qint64 label_class_id) const;
};

struct MODEL_API ModelDatasetSelections
{
    ModelDatasetSelection train;
    ModelDatasetSelection validation;
    ModelDatasetSelection test;
};

MODEL_API ModelDatasetSelections modelDatasetSelectionsSnapshot(IModel *model);
MODEL_API QVariantMap            modelDatasetSelections(IModel *model);
MODEL_API void                   applyModelDatasetSelections(IModel *model, const QVariantMap &dataset_selections);

} // namespace dltool::model
