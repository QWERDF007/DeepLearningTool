import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui

// 进度信息徽章组件
// 显示在 Footer 中，用于指示后端处理任务的进度状态
Rectangle {
    id: control
    width: 20
    height: 20
    
    // 属性定义
    property bool checked: false
    property bool hovered: mouseArea.containsMouse
    property color normalColor: checked ? DltColor.Highlight : DltColor.Primary
    property color hoverColor: Qt.lighter(normalColor, 1.2)
    
    // 背景颜色根据悬停状态变化
    color: hovered ? hoverColor : normalColor

    RowLayout {
        anchors.fill: parent
        
        // "进度" 文本标签
        DltText {
            Layout.leftMargin: 5
            text: "进度"
            verticalAlignment: Text.AlignVCenter
        }
        
        // 进度图标 - 根据运行状态切换
        DltTextIcon {
            id: progressIcon
            Layout.preferredWidth: 24
            Layout.preferredHeight: 24
            iconSource: ProgressManager.isRunning ? 
                       DltFontIcon.ProgressRingDots : 
                       DltFontIcon.CheckMark
            iconSize: 16
            
            // 旋转动画 - 仅在任务运行时显示
            RotationAnimator on rotation {
                running: ProgressManager.isRunning
                from: 0
                to: 360
                duration: 1000
                loops: Animation.Infinite
            }
        }
        
        // 进度百分比文本 - 仅在运行时显示
        DltText {
            Layout.preferredWidth: 32
            text: ProgressManager.progress + "%"
            visible: ProgressManager.isRunning
            verticalAlignment: Text.AlignVCenter
        }
        
        Item {
            Layout.fillWidth: true
        }
    }

    // 鼠标区域处理点击事件
    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        onClicked: {
            control.checked = !control.checked
        }
    }
}
