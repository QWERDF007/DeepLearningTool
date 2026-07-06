import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import quickui

Rectangle {
    id: control
    width: parent ? parent.width : 200
    height: 32
    color: backgroundColor

    property string className: ""
    property string classShortcut: ""
    property color classColor: "black"
    property color backgroundColor: Qt.lighter(QuiColor.Primary, 1.2)
    property int classId: -1
    property int ordinalIndex: -1
    property var listView
    property LabelClassesModel labelClasses
    property bool dragEnabled: false

    signal editClicked
    signal deleteClicked
    signal clicked

    function getContrastColor(bgColor) {
        var r = bgColor.r
        var g = bgColor.g
        var b = bgColor.b
        var luminance = 0.299 * r + 0.587 * g + 0.114 * b
        return luminance > 0.5 ? "black" : "white"
    }

    property bool held: false
    property real dragStartY: 0
    property int dragStartIndex: -1

    z: held ? 100 : 1
    opacity: held ? 0.9 : 1.0

    Drag.active: held && dragEnabled
    Drag.source: control
    Drag.hotSpot.x: width / 2
    Drag.hotSpot.y: height / 2

    MouseArea {
        id: dragArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        hoverEnabled: true
        drag.target: held && control.dragEnabled ? control : undefined
        drag.axis: Drag.YAxis
        pressAndHoldInterval: 200

        onClicked: function(mouse) {
            control.clicked()
        }

        onPressAndHold: function(mouse) {
            if (!control.dragEnabled) {
                return
            }

            control.clicked()
            control.dragStartIndex = control.ordinalIndex
            control.dragStartY = control.y
            control.held = true
        }

        onReleased: function(mouse) {
            if (!control.held) {
                return
            }

            control.held = false

            if (!listView || !labelClasses) {
                control.y = control.dragStartY
                return
            }

            let posInList = control.mapToItem(listView.contentItem, 0, 0)
            let itemHeight = control.height + listView.spacing
            let dragCenterY = posInList.y + control.height / 2
            let targetIndex = control.dragStartIndex

            if (dragCenterY < control.dragStartIndex * itemHeight + itemHeight / 2) {
                for (let i = control.dragStartIndex - 1; i >= 0; i--) {
                    let targetCenterY = i * itemHeight + itemHeight / 2
                    if (dragCenterY < targetCenterY) {
                        targetIndex = i
                    } else {
                        break
                    }
                }
            } else if (dragCenterY > control.dragStartIndex * itemHeight + itemHeight / 2) {
                for (let i = control.dragStartIndex + 1; i < listView.count; i++) {
                    let targetCenterY = i * itemHeight + itemHeight / 2
                    if (dragCenterY > targetCenterY) {
                        targetIndex = i
                    } else {
                        break
                    }
                }
            }

            targetIndex = Math.max(0, Math.min(listView.count - 1, targetIndex))
            control.y = control.dragStartY

            if (targetIndex !== control.dragStartIndex) {
                labelClasses.reorderLabelClass(control.classId, targetIndex)
            }
        }

        onCanceled: {
            if (control.held) {
                control.held = false
                control.y = control.dragStartY
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 5

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: height
            radius: 3
            color: control.classColor
            border.width: 1
            border.color: "black"

            QuiText {
                text: control.classShortcut
                color: control.getContrastColor(control.classColor)
                anchors.centerIn: parent
            }
        }

        QuiText {
            text: control.className
            Layout.fillWidth: true
        }
    }

    RowLayout {
        anchors {
            top: parent.top
            bottom: parent.bottom
            right: parent.right
            rightMargin: 5
        }
        spacing: 3

        QuiTextIconButton {
            iconSource: QuiFontIcon.Edit
            onClicked: control.editClicked()
            normalColor: control.backgroundColor
        }

        QuiTextIconButton {
            iconSource: QuiFontIcon.Delete
            onClicked: control.deleteClicked()
            normalColor: control.backgroundColor
        }
    }
}
