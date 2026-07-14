#pragma once

#include "IParams.h"
#include "dltool/model/Export.h"

#include <QObject>
#include <QString>
#include <QtQml>
#include <memory>

namespace dltool::model {

/**
 * @brief 模型配置抽象接口，定义模型的方法、框架、架构等元信息及训练/测试参数访问
 */
class MODEL_API IModelConfig : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(IModelConfig)
    QML_UNCREATABLE("IModelConfig is an abstract interface")
    Q_PROPERTY(int method READ method CONSTANT FINAL)
    Q_PROPERTY(QString frameworkName READ frameworkName CONSTANT FINAL)
    Q_PROPERTY(QString modelArchitecture READ modelArchitecture CONSTANT FINAL)
    Q_PROPERTY(QString typeName READ typeName CONSTANT FINAL)
    Q_PROPERTY(dltool::model::ITrainParams *trainParams READ trainParams CONSTANT FINAL)
    Q_PROPERTY(dltool::model::ITestParams *testParams READ testParams CONSTANT FINAL)

public:
    /**
     * @brief 构造模型配置
     * @param parent 父对象
     */
    explicit IModelConfig(QObject *parent = nullptr);
    ~IModelConfig() override;

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
     * @brief 获取训练参数（可修改）
     * @return 训练参数指针，默认返回 nullptr
     */
    virtual ITrainParams *trainParams();

    /**
     * @brief 获取测试参数（可修改）
     * @return 测试参数指针，默认返回 nullptr
     */
    virtual ITestParams *testParams();

    /**
     * @brief 获取训练参数（只读）
     * @return 训练参数指针，默认返回 nullptr
     */
    virtual const ITrainParams *trainParams() const;

    /**
     * @brief 获取测试参数（只读）
     * @return 测试参数指针，默认返回 nullptr
     */
    virtual const ITestParams *testParams() const;

    /**
     * @brief 克隆配置对象
     * @return 克隆后的配置实例
     */
    virtual std::unique_ptr<IModelConfig> clone() const = 0;
};

} // namespace dltool::model
