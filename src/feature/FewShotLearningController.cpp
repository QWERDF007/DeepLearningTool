#include "feature/FewShotLearningController.h"

#include "data/DataManager.h"
#include "data/DataSelectionTreeModel.h"
#include "data/DatasetViewModelFactory.h"
#include "feature/Utils.h"
#include "model/FewShotLearningTaskService.h"
#include "model/ModelTaskController.h"
#include "settings/GlobalSettings.h"
#include "settings/SettingsKeys.h"

#include <spdlog/spdlog.h>

#include <QMetaObject>

namespace dltool::feature {

namespace {

QVariantList variantListFromViewModel(QObject *view_model, const char *method_name)
{
    if (view_model == nullptr)
        return {};

    QVariantList ids;
    QMetaObject::invokeMethod(view_model, method_name, Q_RETURN_ARG(QVariantList, ids));
    return ids;
}

QVariantList selectedDatasetIdsFromViewModel(QObject *view_model)
{
    QVariantList ids = variantListFromViewModel(view_model, "selectedDatasetIds");
    if (ids.empty())
        ids = variantListFromViewModel(view_model, "selectedIds");
    return ids;
}

QVariantList selectedLabelClassIdsFromViewModel(QObject *view_model)
{
    QVariantList ids = variantListFromViewModel(view_model, "selectedLabelClassIds");
    if (ids.empty())
        ids = variantListFromViewModel(view_model, "selectedIds");
    return ids;
}

} // namespace

FewShotLearningController::FewShotLearningController(dltool::data::DataManager          *data_manager,
                                                     dltool::model::ModelTaskController *model_task_controller,
                                                     QObject                            *parent)
    : QObject(parent)
    , data_manager_(data_manager)
    , model_task_controller_(model_task_controller)
{
    auto *gs = dltool::settings::GlobalSettings::getInstance();
    enabled_ = gs->valueForField(dltool::settings::generated::field::FewShotLearning::Key::Enabled, true).toBool();

    setTrainDatasetViewModel(dltool::data::DatasetViewModelFactory::createDatasetSelectionModel(data_manager, this));
    setValidationDatasetViewModel(
        dltool::data::DatasetViewModelFactory::createDatasetSelectionModel(data_manager, this));
    setTestDatasetViewModel(dltool::data::DatasetViewModelFactory::createDatasetSelectionModel(data_manager, this));

    if (model_task_controller_ != nullptr)
    {
        connect(model_task_controller_, &dltool::model::ModelTaskController::fewShotLearningRunningChanged, this,
                [this](bool running) { setRunning(running); });
        connect(model_task_controller_, &dltool::model::ModelTaskController::fewShotLearningLastErrorChanged, this,
                [this](const QString &message) { setLastError(message); });
        connect(model_task_controller_, &dltool::model::ModelTaskController::fewShotLearningFinished, this,
                [this](bool success, const QString &message)
                {
                    setRunning(false);
                    if (!success && !message.isEmpty())
                        setLastError(message);
                });
    }

    connect(gs->catalog(), &dltool::settings::SettingsCatalog::fieldValueChanged, this,
            [this](const QString &group_key, const QString &name, const QVariant &value)
            {
                if (group_key == QStringLiteral("FewShotLearningSettings") && name == QStringLiteral("enabled"))
                {
                    const bool v = value.toBool();
                    if (v != enabled_)
                    {
                        enabled_ = v;
                        emit enabledChanged();
                    }
                }
            });
}

FewShotLearningController::~FewShotLearningController() = default;

bool FewShotLearningController::enabled() const
{
    return enabled_;
}

bool FewShotLearningController::running() const
{
    return running_;
}

QString FewShotLearningController::lastError() const
{
    return last_error_;
}

QObject *FewShotLearningController::trainDatasetViewModel() const
{
    return train_dataset_view_model_;
}

void FewShotLearningController::setTrainDatasetViewModel(QObject *view_model)
{
    if (train_dataset_view_model_ == view_model)
        return;
    train_dataset_view_model_ = view_model;
    if (view_model != nullptr && view_model->parent() == nullptr)
        view_model->setParent(this);
    emit trainDatasetViewModelChanged();
}

QObject *FewShotLearningController::validationDatasetViewModel() const
{
    return validation_dataset_view_model_;
}

void FewShotLearningController::setValidationDatasetViewModel(QObject *view_model)
{
    if (validation_dataset_view_model_ == view_model)
        return;
    validation_dataset_view_model_ = view_model;
    if (view_model != nullptr && view_model->parent() == nullptr)
        view_model->setParent(this);
    emit validationDatasetViewModelChanged();
}

QObject *FewShotLearningController::testDatasetViewModel() const
{
    return test_dataset_view_model_;
}

void FewShotLearningController::setTestDatasetViewModel(QObject *view_model)
{
    if (test_dataset_view_model_ == view_model)
        return;
    test_dataset_view_model_ = view_model;
    if (view_model != nullptr && view_model->parent() == nullptr)
        view_model->setParent(this);
    emit testDatasetViewModelChanged();
}

QObject *FewShotLearningController::labelClassViewModel() const
{
    return label_class_view_model_;
}

void FewShotLearningController::setLabelClassViewModel(QObject *view_model)
{
    if (label_class_view_model_ == view_model)
        return;
    label_class_view_model_ = view_model;
    if (view_model != nullptr && view_model->parent() == nullptr)
        view_model->setParent(this);
    emit labelClassViewModelChanged();
}

bool FewShotLearningController::startFsSam2()
{
    QVariantList label_class_ids = selectedLabelClassIdsFromViewModel(train_dataset_view_model_);
    if (label_class_ids.empty())
        label_class_ids = selectedLabelClassIdsFromViewModel(label_class_view_model_);

    return startFsSam2WithIds(selectedDatasetIdsFromViewModel(train_dataset_view_model_),
                              selectedDatasetIdsFromViewModel(validation_dataset_view_model_),
                              selectedDatasetIdsFromViewModel(test_dataset_view_model_), label_class_ids);
}

bool FewShotLearningController::startFsSam2WithIds(const QVariantList &train_dataset_ids,
                                                   const QVariantList &validation_dataset_ids,
                                                   const QVariantList &test_dataset_ids,
                                                   const QVariantList &label_class_ids)
{
    if (running_)
        return false;

    setLastError({});
    if (model_task_controller_ == nullptr)
    {
        setLastError(QString("模型任务控制器未初始化"));
        return false;
    }

    dltool::model::FewShotLearningRequest request;
    request.train_dataset_ids      = parseInt64Ids(train_dataset_ids, true, true);
    request.validation_dataset_ids = parseInt64Ids(validation_dataset_ids, true, true);
    request.test_dataset_ids       = parseInt64Ids(test_dataset_ids, true, true);
    request.label_class_ids        = parseInt64Ids(label_class_ids, true, true);

    QString err_msg;
    if (!model_task_controller_->startFewShotLearning(request, &err_msg))
    {
        setLastError(err_msg);
        spdlog::error("启动小样本学习失败: {}", err_msg.toUtf8().constData());
        return false;
    }

    setRunning(true);
    return true;
}

void FewShotLearningController::clearLastError()
{
    setLastError({});
}

void FewShotLearningController::cancel()
{
    if (model_task_controller_ != nullptr)
        model_task_controller_->stopFewShotLearning();
}

void FewShotLearningController::setRunning(bool running)
{
    if (running_ == running)
        return;
    running_ = running;
    emit runningChanged();
}

void FewShotLearningController::setLastError(const QString &last_error)
{
    if (last_error_ == last_error)
        return;
    last_error_ = last_error;
    emit lastErrorChanged();
}

} // namespace dltool::feature
