#pragma once

#include "IModelConfig.h"
#include "dltool/model/Export.h"

#include <QObject>
#include <QString>
#include <QtQml>

#include <memory>

namespace dltool::model {

class MODEL_API IModel : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(IModel)
    QML_UNCREATABLE("IModel is an abstract interface")
    Q_PROPERTY(int method READ method CONSTANT FINAL)
    Q_PROPERTY(QString typeName READ typeName CONSTANT FINAL)
    Q_PROPERTY(dltool::model::IModelConfig *config READ config CONSTANT FINAL)

public:
    explicit IModel(std::unique_ptr<IModelConfig> config, QObject *parent = nullptr);
    ~IModel() override;

    IModelConfig *config();
    const IModelConfig *config() const;

    virtual int method() const = 0;
    virtual QString typeName() const = 0;
    virtual std::unique_ptr<IModel> clone() const = 0;

protected:
    std::unique_ptr<IModelConfig> config_;
};

} // namespace dltool::model
