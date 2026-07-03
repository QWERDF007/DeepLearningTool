#include "model/IModel.h"

#include <QQmlEngine>

namespace dltool::model {

IModelConfig::IModelConfig(QObject *parent)
    : QObject(parent)
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
}

IModelConfig::~IModelConfig() = default;

ITrainParams *IModelConfig::trainParams()
{
    return nullptr;
}

ITestParams *IModelConfig::testParams()
{
    return nullptr;
}

const ITrainParams *IModelConfig::trainParams() const
{
    return nullptr;
}

const ITestParams *IModelConfig::testParams() const
{
    return nullptr;
}

IModel::IModel(std::unique_ptr<IModelConfig> config, QObject *parent)
    : QObject(parent)
    , config_(std::move(config))
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    if (config_)
    {
        config_->setParent(this);
        QQmlEngine::setObjectOwnership(config_.get(), QQmlEngine::CppOwnership);
    }
}

IModel::~IModel() = default;

IModelConfig *IModel::config()
{
    return config_.get();
}

const IModelConfig *IModel::config() const
{
    return config_.get();
}

QString IModel::uuid() const
{
    return uuid_;
}

void IModel::setUuid(const QString &uuid)
{
    if (uuid_ == uuid)
        return;
    uuid_ = uuid;
    emit uuidChanged();
}

QObject *IModel::trainDatasetViewModel() const
{
    return train_dataset_view_model_;
}

void IModel::setTrainDatasetViewModel(QObject *view_model)
{
    if (train_dataset_view_model_ == view_model)
        return;
    train_dataset_view_model_ = view_model;
    if (view_model != nullptr && view_model->parent() == nullptr)
        view_model->setParent(this);
    emit trainDatasetViewModelChanged();
}

QObject *IModel::validationDatasetViewModel() const
{
    return validation_dataset_view_model_;
}

void IModel::setValidationDatasetViewModel(QObject *view_model)
{
    if (validation_dataset_view_model_ == view_model)
        return;
    validation_dataset_view_model_ = view_model;
    if (view_model != nullptr && view_model->parent() == nullptr)
        view_model->setParent(this);
    emit validationDatasetViewModelChanged();
}

QObject *IModel::testDatasetViewModel() const
{
    return test_dataset_view_model_;
}

void IModel::setTestDatasetViewModel(QObject *view_model)
{
    if (test_dataset_view_model_ == view_model)
        return;
    test_dataset_view_model_ = view_model;
    if (view_model != nullptr && view_model->parent() == nullptr)
        view_model->setParent(this);
    emit testDatasetViewModelChanged();
}

} // namespace dltool::model
