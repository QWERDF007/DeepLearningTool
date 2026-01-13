import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data

Rectangle {
    id: control
    width: parent ? parent.width : 200
    height: 32
    color: backgroundColor
    
    property string className: ""
    property string classShortcut: ""
    property color classColor: "black"
    property color backgroundColor: Qt.lighter(DltColor.Primary, 1.2)
    property int classId: -1
    property int ordinalIndex: -1
    property var listView
    property LabelClassesModel labelClasses

    signal editClicked
    signal deleteClicked
    signal clicked

    // 拖拽状态
    property bool held: false
    property real dragStartY: 0
    property int dragStartIndex: -1
    
    // 拖拽时的视觉效果
    z: held ? 100 : 1
    opacity: held ? 0.9 : 1.0

    Drag.active: held
    Drag.source: control
    Drag.hotSpot.x: width / 2
    Drag.hotSpot.y: height / 2

    // 拖拽区域放在最底层
    MouseArea {
        id: dragArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        hoverEnabled: true
        drag.target: held ? control : undefined
        drag.axis: Drag.YAxis
        pressAndHoldInterval: 200
        
        onClicked: function(mouse) {
            control.clicked()
        }
        
        onPressAndHold: function(mouse) {
            // 如果未选中，先选中该项
            control.clicked()
            
            control.dragStartIndex = control.ordinalIndex
            control.dragStartY = control.y
            control.held = true
        }
        
        onReleased: function(mouse) {
            if (control.held) {
                control.held = false
                
                if (!listView || !labelClasses) {
                    control.y = control.dragStartY
                    return
                }
                
                // 使用 mapToItem 获取在 contentItem 中的绝对位置
                let posInList = control.mapToItem(listView.contentItem, 0, 0)
                let itemHeight = control.height + listView.spacing
                
                // 计算拖拽项中心位置
                let dragCenterY = posInList.y + control.height / 2
                
                // 计算目标位置：拖拽项中心超过目标项中心才交换
                let targetIndex = control.dragStartIndex
                
                if (dragCenterY < control.dragStartIndex * itemHeight + itemHeight / 2) {
                    // 向上拖拽：找到第一个中心位置大于拖拽项中心的项
                    for (let i = control.dragStartIndex - 1; i >= 0; i--) {
                        let targetCenterY = i * itemHeight + itemHeight / 2
                        if (dragCenterY < targetCenterY) {
                            targetIndex = i
                        } else {
                            break
                        }
                    }
                } else if (dragCenterY > control.dragStartIndex * itemHeight + itemHeight / 2) {
                    // 向下拖拽：找到最后一个中心位置小于拖拽项中心的项
                    for (let i = control.dragStartIndex + 1; i < listView.count; i++) {
                        let targetCenterY = i * itemHeight + itemHeight / 2
                        if (dragCenterY > targetCenterY) {
                            targetIndex = i
                        } else {
                            break
                        }
                    }
                }
                
                // 边界检查
                targetIndex = Math.max(0, Math.min(listView.count - 1, targetIndex))
                
                // 重置位置
                control.y = control.dragStartY
                
                // 执行重排序
                if (targetIndex !== control.dragStartIndex) {
                    labelClasses.reorderLabelClass(control.classId, targetIndex)
                }
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
    
    // 按钮放在最上层，可以接收点击事件
    RowLayout {
        anchors {
            top: parent.top
            bottom: parent.bottom
            right: parent.right
            rightMargin: 5
        }
        spacing: 3
        DltTextIconButton {
            iconSource: DltFontIcon.Edit
            onClicked: control.editClicked()
            normalColor: control.backgroundColor
        }
        DltTextIconButton {
            iconSource: DltFontIcon.Delete
            onClicked: control.deleteClicked()
            normalColor: control.backgroundColor
        }
    }
}
