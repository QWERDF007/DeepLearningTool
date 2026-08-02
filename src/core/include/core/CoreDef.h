#pragma once

#include "common/Singleton.h"
#include "dltool/core/Export.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQml>
#include <unordered_map>

namespace dltool::core {

class CORE_API DeepLearningMethod : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(DeepLearningMethod)
    QT_QML_SINGLETON(DeepLearningMethod)
public:
    enum Method
    {
        Classification = 0,
        Detection,
        Segmentation,
        Pose,
        OCR,
        AnomalyDetection,
        Unknown,
    };
    Q_ENUM(Method)

    Q_INVOKABLE QList<QVariantMap> getMethods() const
    {
        return methodItems();
    }

    QList<int> getMethodTypes() const
    {
        return supportedMethodTypes();
    }

    Q_INVOKABLE QString getMethodName(const int method) const
    {
        return methodName(method);
    }

    static const QList<QVariantMap> &methodItems()
    {
        return MethodsList;
    }

    static const QList<int> &supportedMethodTypes()
    {
        return TypesList;
    }

    static bool isSupportedMethod(const int method)
    {
        return TypesList.contains(method);
    }

    static QString methodName(const int method)
    {
        auto found = MethodToName.find(method);
        if (found == MethodToName.end())
            return QString();
        return found->second;
    }

private:
    explicit DeepLearningMethod(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~DeepLearningMethod() {}

    inline static const QList<int> TypesList{
        Method::Classification,
        Method::Detection,
        Method::Segmentation,
        Method::AnomalyDetection,
    };

    inline static const QList<QVariantMap> MethodsList{
        QVariantMap{{"name", QString("图像分类")},     {"method", Method::Classification}},
        QVariantMap{{"name", QString("目标检测")},          {"method", Method::Detection}},
        QVariantMap{{"name", QString("语义分割")},       {"method", Method::Segmentation}},
        QVariantMap{{"name", QString("异常检测")}, {"method", Method::AnomalyDetection}},
        QVariantMap{{"name", QString("姿态检测")},               {"method", Method::Pose}},
        QVariantMap{     {"name", QString("OCR")},                {"method", Method::OCR}},
    };

    inline static const std::unordered_map<int, QString> MethodToName = {
        {    Method::Classification, QString("图像分类")},
        {         Method::Detection, QString("目标检测")},
        {      Method::Segmentation, QString("语义分割")},
        {  Method::AnomalyDetection, QString("异常检测")},
        {              Method::Pose, QString("姿态检测")},
        {               Method::OCR,      QString("OCR")},
    };
};

} // namespace dltool::core
