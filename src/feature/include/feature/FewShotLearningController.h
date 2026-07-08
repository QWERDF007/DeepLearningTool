#pragma once

#include "dltool/feature/Export.h"

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QVariantList>
#include <QtQml>
#include <memory>
#include <vector>

namespace dltool::model {
class TaskManager;
}

namespace dltool::data {
class DataManager;
}

namespace dltool::feature {

class FewShotLearningDataProvider;

class FEATURE_API FewShotLearningController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(FewShotLearningController)
    QML_UNCREATABLE("Can not create FewShotLearningController directly!")
    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged FINAL)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged FINAL)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged FINAL)
    Q_PROPERTY(QObject *trainDatasetViewModel READ trainDatasetViewModel WRITE setTrainDatasetViewModel NOTIFY
                   trainDatasetViewModelChanged FINAL)
    Q_PROPERTY(QObject *validationDatasetViewModel READ validationDatasetViewModel WRITE setValidationDatasetViewModel
                   NOTIFY validationDatasetViewModelChanged FINAL)
    Q_PROPERTY(QObject *testDatasetViewModel READ testDatasetViewModel WRITE setTestDatasetViewModel NOTIFY
                   testDatasetViewModelChanged FINAL)
    Q_PROPERTY(QObject *labelClassViewModel READ labelClassViewModel WRITE setLabelClassViewModel NOTIFY
                   labelClassViewModelChanged FINAL)

public:
    /**
     * @brief 构造函数
     * @param data_provider 小样本学习数据提供者
     * @param parent 父对象
     */
    explicit FewShotLearningController(FewShotLearningDataProvider *data_provider,
                                       dltool::data::DataManager *data_manager,
                                       QObject *parent = nullptr);
    ~FewShotLearningController() override;

    /**
     * @brief 功能是否启用
     * @return 启用返回 true
     */
    bool enabled() const;

    /**
     * @brief 是否正在运行
     * @return 运行中返回 true
     */
    bool running() const;

    /**
     * @brief 获取最后一次错误信息
     * @return 错误信息文本
     */
    QString lastError() const;

    QObject *trainDatasetViewModel() const;
    void     setTrainDatasetViewModel(QObject *view_model);

    QObject *validationDatasetViewModel() const;
    void     setValidationDatasetViewModel(QObject *view_model);

    QObject *testDatasetViewModel() const;
    void     setTestDatasetViewModel(QObject *view_model);

    QObject *labelClassViewModel() const;
    void     setLabelClassViewModel(QObject *view_model);

    /**
     * @brief 启动小样本学习流程（FS-SAM2），数据集和类别从控制器持有的 ViewModel 读取
     * @return 启动成功返回 true
     */
    Q_INVOKABLE bool startFsSam2();

    /// 清除最后一次错误信息
    Q_INVOKABLE void clearLastError();

    /// 取消当前运行的小样本学习任务
    Q_INVOKABLE void cancel();

signals:
    void enabledChanged();
    void runningChanged();
    void lastErrorChanged();
    void trainDatasetViewModelChanged();
    void validationDatasetViewModelChanged();
    void testDatasetViewModelChanged();
    void labelClassViewModelChanged();

private:
    /// 运行阶段枚举
    enum class RunStage
    {
        Idle,          ///< 空闲
        PreparingMask, ///< 准备 Mask
        Training,      ///< 训练中
        Predicting,    ///< 推理中
    };

    struct RunContext;

    /**
     * @brief 准备运行环境
     * @param train_dataset_ids 训练数据集 ID
     * @param validation_dataset_ids 验证数据集 ID；为空时复用训练数据集
     * @param test_dataset_ids 测试数据集 ID
     * @param label_class_ids 标注类别 ID
     * @param context 运行上下文（输出）
     * @param err_msg 错误信息（输出）
     * @return 准备成功返回 true
     */
    bool prepareRun(const std::vector<int64_t> &train_dataset_ids,
                    const std::vector<int64_t> &validation_dataset_ids,
                    const std::vector<int64_t> &test_dataset_ids, const std::vector<int64_t> &label_class_ids,
                    RunContext &context, QString &err_msg) const;

    bool startFsSam2WithIds(const QVariantList &train_dataset_ids, const QVariantList &validation_dataset_ids,
                            const QVariantList &test_dataset_ids, const QVariantList &label_class_ids);

    /**
     * @brief 启动训练流程
     * @param context 运行上下文
     * @param err_msg 错误信息（输出）
     * @return 启动成功返回 true
     */
    bool startTraining(const RunContext &context, QString &err_msg);

    /**
     * @brief 启动框转 Mask 预处理
     * @param context 运行上下文
     * @param split_index 当前数据集拆分索引
     * @param err_msg 错误信息（输出）
     * @return 启动成功返回 true
     */
    bool startBoxToMask(const RunContext &context, int split_index, QString &err_msg);

    /**
     * @brief 启动推理流程
     * @param context 运行上下文
     * @param err_msg 错误信息（输出）
     * @return 启动成功返回 true
     */
    bool startPrediction(const RunContext &context, QString &err_msg);

    /**
     * @brief 启动 Python 子进程
     * @param context 运行上下文
     * @param arguments 命令行参数
     * @param err_msg 错误信息（输出）
     * @return 启动成功返回 true
     */
    bool startProcess(const RunContext &context, const QStringList &arguments, QString &err_msg);

    /// 开始导入预测结果
    void startPredictionImports();
    /// 开始导入下一个预测结果
    void startNextPredictionImport();

    /**
     * @brief 处理预测结果导入完成
     * @param success 是否成功
     * @param message 导入消息
     */
    void handlePredictionImportFinished(bool success, const QString &message);

    /// 结束运行并重置状态
    void finishRun();

    /**
     * @brief 处理子进程结束
     * @param exit_code 退出码
     * @param exit_status 退出状态
     */
    void handleProcessFinished(int exit_code, QProcess::ExitStatus exit_status);

    /**
     * @brief 处理任务管理器请求停止任务
     * @param task_id 任务 ID
     */
    void handleTaskStopRequested(int task_id);

    void setRunning(bool running);
    void setLastError(const QString &last_error);

    FewShotLearningDataProvider *data_provider_{nullptr}; ///< 数据提供者

    QPointer<dltool::model::TaskManager> task_manager_;               ///< 任务管理器
    QProcess                            *process_{nullptr};           ///< Python 子进程
    QMetaObject::Connection              import_finished_connection_; ///< 导入完成信号连接

    int train_task_id_{-1};       ///< 训练任务 ID
    int predict_task_id_{-1};     ///< 推理任务 ID
    int box_to_mask_task_id_{-1}; ///< 框转 Mask 任务 ID

    QString prediction_output_dir_; ///< 预测输出目录
    QString checkpoint_path_;       ///< 模型检查点路径

    std::unique_ptr<RunContext> active_context_; ///< 当前运行上下文

    RunStage stage_{RunStage::Idle}; ///< 当前运行阶段

    int current_prepare_split_index_{0}; ///< 当前预处理数据集拆分索引
    int current_import_index_{0};        ///< 当前导入索引

    bool importing_predictions_{false}; ///< 是否正在导入预测结果
    bool enabled_{true};                ///< 功能是否启用
    bool running_{false};               ///< 是否正在运行
    bool stop_requested_{false};        ///< 是否已请求停止

    QString last_error_; ///< 最后一次错误信息

    QPointer<QObject> train_dataset_view_model_;
    QPointer<QObject> validation_dataset_view_model_;
    QPointer<QObject> test_dataset_view_model_;
    QPointer<QObject> label_class_view_model_;
};

} // namespace dltool::feature
