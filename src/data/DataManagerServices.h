#pragma once

/**
 * @file DataManagerServices.h
 * @brief data 模块私有：DataManager 用例服务的共享上下文。
 *
 * 由 DataManager 在 init() 的模型构建完成后装配。用例服务只编排各自
 * 的业务流程；运行状态闸门、信号发射和 facade 私有能力一律通过钩子
 * 回到 DataManager，服务不持有跨线程数据库连接。
 */

#include <QObject>

#include "data/DataOperationWorkflow.h"
#include "data/DatasetExportSource.h"
#include "data/Datasets.h"
#include "data/Images.h"
#include "data/Labels.h"
#include "data/LabelClasses.h"
#include "data/ImageTags.h"

#include <QString>
#include <functional>

namespace dltool::database {
class ProjectDataBase;
}

namespace dltool::data {

/// 导出准备工作回调签名（与 DataManager::DatasetExportWork 等价，避免依赖公开头）。
using DatasetExportWorkFn = std::function<void(const DatasetExportSource &, DataOperationWorkflow::Result &)>;

struct DataManagerServices
{
    // 共享依赖（生命周期归 DataManager 所有）
    dltool::database::ProjectDataBase *database{nullptr};
    DatasetsListModel                 *datasets{nullptr};
    ImageInstancesListModel           *image_source{nullptr};
    LabelClassesListModel             *label_classes{nullptr};
    ImageTagsListModel                *image_tags{nullptr};
    LabelInstancesListModel           *label_source{nullptr};
    int                                method{0};
    QString                            project_dir;

    /// 服务创建的 worker QObject 的父对象与 connect 接收者（即 DataManager 自身）。
    QObject *host{nullptr};

    std::function<bool()> shutting_down;

    /// 登记进行中的工作流句柄，供关闭路径统一等待与取消。
    std::function<DataOperationWorkflow::HandlePtr(DataOperationWorkflow::HandlePtr)> track_operation;

    /// 数据操作运行闸门：阻断标签类别与标签类的变更并广播状态。
    std::function<void(bool)>  set_data_operation_running;
    std::function<bool()>      is_data_operation_running;

    /// 准备阶段工作流入口（ DataManager::runDatasetExportAsync 的门面）。
    std::function<DataOperationWorkflow::HandlePtr(QObject *context, DatasetExportRequest request,
                                                   DataOperationWorkflow::Options options, DatasetExportWorkFn work,
                                                   DataOperationWorkflow::Completion completion)>
        run_dataset_export_async;

    bool isShuttingDown() const
    {
        return shutting_down != nullptr && shutting_down();
    }
};

} // namespace dltool::data
