#pragma once

#include "dltool/feature/Export.h"

#include <inferrt/features/SAMImagePredictor.hpp>
#include <inferrt/model/ModelRuntime.hpp>

#include <QObject>
#include <QPointer>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml>
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>

class QThread;

namespace irt::features {
class SAMImagePredictor;
} // namespace irt::features

namespace irt::model {
enum class ModelPrecision;
} // namespace irt::model

namespace dltool::feature {

class SmartAnnotationExecutor;

class FEATURE_API SmartAnnotationController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SmartAnnotationController)
    QML_UNCREATABLE("Can not create SmartAnnotationController directly!")

    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged FINAL)
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged FINAL)
    Q_PROPERTY(bool loadingModel READ isLoadingModel NOTIFY loadingModelChanged FINAL)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged FINAL)
    Q_PROPERTY(QVariantMap lastResult READ lastResult NOTIFY lastResultChanged FINAL)

    friend class SmartAnnotationExecutor;

public:
    using ModelLoader = std::function<std::unique_ptr<irt::features::SAMImagePredictor>(
        const QString &, const QString &, const irt::model::ModelRuntime &, irt::model::ModelPrecision)>;

    using PredictExecutor = std::function<irt::features::SAMImagePrediction(
        irt::features::SAMImagePredictor *predictor,
        const std::filesystem::path &image_path,
        const irt::features::SAMImagePrompt &prompt,
        const irt::features::SAMImagePredictOptions &options)>;

    /**
     * @brief 构造函数
     * @param parent 父对象
     */
    explicit SmartAnnotationController(QObject *parent = nullptr);
    explicit SmartAnnotationController(ModelLoader model_loader, QObject *parent = nullptr);
    explicit SmartAnnotationController(ModelLoader model_loader, PredictExecutor predict_executor,
                                       QObject *parent = nullptr);
    ~SmartAnnotationController() override;

    /** @brief 等待执行器与模型加载线程收敛并丢弃迟到结果。 */
    void shutdown();
    void requestShutdown();

    /**
     * @brief 智能标注功能是否启用
     * @return 启用返回 true
     */
    bool enabled() const
    {
        return enabled_;
    }

    /**
     * @brief 是否正在运行标注推理
     * @return 运行中返回 true
     */
    bool isRunning() const
    {
        return running_;
    }

    /**
     * @brief 是否正在加载模型
     * @return 加载中返回 true
     */
    bool isLoadingModel() const
    {
        return loading_model_;
    }

    /**
     * @brief 获取最后一次错误信息
     * @return 错误信息文本
     */
    QString lastError() const
    {
        return last_error_;
    }

    /**
     * @brief 获取最近一次成功的推理结果
     * @return 包含推理结果的 QVariantMap
     */
    QVariantMap lastResult() const
    {
        return last_result_;
    }

    /**
     * @brief 执行智能标注推理
     * @param image_path 输入图像路径
     * @param prompt_points 提示点列表
     * @param options 输入图像选项，可包含 prompt_box 提示框
     * @return 包含推理请求状态的 QVariantMap
     */
    Q_INVOKABLE QVariantMap infer(const QString &image_path, const QVariantList &prompt_points,
                                  const QVariantMap &options);

    /// 清除模型缓存并取消进行中的推理
    Q_INVOKABLE void clearCache();

    /**
     * @brief 等待当前推理完成（供测试及同步确认使用）
     * @param timeout_ms 超时毫秒数
     * @return 完成返回 true，超时返回 false
     */
    Q_INVOKABLE bool waitForFinished(int timeout_ms = 3000);

signals:
    void enabledChanged();
    void runningChanged();
    void loadingModelChanged();
    void lastErrorChanged();
    void lastResultChanged();

    /**
     * @brief 模型加载完成信号
     * @param success 加载是否成功
     */
    void modelLoadFinished(bool success);

    /**
     * @brief 推理完成信号
     * @param result 包含推理结果的 QVariantMap
     */
    void inferFinished(const QVariantMap &result);

private:
    /**
     * @brief 启动异步模型加载
     * @param model_name 模型名称
     * @param model_path 模型文件路径
     * @param runtime 模型运行时
     * @param precision 推理精度
     */
    void startAsyncModelLoad(const QString &model_name, const QString &model_path,
                             const irt::model::ModelRuntime &runtime, irt::model::ModelPrecision precision);
    void ensureExecutorStarted();
    void setRunning(bool running);
    void setLoadingModel(bool loading_model);
    void setLastError(const QString &last_error);
    void onInferCompleted(quint64 request_id, QVariantMap result);

    QString cached_model_key_;     ///< 当前缓存模型的唯一标识
    QString loading_model_key_;    ///< 正在加载的模型标识
    bool    enabled_{false};       ///< 功能是否启用
    bool    running_{false};       ///< 是否正在运行
    bool    loading_model_{false}; ///< 是否正在加载模型
    QString last_error_;           ///< 最后一次错误信息
    QVariantMap last_result_;      ///< 最近一次推理结果

    QList<QPointer<::QThread>> worker_threads_;
    ModelLoader               model_loader_;
    PredictExecutor           predict_executor_;
    std::shared_ptr<std::atomic_bool> loading_cancellation_token_;
    std::shared_ptr<std::atomic_bool> infer_cancellation_token_;
    std::atomic_bool           shutting_down_{false};
    std::atomic_bool           shutdown_requested_{false};
    quint64                    current_request_id_{0};

    SmartAnnotationExecutor   *executor_{nullptr};
    ::QThread                 *executor_thread_{nullptr};
};

} // namespace dltool::feature
