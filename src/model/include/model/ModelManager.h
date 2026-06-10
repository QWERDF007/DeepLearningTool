#pragma once

#include <QObject>
#include <QtQml>

namespace dltool::model {

class ModelManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ModelManager)
    QML_UNCREATABLE("Can not create ModelManager directly!")
public:
};

} // namespace dltool::model