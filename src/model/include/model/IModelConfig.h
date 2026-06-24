#pragma once

#include "IParams.h"
#include "dltool/model/Export.h"

#include <QObject>
#include <QString>
#include <QtQml>
#include <memory>

namespace dltool::model {

class MODEL_API IModelConfig : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(IModelConfig)
    QML_UNCREATABLE("IModelConfig is an abstract interface")
    Q_PROPERTY(int method READ method CONSTANT FINAL)
    Q_PROPERTY(QString typeName READ typeName CONSTANT FINAL)
    Q_PROPERTY(dltool::model::ITrainParams *trainParams READ trainParams CONSTANT FINAL)
    Q_PROPERTY(dltool::model::ITestParams *testParams READ testParams CONSTANT FINAL)

public:
    explicit IModelConfig(QObject *parent = nullptr);
    ~IModelConfig() override;

    virtual int                           method() const   = 0;
    virtual QString                       typeName() const = 0;
    virtual ITrainParams                 *trainParams();
    virtual ITestParams                  *testParams();
    virtual const ITrainParams           *trainParams() const;
    virtual const ITestParams            *testParams() const;
    virtual std::unique_ptr<IModelConfig> clone() const = 0;
};

} // namespace dltool::model
