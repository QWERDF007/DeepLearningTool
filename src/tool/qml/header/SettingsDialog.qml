import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

import dltool.settings
import dltool.ui
import quickui

Window {
    id: dialog

    property var imageSearch: null
    property var smartAnnotation: null
    property int currentGroupIndex: 0

    visible: false
    title: "设置"
    width: 980
    height: 720
    minimumWidth: 760
    minimumHeight: 520
    color: QuiColor.Background
    modality: Qt.NonModal
    flags: Qt.Window | Qt.CustomizeWindowHint | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowMaximizeButtonHint | Qt.WindowCloseButtonHint

    onClosing: GlobalSettings.save()

    Shortcut {
        sequence: "Esc"
        onActivated: dialog.close()
    }

    function screenGeometryFor(targetScreen) {
        if (targetScreen) {
            let availableWidth = targetScreen.desktopAvailableWidth > 0 ? targetScreen.desktopAvailableWidth : targetScreen.width
            let availableHeight = targetScreen.desktopAvailableHeight > 0 ? targetScreen.desktopAvailableHeight : targetScreen.height
            return Qt.rect(targetScreen.virtualX, targetScreen.virtualY, availableWidth, availableHeight)
        }
        return Qt.rect(x, y, width, height)
    }

    function centerInOwner() {
        let owner = transientParent
        let geometry = owner ? Qt.rect(owner.x, owner.y, owner.width, owner.height) : screenGeometryFor(dialog.screen)
        let nextX = Math.round(geometry.x + (geometry.width - width) / 2)
        let nextY = Math.round(geometry.y + (geometry.height - height) / 2)
        let screenGeometry = screenGeometryFor(owner ? owner.screen : dialog.screen)
        let maxX = Math.max(screenGeometry.x, screenGeometry.x + screenGeometry.width - width)
        let maxY = Math.max(screenGeometry.y, screenGeometry.y + screenGeometry.height - height)
        x = Math.max(screenGeometry.x, Math.min(nextX, maxX))
        y = Math.max(screenGeometry.y, Math.min(nextY, maxY))
    }

    function open() {
        if (!visible) {
            centerInOwner()
        }
        show()
        raise()
        requestActivate()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            spacing: 10

            QuiText {
                Layout.fillWidth: true
                text: "设置"
                font: QuiFont.Title
                color: QuiColor.FontPrimary
                elide: Text.ElideRight
            }

            QuiButton {
                Layout.preferredWidth: 84
                Layout.preferredHeight: 32
                text: "重置"
                onClicked: GlobalSettings.reset()
            }

            QuiButton {
                Layout.preferredWidth: 84
                Layout.preferredHeight: 32
                text: "保存"
                normalColor: QuiColor.Highlight
                onClicked: GlobalSettings.save()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: QuiColor.Border
            opacity: 0.7
        }

        TabBar {
            id: tabBar

            Layout.fillWidth: true
            Layout.preferredHeight: 46
            currentIndex: dialog.currentGroupIndex
            spacing: 1
            onCurrentIndexChanged: dialog.currentGroupIndex = currentIndex

            background: Rectangle {
                color: QuiColor.Primary
            }

            contentItem: ListView {
                id: tabList

                model: tabBar.contentModel
                currentIndex: tabBar.currentIndex
                orientation: ListView.Horizontal
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.AutoFlickIfNeeded
                clip: true
                spacing: tabBar.spacing

                highlightFollowsCurrentItem: false
                highlightMoveDuration: 0
                highlight: Rectangle {
                    x: tabList.currentItem ? tabList.currentItem.x : 0
                    y: tabList.height - height
                    width: tabList.currentItem ? tabList.currentItem.width : 0
                    height: 2
                    color: QuiColor.Highlight

                    Behavior on x {
                        SmoothedAnimation {
                            duration: 220
                            velocity: 500
                        }
                    }
                }
            }

            Repeater {
                model: GlobalSettings.catalog

                QuiTabButton {
                    required property int index
                    required property string label

                    width: Math.max(118, implicitWidth + 24)
                    height: 44
                    text: label
                    textColor: tabBar.currentIndex === index ? QuiColor.Highlight : QuiColor.FontPrimary
                    focusPolicy: Qt.NoFocus
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: Math.max(0, Math.min(dialog.currentGroupIndex, GlobalSettings.catalog.count - 1))

            Repeater {
                model: GlobalSettings.catalog

                QuiScrollablePage {
                    id: page

                    required property var fieldModel

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    animationEnabled: false
                    padding: 0

                    ColumnLayout {
                        width: parent.width
                        spacing: 12

                        Item {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 12
                        }

                        SettingsFieldsPanel {
                            fieldModel: page.fieldModel
                        }

                        Item {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 16
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: QuiColor.Border
            opacity: 0.7
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            spacing: 10

            Item {
                Layout.fillWidth: true
            }

            QuiButton {
                Layout.preferredWidth: 84
                Layout.preferredHeight: 32
                text: "关闭"
                onClicked: dialog.close()
            }
        }
    }
}
