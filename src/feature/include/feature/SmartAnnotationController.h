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
}

namespace dltool::feature {

class FEATURE_API SmartAnnotationController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SmartAnnotationController)
    QML_UNCREATABLE("Can not create SmartAnnotationController directly!")

    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged FINAL)
    Q_PROPERTY(bool loadingModel READ isLoadingModel NOTIFY loadingModelChanged FINAL)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged FINAL)

public:
    explicit SmartAnnotationController(QObject *parent = nullptr);
    ~SmartAnnotationController() override;

    bool isRunning() const
    {
        return running_;
    }

    bool isLoadingModel() const
    {
        return loading_model_;
    }

    QString lastError() const
    {
        return last_error_;
    }

    Q_INVOKABLE QStringList supportedModelPresets() const;
    Q_INVOKABLE QString     suggestedModelPath(const QString &model_name, const QString &backend) const;

    Q_INVOKABLE QVariantMap infer(const QString &image_path, const QVariantList &prompt_points);
    Q_INVOKABLE void        clearCache();

signals:
    void runningChanged();
    void loadingModelChanged();
    void lastErrorChanged();
    void modelLoadFinished(bool success);

private:
    bool ensureModel(const QString &model_name, const QString &model_path, const QString &backend,
                     const QString &device);
    void startAsyncModelLoad(const QString &model_name, const QString &model_path, const QString &backend,
                             const QString &device);
    void setRunning(bool running);
    void setLoadingModel(bool loading_model);
    void setLastError(const QString &last_error);

    std::unique_ptr<irt::model::IModel> model_;
    QString                             cached_model_key_;
    QString                             loading_model_key_;
    bool                                running_{false};
    bool                                loading_model_{false};
    QString                             last_error_;
};

} // namespace dltool::feature
