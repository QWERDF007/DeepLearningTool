#pragma once

#include <QObject>
#include <QtQml>

#define Q_PROPERTY_AUTO(TYPE, M)                     \
    Q_PROPERTY(TYPE M MEMBER _##M NOTIFY M##Changed) \
public:                                              \
    Q_SIGNAL void M##Changed();                      \
                                                     \
    void M(TYPE in_##M)                              \
    {                                                \
        _##M = in_##M;                               \
        Q_EMIT M##Changed();                         \
    }                                                \
                                                     \
    TYPE M()                                         \
    {                                                \
        return _##M;                                 \
    }                                                \
                                                     \
private:                                             \
    TYPE _##M;

namespace dltool::ui { namespace button {
Q_NAMESPACE

enum ButtonFlag
{
    NeutralButton  = 0x0001,
    NegativeButton = 0x0002,
    PositiveButton = 0x0004
};
Q_ENUM_NS(ButtonFlag)
QML_NAMED_ELEMENT(DltDialogButtonFlag)
}} // namespace dltool::ui::button
