import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.project

Rectangle {
    id: header
    width: 600
    height: 80
    color: DltColor.Primary

    property alias currentIndex: mainTabBar.currentIndex

    Connections {
        target: SignalHelper
        function onChangeTabBarIndex(index) {
            mainTabBar.currentIndex = index
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 5
        anchors.rightMargin: 5
        RowLayout {
            Layout.fillWidth: true
            // Layout.fillHeight: true
            Layout.preferredHeight: 36
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
                        height: 32
                        text: modelData
                        textColor: mainTabBar.currentIndex === index ? DltColor.Highlight : "white"
                        focusPolicy: Qt.NoFocus
                        enabled: modelData === "项目" || (ProjectManager.currentProject ? true : false)
                    }
                }
            }
            Rectangle { // splitter
                color: DltColor.Background
                width: 5
                Layout.fillHeight: true
            }
            ToolBar {
                Layout.fillHeight: true
                background: Rectangle {
                    color: DltColor.Primary
                }

                RowLayout {
                    anchors.verticalCenter: parent.verticalCenter
                    DltTextIconButton {
                        iconSource: DltFontIcon.Help
                        text: "帮助"
                    }
                    DltTextIconButton {
                        iconSource: DltFontIcon.Settings
                        text: "设置"
                    }
                }
            }
        }
        RowLayout { // TODO: 过滤栏
            id: filterBar
            Layout.fillWidth: true
            Layout.fillHeight: true
            ToolBar {
                Layout.fillHeight: true
                background: Rectangle {
                    color: DltColor.Primary
                }
                RowLayout {
                    DltTextIconButton {
                        iconSource: DltFontIcon.Help
                        text: "帮助"
                    }
                    DltTextIconButton {
                        iconSource: DltFontIcon.Settings
                        text: "设置"
                    }
                }
            }
        }
    }
}
