import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data

Rectangle {
    id: control
    width: parent.width
    height: 32
    color: Qt.lighter(DltColor.Primary, 1.2)
    property string className: ""
    property string classShortcut: ""
    property color classColor: "black"
    property int classId
    property int ordinalIndex
    property var listView
    property LabelClassesModel labelClasses

    signal editClicked
    signal deleteClicked
    signal clicked

    MouseArea {
        id: dragArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        hoverEnabled: true
        drag.axis: Drag.YAxis
        drag.target: control
        onClicked: function(mouse) {
            control.clicked()
        }
        onReleased: function(mouse) {
            // 计算拖拽释放位置对应的目标索引，并请求重排
            if (!listView || !labelClasses)
                return
            let pos = control.mapToItem(listView.contentItem, 0, mouse.y)
            let itemSpan = control.height + listView.spacing
            let newOrdinalIndex = Math.max(0, Math.min(listView.count - 1, Math.floor(pos.y / itemSpan)))
            if (newOrdinalIndex === control.ordinalIndex)
                return
            labelClasses.reorderLabelClass(control.classId, newOrdinalIndex)
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
            DltText {
                text: control.classShortcut
                color: "black"
                anchors.centerIn: parent
            }
        }
        DltText {
            text: control.className
            Layout.fillWidth: true
        }
    }
    RowLayout {
        anchors{
            top: parent.top
            bottom: parent.bottom
            right: parent.right
            rightMargin: 5
            // margins: 5
        }
        spacing: 3
        DltTextIconButton {
            iconSource: DltFontIcon.Edit
            onClicked: {
                control.editClicked()
            }
            normalColor: control.color
        }
        DltTextIconButton {
            iconSource: DltFontIcon.Delete
            onClicked: {
                control.deleteClicked()
            }
            normalColor: control.color
        }
    }
}
