#pragma once

#include "dltool/data/Export.h"

#include <QObject>
#include <QtQml>

namespace dltool::data {

class DATA_API LabelCanvasEnums : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(LabelCanvasEnums)
    QML_UNCREATABLE("LabelCanvasEnums only provides enum values.")

public:
    enum ToolMode
    {
        SelectTool    = 0,
        RectangleTool = 1,
        PolygonTool   = 2,
        SmartTool     = 3,
    };
    Q_ENUM(ToolMode)

    enum InteractionState
    {
        Idle      = 0,
        ReadyDraw = 1,
        Drawing   = 2,
        Dragging  = 3,
        ReadyEdit = 4,
        Editing   = 5,
    };
    Q_ENUM(InteractionState)

    enum SmartPromptLabel
    {
        BackgroundPrompt = 0,
        ForegroundPrompt = 1,
    };
    Q_ENUM(SmartPromptLabel)
};

} // namespace dltool::data
