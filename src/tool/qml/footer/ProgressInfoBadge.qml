import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import quickui

// 进度信息徽章组件
// 显示在 Footer 中，用于指示后端处理任务的进度状态
Rectangle {
    id: control
    width: 20
    height: 20
    
    // 属性定义
    property bool checked: false
    property bool hovered: mouseArea.containsMouse
    property color normalColor: checked ? QuiColor.Highlight : QuiColor.Primary
    property color hoverColor: Qt.lighter(normalColor, 1.2)
    
    // 背景颜色根据悬停状态变化
    color: hovered ? hoverColor : normalColor

    RowLayout {
        anchors.fill: parent
        
        // "进度" 文本标签
        QuiText {
            Layout.leftMargin: 5
            text: "进度"
            verticalAlignment: Text.AlignVCenter
        }
        
        // 进度百分比文本 - 仅在运行时显示
        QuiText {
            Layout.preferredWidth: 32
            text: ProgressManager ? ProgressManager.progress + "%" : ""
            visible: ProgressManager ? ProgressManager.isRunning : false
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
