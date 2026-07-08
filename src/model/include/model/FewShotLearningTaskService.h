#pragma once

#include "dltool/model/Export.h"
#include "model/ExternalProcessSpec.h"

#include <QString>
#include <QStringList>
#include <cstdint>
#include <vector>

namespace dltool::data {
class DataManager;
} // namespace dltool::data

namespace dltool::model {

struct MODEL_API FewShotLearningRequest
{
    std::vector<int64_t> train_dataset_ids;
    std::vector<int64_t> validation_dataset_ids;
    std::vector<int64_t> test_dataset_ids;
    std::vector<int64_t> label_class_ids;
};

struct MODEL_API FewShotPredictionImportTarget
{
    int64_t dataset_id{-1};
    QString manifest_path;
};

struct MODEL_API FewShotLearningRunContext
{
    QString     task_uuid;
    QString     python_executable;
    QString     fs_sam2_root;
    QString     sam2_checkpoint;
    QString     sam2_cfg;
    QString     run_dir;
    QString     train_dataset_dir;
    QString     validation_dataset_dir;
    QString     test_dataset_dir;
    QString     output_dir;
    QString     train_manifest_path;
    QString     validation_manifest_path;
    QString     test_manifest_path;
    QString     train_script;
    QString     predict_script;
    QString     box_to_mask_script;
    QStringList python_paths;
    QString     training_checkpoint_path;
    QString     checkpoint_path;
    QString     exp_id;
    QString     logpath;

    int    kshot{1};
    int    epochs{50};
    int    batch_size{2};
    int    num_workers{0};
    int    image_size{1024};
    double lr{1e-4};
    double weight_decay{1e-6};

    int     train_task_id{-1};
    int     predict_task_id{-1};
    int     box_to_mask_task_id{-1};
    QString task_host;
    quint16 task_port{0};

    bool requires_box_to_mask{false};

    std::vector<FewShotPredictionImportTarget> import_targets;
};

class MODEL_API FewShotLearningTaskService
{
public:
    FewShotLearningTaskService(int method, QString project_dir, dltool::data::DataManager *data_manager);

    bool prepare(const FewShotLearningRequest &request, FewShotLearningRunContext &context,
                 QString *err_msg = nullptr) const;
    bool buildTrainingSpec(const FewShotLearningRunContext &context, ExternalProcessSpec &process_spec,
                           QString *err_msg = nullptr) const;
    bool buildPredictionSpec(const FewShotLearningRunContext &context, ExternalProcessSpec &process_spec,
                             QString *err_msg = nullptr) const;
    bool buildBoxToMaskSpec(const FewShotLearningRunContext &context, int split_index,
                            ExternalProcessSpec &process_spec, QString *err_msg = nullptr) const;

    int boxToMaskSplitCount(const FewShotLearningRunContext &context) const;

    static QString trainTaskName();
    static QString predictTaskName();
    static QString boxToMaskTaskName();

private:
    int                       method_{-1};
    QString                   project_dir_;
    dltool::data::DataManager *data_manager_{nullptr};
};

} // namespace dltool::model
