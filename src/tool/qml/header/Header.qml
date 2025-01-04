import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.project

Rectangle {
    id: header
    width: 600
    height: 48
    color: DltColor.Primary

    property alias currentIndex: mainTabBar.currentIndex

    ColumnLayout {
        anchors.fill: parent
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            MenuTabBar {
                id: mainTabBar
                Layout.fillWidth: true
                Layout.fillHeight: true
                Repeater {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: ["项目", "图库", "标注", "检查", "训练", "评估", "导出"]
                    delegate: DltTabButton {
                        id: tbtn
                        width: 100
                        height: header.height
                        text: modelData
                        textColor: mainTabBar.currentIndex === index ? DltColor.Highlight : "white"
                        focusPolicy: Qt.NoFocus
                        enabled: modelData === "项目" || (ProjectManager.currentProject ? true : false)
                    }
                }
            }
        }
    }
}
