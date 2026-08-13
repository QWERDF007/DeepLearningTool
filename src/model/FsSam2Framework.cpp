#include "core/CoreDef.h"
#include "model/ModelRegistry.h"

namespace dltool::model { namespace {

using dltool::core::DeepLearningMethod;

/**
 * @brief 构建 FS-SAM2 框架定义
 * @return 框架定义
 */
FrameworkDefinition fsSam2Framework()
{
    FrameworkDefinition framework;
    framework.name           = QStringLiteral("FS-SAM2");
    framework.root           = QStringLiteral("python/fornib/FS-SAM2");
    framework.train_script   = QStringLiteral("train.py");
    framework.predict_script = QStringLiteral("predict.py");
    framework.task_capabilities.push_back({dltool::model::ModelTaskType::BoxToMask, QStringLiteral("box_to_mask.py")});
    framework.scripts.insert(QStringLiteral("box_to_mask"), QStringLiteral("box_to_mask.py"));
    framework.python_paths = {
        QStringLiteral("."),
    };
    framework.visible_for_model_creation = false;
    framework.write_to_database          = true;
    // 小样本能力位：FS-SAM2 的测试任务没有 UUID 测试任务记录、无评估适配器，
    // 控制器/准备/数据集组织逻辑据此走小样本专用分支，不再按框架名特判。
    framework.few_shot = true;
    // 小样本测试任务没有 UUID，使用稳定目录名作为测试任务标识与存储目录。
    framework.default_test_task_directory = QStringLiteral("fs_sam2");
    return framework;
}

DLT_REGISTER_FRAMEWORK(DeepLearningMethod::Detection, FsSam2Detection, fsSam2Framework());
DLT_REGISTER_FRAMEWORK(DeepLearningMethod::Segmentation, FsSam2Segmentation, fsSam2Framework());
DLT_REGISTER_FRAMEWORK(DeepLearningMethod::AnomalyDetection, FsSam2Anomaly, fsSam2Framework());

}} // namespace dltool::model
