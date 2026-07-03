#pragma once

#include "IModelConfig.h"
#include "dltool/model/Export.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQml>
#include <memory>

namespace dltool::model {

class MODEL_API IModel : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(IModel)
    QML_UNCREATABLE("IModel is an abstract interface")
    Q_PROPERTY(QString uuid READ uuid WRITE setUuid NOTIFY uuidChanged FINAL)
    Q_PROPERTY(int method READ method CONSTANT FINAL)
    Q_PROPERTY(QString typeName READ typeName CONSTANT FINAL)
    Q_PROPERTY(dltool::model::IModelConfig *config READ config CONSTANT FINAL)
    Q_PROPERTY(QObject *trainDatasetViewModel READ trainDatasetViewModel WRITE setTrainDatasetViewModel NOTIFY
                   trainDatasetViewModelChanged FINAL)
    Q_PROPERTY(QObject *validationDatasetViewModel READ validationDatasetViewModel WRITE setValidationDatasetViewModel
                   NOTIFY validationDatasetViewModelChanged FINAL)
    Q_PROPERTY(QObject *testDatasetViewModel READ testDatasetViewModel WRITE setTestDatasetViewModel NOTIFY
                   testDatasetViewModelChanged FINAL)

public:
    explicit IModel(std::unique_ptr<IModelConfig> config, QObject *parent = nullptr);
    ~IModel() override;

    IModelConfig       *config();
    const IModelConfig *config() const;

    QString uuid() const;
    void    setUuid(const QString &uuid);

    QObject *trainDatasetViewModel() const;
    void     setTrainDatasetViewModel(QObject *view_model);

    QObject *validationDatasetViewModel() const;
    void     setValidationDatasetViewModel(QObject *view_model);

    QObject *testDatasetViewModel() const;
    void     setTestDatasetViewModel(QObject *view_model);

    virtual int                     method() const   = 0;
    virtual QString                 typeName() const = 0;
    virtual std::unique_ptr<IModel> clone() const    = 0;

signals:
    void uuidChanged();
    void trainDatasetViewModelChanged();
    void validationDatasetViewModelChanged();
    void testDatasetViewModelChanged();

protected:
    std::unique_ptr<IModelConfig> config_;
    QString                       uuid_;
    QPointer<QObject>             train_dataset_view_model_;
    QPointer<QObject>             validation_dataset_view_model_;
    QPointer<QObject>             test_dataset_view_model_;
};

} // namespace dltool::model
