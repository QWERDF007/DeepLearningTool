#pragma once

#include "DataExport.h"
#include "common/Singleton.h"

#include <QList>
#include <QObject>
#include <QVariantMap>

namespace dltool::data {

class DATA_API DeepLearningMethod : public QObject
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
    };
    Q_ENUM(Method)

    Q_INVOKABLE QList<QVariantMap> getMethods() const
    {
        return MethodsList;
    }

    QList<int> getMethodTypes() const
    {
        return TypesList;
    }

    Q_INVOKABLE QString getMethodName(const int method)
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

    // clang-format off

    inline static const  QList<int> TypesList{
        Method::Classification,
        Method::Detection,
    };

    inline static const QList<QVariantMap> MethodsList{
        QVariantMap{{"name", "图像分类"}, {"method", Method::Classification}},
        QVariantMap{{"name", "目标检测"}, {"method", Method::Detection}},
        QVariantMap{{"name", "语义分割"}, {"method", Method::Segmentation}},
        QVariantMap{{"name", "姿态检测"}, {"method", Method::Pose}},
        QVariantMap{{"name", "OCR"}, {"method", Method::OCR}},
    };

    inline static const std::unordered_map<int, QString> MethodToName = {
        {Method::Classification, "图像分类"},
        {Method::Detection, "目标检测"},
        {Method::Segmentation, "语义分割"},
        {Method::Pose, "姿态检测"},
        {Method::OCR, "OCR"},
    };

    // clang-format on
};

} // namespace dltool::data
