#include "feature/FeatureManager.h"

#include "data/DataManager.h"
#include "feature/FewShotLearningDataProvider.h"
#include "feature/ImageSearchDataProvider.h"
#include "settings/GlobalSettings.h"

#include <QPointer>
#include <utility>

namespace dltool::feature {

class FeatureManager::DataManagerDataProvider final
    : public ImageSearchDataProvider
    , public FewShotLearningDataProvider
{
public:
    explicit DataManagerDataProvider(dltool::data::DataManager *data_manager)
        : data_manager_(data_manager)
    {
    }

    int method() const override
    {
        return data_manager_ ? data_manager_->method() : 0;
    }

    std::vector<int64_t> selectedImageIds() const override
    {
        return data_manager_ ? data_manager_->selectedImageIds() : std::vector<int64_t>{};
    }

    std::vector<int64_t> allImageIds() const override
    {
        return data_manager_ ? data_manager_->allImageIds() : std::vector<int64_t>{};
    }

    QString imagePath(int64_t image_id) const override
    {
        return data_manager_ ? data_manager_->imagePath(image_id) : QString();
    }

    int64_t imageDatasetId(int64_t image_id) const override
    {
        return data_manager_ ? data_manager_->imageDatasetId(image_id) : -1;
    }

    QString databasePath() const override
    {
        return data_manager_ ? data_manager_->databasePath() : QString();
    }

    std::vector<int64_t> allLabelIds() const override
    {
        return data_manager_ ? data_manager_->allLabelIds() : std::vector<int64_t>{};
    }

    int64_t labelImageId(int64_t label_id) const override
    {
        return data_manager_ ? data_manager_->labelImageId(label_id) : -1;
    }

    int64_t labelClassId(int64_t label_id) const override
    {
        return data_manager_ ? data_manager_->labelClassId(label_id) : -1;
    }

    QVariantMap labelData(int64_t label_id) const override
    {
        return data_manager_ ? data_manager_->labelData(label_id) : QVariantMap();
    }

    QString labelClassName(int64_t label_class_id) const override
    {
        return data_manager_ ? data_manager_->labelClassName(label_class_id) : QString();
    }

    QString datasetName(int64_t dataset_id) const override
    {
        return data_manager_ ? data_manager_->datasetName(dataset_id) : QString();
    }

    void importMaskData(int64_t dataset_id, const QString &image_manifest_path,
                        const QString &prediction_output_dir) override
    {
        if (data_manager_)
            data_manager_->importMaskData(dataset_id, image_manifest_path, prediction_output_dir);
    }

    QMetaObject::Connection connectImportFinished(
        QObject *context, FewShotLearningDataProvider::ImportFinishedHandler handler) override
    {
        if (!data_manager_)
            return {};
        return data_manager_->connectImportFinished(context, std::move(handler));
    }

    void disconnectImportFinished(const QMetaObject::Connection &connection) override
    {
        if (data_manager_)
            data_manager_->disconnectImportFinished(connection);
    }

    void clearImageSearchResults() override
    {
        if (data_manager_)
            data_manager_->clearImageSearchResults();
    }

    void setImageSearchResults(const std::vector<int64_t> &image_ids, bool enable_filter) override
    {
        if (data_manager_)
            data_manager_->setImageSearchResults(image_ids, enable_filter);
    }

    void clearLabelSearchResults() override
    {
        if (data_manager_)
            data_manager_->clearLabelSearchResults();
    }

    void setLabelSearchResults(const std::vector<int64_t> &label_ids, bool enable_filter) override
    {
        if (data_manager_)
            data_manager_->setLabelSearchResults(label_ids, enable_filter);
    }

    bool applyImageClusterAssignments(const std::vector<ImageClusterAssignment> &assignments,
                                      bool include_noise,
                                      ImageClusterApplyMode apply_mode,
                                      ImageClusterApplyResult &result,
                                      QString &err_msg) override
    {
        if (!data_manager_)
        {
            err_msg = QStringLiteral("数据管理器未初始化");
            return false;
        }

        std::vector<dltool::data::DataManager::ImageClusterAssignment> data_assignments;
        data_assignments.reserve(assignments.size());
        for (const ImageClusterAssignment &assignment : assignments)
        {
            data_assignments.push_back({
                assignment.image_id,
                assignment.cluster_id,
                assignment.probability,
            });
        }

        dltool::data::DataManager::ImageClusterApplyResult data_result;
        const auto data_apply_mode = apply_mode == ImageClusterApplyMode::Copy
                                         ? dltool::data::DataManager::ImageClusterApplyMode::Copy
                                         : dltool::data::DataManager::ImageClusterApplyMode::Move;
        const bool ok = data_manager_->applyImageClusterAssignments(data_assignments, include_noise, data_apply_mode,
                                                                    data_result, err_msg);
        result.moved_image_count     = data_result.moved_image_count;
        result.copied_image_count    = data_result.copied_image_count;
        result.target_dataset_count  = data_result.target_dataset_count;
        result.skipped_noise_count   = data_result.skipped_noise_count;
        return ok;
    }

private:
    QPointer<dltool::data::DataManager> data_manager_;
};

FeatureManager::FeatureManager(dltool::data::DataManager *data_manager,
                               dltool::model::TaskManager *task_manager,
                               QObject *parent)
    : QObject(parent)
    , data_provider_(std::make_unique<DataManagerDataProvider>(data_manager))
{
    auto *image_search_provider = data_provider_.get();
    auto *few_shot_provider     = data_provider_.get();

    image_search_ = new ImageSearchController(image_search_provider, this);
    roi_search_ = new RoiSearchController(image_search_provider, this);
    image_cluster_ = new ImageClusterController(image_search_provider, this);
    smart_annotation_ = new SmartAnnotationController(this);
    few_shot_learning_ = new FewShotLearningController(few_shot_provider, data_manager, task_manager, this);

    if (auto *settings = dltool::settings::GlobalSettings::getInstance()->settingsGroup(
            dltool::settings::generated::AccessorKey::SmartAnnotation))
    {
        connect(settings, &dltool::settings::SettingsGroup::valueChanged, smart_annotation_,
                &SmartAnnotationController::clearCache);
    }
}

FeatureManager::~FeatureManager()
{
    delete few_shot_learning_;
    few_shot_learning_ = nullptr;
    delete smart_annotation_;
    smart_annotation_ = nullptr;
    delete image_cluster_;
    image_cluster_ = nullptr;
    delete roi_search_;
    roi_search_ = nullptr;
    delete image_search_;
    image_search_ = nullptr;
    data_provider_.reset();
}

ImageSearchController *FeatureManager::imageSearch() const
{
    return image_search_;
}

RoiSearchController *FeatureManager::roiSearch() const
{
    return roi_search_;
}

ImageClusterController *FeatureManager::imageCluster() const
{
    return image_cluster_;
}

SmartAnnotationController *FeatureManager::smartAnnotation() const
{
    return smart_annotation_;
}

FewShotLearningController *FeatureManager::fewShotLearning() const
{
    return few_shot_learning_;
}

} // namespace dltool::feature
