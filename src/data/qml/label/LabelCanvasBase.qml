import QtQuick
import QtQuick.Controls

import dltool.core
import dltool.data

Item {
    id: labelView
    clip: true

    property DataManager dataManager
    readonly property ImageInstancesModel imageInstances: dataManager ? dataManager.imageInstances : null
    readonly property LabelClassesModel labelClasses: dataManager ? dataManager.labelClasses : null
    readonly property ImageLabelsListModel imageLabelsList: dataManager ? dataManager.imageLabelsList : null
    readonly property ItemSelectionModel selection: imageLabelsList ? imageLabelsList.selection : null
    readonly property color drawingColor: labelClasses ? labelClasses.currentLabelClassColor : "red"

    property point startPos: Qt.point(0, 0)
    property int toolMode: LabelCanvasEnums.SelectTool

    property alias imageView: labelImage
    property alias geometry: canvasGeometry
    property alias canvasMouseArea: mouseArea
    property alias interactionState: mouseArea.interactionState
    property alias activeData: mouseArea.activeData
    property alias suppressNextRelease: mouseArea.suppressNextRelease

    property var toolAvailableHandler: function(mode) {
        return mode === LabelCanvasEnums.SelectTool
    }
    property var toolModeChangedHandler: function() {}
    property var currentImageChangedHandler: function() {}
    property var drawingColorChangedHandler: function() {}
    property var mouseExitedHandler: function() {}
    property var toolKeyPressedHandler: function(event) { return false }
    property var toolMousePressedHandler: function(event) { return false }
    property var toolMouseReleasedHandler: function(event) { return false }
    property var toolMousePositionChangedHandler: function(event) { return false }
    property var toolMouseDoubleClickedHandler: function(event) { return false }

    onToolModeChanged: {
        resetInteractionState()
        toolModeChangedHandler()
    }

    onDrawingColorChanged: drawingColorChangedHandler()

    Connections {
        target: imageInstances
        function onCurrentImageChanged() {
            currentImageChangedHandler()
        }
    }

    LabelCanvasGeometry {
        id: canvasGeometry
        imageItem: labelImage.image
    }

    LabelImage {
        id: labelImage
        anchors.fill: parent
        currentImagePath: imageInstances ? imageInstances.currentImagePath : ""
    }

    Keys.onPressed: function(event) { handleKeyPressed(event) }
    Keys.onReleased: function(event) {
        if (event.key === Qt.Key_Control && !mouseArea.pressed) {
            mouseArea.cursorShape = Qt.ArrowCursor
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        property int interactionState: LabelCanvasEnums.Idle
        property var activeData: null
        property bool suppressNextRelease: false

        onPressed: function(event) { handleMousePressed(event) }
        onReleased: function(event) { handleMouseReleased(event) }
        onCanceled: function() { resetInteractionState() }
        onPositionChanged: function(event) { handleMousePositionChanged(event) }
        onEntered: labelView.forceActiveFocus()
        onExited: mouseExitedHandler()
        onDoubleClicked: function(event) { handleMouseDoubleClicked(event) }
    }

    Shortcut {
        enabled: labelView.visible
        sequence: "F1"
        onActivated: labelView.activateToolMode(LabelCanvasEnums.SelectTool)
    }

    function handleKeyPressed(event) {
        if (event.modifiers === Qt.NoModifier && event.key === Qt.Key_F1) {
            activateToolMode(LabelCanvasEnums.SelectTool)
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Control && !mouseArea.pressed) {
            mouseArea.cursorShape = Qt.OpenHandCursor
            return
        }
        if (toolKeyPressedHandler(event)) {
            return
        }
        handleLabelClassShortcut(event)
    }

    function handleLabelClassShortcut(event) {
        if (!labelClasses || !event.text || event.text.length <= 0) {
            return false
        }
        if (labelClasses.selectByShortcut(event.text)) {
            event.accepted = true
            return true
        }
        return false
    }

    function handleMousePressed(event) {
        if (mouseArea.interactionState !== LabelCanvasEnums.Idle) {
            return
        }
        if (event.button === Qt.MiddleButton
                || ((event.modifiers & Qt.ControlModifier) && event.button === Qt.LeftButton)) {
            mouseArea.cursorShape = Qt.ClosedHandCursor
            startPos = Qt.point(event.x, event.y)
            mouseArea.interactionState = LabelCanvasEnums.Dragging
            return
        }
        toolMousePressedHandler(event)
    }

    function handleMouseReleased(event) {
        if (mouseArea.interactionState === LabelCanvasEnums.Dragging) {
            finishImageDragging(event)
            return
        }
        toolMouseReleasedHandler(event)
    }

    function handleMousePositionChanged(event) {
        if (mouseArea.interactionState === LabelCanvasEnums.Dragging) {
            moveImage(event)
            return
        }
        toolMousePositionChangedHandler(event)
    }

    function handleMouseDoubleClicked(event) {
        toolMouseDoubleClickedHandler(event)
    }

    function finishImageDragging(event) {
        setIdleCursor(event.modifiers)
        startPos = Qt.point(event.x, event.y)
        mouseArea.interactionState = LabelCanvasEnums.Idle
    }

    function setIdleCursor(modifiers) {
        mouseArea.cursorShape = (modifiers & Qt.ControlModifier) ? Qt.OpenHandCursor : Qt.ArrowCursor
    }

    function moveImage(event) {
        let dx = event.x - startPos.x
        let dy = event.y - startPos.y
        labelImage.image.x += dx
        labelImage.image.y += dy
        startPos = Qt.point(event.x, event.y)
    }

    function resetInteractionState() {
        if (mouseArea.interactionState === LabelCanvasEnums.Dragging) {
            mouseArea.cursorShape = Qt.ArrowCursor
        }
        mouseArea.interactionState = LabelCanvasEnums.Idle
        mouseArea.suppressNextRelease = false
    }

    function isToolAvailable(mode) {
        return toolAvailableHandler(mode)
    }

    function setToolMode(mode) {
        toolMode = isToolAvailable(mode) ? mode : LabelCanvasEnums.SelectTool
    }

    function activateToolMode(mode) {
        if (!isToolAvailable(mode)) {
            return false
        }

        setToolMode(mode)
        forceActiveFocus()
        return true
    }

}
