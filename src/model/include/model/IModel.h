#pragma once

#include "IModelConfig.h"
#include "dltool/model/Export.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQml>
#include <memory>

namespace dltool::model {

/**
 * @brief 模型抽象接口，持有一个 IModelConfig 及训练/验证/测试数据集视图
 */
class MODEL_API IModel : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(IModel)
    QML_UNCREATABLE("IModel is an abstract interface")
    Q_PROPERTY(QString uuid READ uuid WRITE setUuid NOTIFY uuidChanged FINAL)
    Q_PROPERTY(int method READ method CONSTANT FINAL)
    Q_PROPERTY(QString frameworkName READ frameworkName CONSTANT FINAL)
    Q_PROPERTY(QString modelArchitecture READ modelArchitecture CONSTANT FINAL)
    Q_PROPERTY(QString typeName READ typeName CONSTANT FINAL)
    Q_PROPERTY(dltool::model::IModelConfig *config READ config CONSTANT FINAL)
    Q_PROPERTY(QObject *trainDatasetViewModel READ trainDatasetViewModel WRITE setTrainDatasetViewModel NOTIFY
                   trainDatasetViewModelChanged FINAL)
    Q_PROPERTY(QObject *validationDatasetViewModel READ validationDatasetViewModel WRITE setValidationDatasetViewModel
                   NOTIFY validationDatasetViewModelChanged FINAL)
    Q_PROPERTY(QObject *testDatasetViewModel READ testDatasetViewModel WRITE setTestDatasetViewModel NOTIFY
                   testDatasetViewModelChanged FINAL)

public:
    /**
     * @brief 构造模型
     * @param config 模型配置
     * @param parent 父对象
     */
    explicit IModel(std::unique_ptr<IModelConfig> config, QObject *parent = nullptr);
    ~IModel() override;

    /**
     * @brief 获取模型配置（可修改）
     * @return 模型配置指针
     */
    IModelConfig *config();

    /**
     * @brief 获取模型配置（只读）
     * @return 模型配置指针
     */
    const IModelConfig *config() const;

    /**
     * @brief 获取模型 UUID
     * @return UUID 字符串
     */
    QString uuid() const;

    /**
     * @brief 设置模型 UUID
     * @param uuid 新的 UUID
     */
    void setUuid(const QString &uuid);

    /**
     * @brief 获取训练数据集视图模型
     * @return 视图模型指针
     */
    QObject *trainDatasetViewModel() const;

    /**
     * @brief 设置训练数据集视图模型
     * @param view_model 视图模型
     */
    void setTrainDatasetViewModel(QObject *view_model);

    /**
     * @brief 获取验证数据集视图模型
     * @return 视图模型指针
     */
    QObject *validationDatasetViewModel() const;

    /**
     * @brief 设置验证数据集视图模型
     * @param view_model 视图模型
     */
    void setValidationDatasetViewModel(QObject *view_model);

    /**
     * @brief 获取测试数据集视图模型
     * @return 视图模型指针
     */
    QObject *testDatasetViewModel() const;

    /**
     * @brief 设置测试数据集视图模型
     * @param view_model 视图模型
     */
    void setTestDatasetViewModel(QObject *view_model);

    /**
     * @brief 获取深度学习方法枚举值
     * @return 方法枚举值
     */
    virtual int method() const = 0;

    /**
     * @brief 获取框架名称
     * @return 框架名称
     */
    virtual QString frameworkName() const = 0;

    /**
     * @brief 获取模型架构名称
     * @return 模型架构名称
     */
    virtual QString modelArchitecture() const = 0;

    /**
     * @brief 获取类型名称
     * @return 类型名称
     */
    virtual QString typeName() const = 0;

    /**
     * @brief 克隆模型对象
     * @return 克隆后的模型实例
     */
    virtual std::unique_ptr<IModel> clone() const = 0;

signals:
    void uuidChanged();
    void trainDatasetViewModelChanged();
    void validationDatasetViewModelChanged();
    void testDatasetViewModelChanged();

protected:
    std::unique_ptr<IModelConfig> config_;                        ///< 模型配置
    QString                       uuid_;                          ///< 模型唯一标识
    QPointer<QObject>             train_dataset_view_model_;      ///< 训练数据集视图模型
    QPointer<QObject>             validation_dataset_view_model_; ///< 验证数据集视图模型
    QPointer<QObject>             test_dataset_view_model_;       ///< 测试数据集视图模型
};

} // namespace dltool::model
