#pragma once

#include "dltool/feature/Export.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml>
#include <memory>

namespace irt::model {
class IModel;
enum class ModelBackend;
enum class ModelDevice;
} // namespace irt::model

namespace dltool::feature {

class FEATURE_API SmartAnnotationController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SmartAnnotationController)
    QML_UNCREATABLE("Can not create SmartAnnotationController directly!")

    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged FINAL)
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged FINAL)
    Q_PROPERTY(bool loadingModel READ isLoadingModel NOTIFY loadingModelChanged FINAL)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged FINAL)

public:
    /**
     * @brief 构造函数
     * @param parent 父对象
     */
    explicit SmartAnnotationController(QObject *parent = nullptr);
    ~SmartAnnotationController() override;

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
     * @brief 执行智能标注推理
     * @param image_path 输入图像路径
     * @param prompt_points 提示点列表
     * @return 包含推理结果的 QVariantMap
     */
    Q_INVOKABLE QVariantMap infer(const QString &image_path, const QVariantList &prompt_points);

    /// 清除模型缓存
    Q_INVOKABLE void clearCache();

signals:
    void enabledChanged();
    void runningChanged();
    void loadingModelChanged();
    void lastErrorChanged();

    /**
     * @brief 模型加载完成信号
     * @param success 加载是否成功
     */
    void modelLoadFinished(bool success);

private:
    /**
     * @brief 启动异步模型加载
     * @param model_name 模型名称
     * @param model_path 模型文件路径
     * @param backend 推理后端
     * @param device 推理设备
     */
    void startAsyncModelLoad(const QString &model_name, const QString &model_path, irt::model::ModelBackend backend,
                             irt::model::ModelDevice device);
    void setRunning(bool running);
    void setLoadingModel(bool loading_model);
    void setLastError(const QString &last_error);

    std::unique_ptr<irt::model::IModel> model_; ///< 加载的推理模型实例

    QString cached_model_key_;     ///< 当前缓存模型的唯一标识
    QString loading_model_key_;    ///< 正在加载的模型标识
    bool    enabled_{false};       ///< 功能是否启用
    bool    running_{false};       ///< 是否正在运行
    bool    loading_model_{false}; ///< 是否正在加载模型
    QString last_error_;           ///< 最后一次错误信息
};

} // namespace dltool::feature
