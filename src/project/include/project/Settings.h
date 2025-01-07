#pragma once

#include "common/Singleton.h"

namespace dltool::project {

class Settings : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Settings)
    QT_QML_SINGLETON(Settings)
    // clang-format off
    Q_PROPERTY(double imageScale READ imageScale WRITE setImageScale NOTIFY imageScaleChanged)
    // clang-format on
public:
    double imageScale() const
    {
        return image_scale_;
    }

    void setImageScale(double scale)
    {
        image_scale_ = scale;
        emit imageScaleChanged();
    }

private:
    explicit Settings(QObject *parent = nullptr);
    ~Settings();

    double image_scale_{1.0};

signals:
    void imageScaleChanged();
};

} // namespace dltool::project
