import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Qt.labs.qmlmodels

import dltool.model
import dltool.project
import dltool.ui
import quickui

Window {
    id: dialog

    property var taskManager: TaskManager
    property var taskModel: taskManager ? taskManager.tasks : null
    property int selectedTaskId: -1

    visible: false
    title: "任务管理中心"
    width: 1120
    height: 640
    minimumWidth: 900
    minimumHeight: 480
    color: QuiColor.Primary
    modality: Qt.NonModal
    flags: Qt.Window | Qt.CustomizeWindowHint | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowMaximizeButtonHint | Qt.WindowCloseButtonHint

    Shortcut {
        sequence: "Esc"
        onActivated: dialog.close()
    }

    Connections {
        target: ProjectManager
        function onCurrentProjectChanged() {
            if (!ProjectManager.currentProject) {
                dialog.selectedTaskId = -1
                dialog.close()
            }
        }
    }

    Connections {
        target: dialog.taskModel
        function onCountChanged() {
            if (!dialog.taskModel || dialog.taskModel.count === 0) {
                dialog.selectedTaskId = -1
            }
        }
    }

    function taskRowSelected(taskId) {
        return dialog.selectedTaskId >= 0 && dialog.selectedTaskId === taskId
    }

    function taskRowColor(taskId, row) {
        if (taskRowSelected(taskId)) {
            return QuiColor.Highlight
        }
        return Qt.lighter(QuiColor.Primary, 1.3)
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
        if (!ProjectManager.currentProject) {
            return
        }
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
                text: "任务管理中心"
                font: QuiFont.Title
                elide: Text.ElideRight
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: QuiColor.Background
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 16

            QuiTableView {
                id: tableView
                anchors.fill: parent
                model: dialog.taskModel
                rowHeight: 42
                headerHeight: 34
                headerColor: QuiColor.Background
                borderColor: QuiColor.Border
                showGridLines: true
                minimumColumnWidth: 80
                fitColumnsToWidth: true
                columnSource: [
                    { width: 100, minimumWidth: 100 },
                    { width: 180, minimumWidth: 120 },
                    { width: 140, minimumWidth: 100 },
                    { width: 120, minimumWidth: 90 },
                    { width: 170, minimumWidth: 140 },
                    { width: 110, minimumWidth: 90 },
                    { width: 110, minimumWidth: 90 },
                    { width: 180, minimumWidth: 140 },
                    { width: 200, minimumWidth: 200 }
                ]

                delegate: DelegateChooser {
                    DelegateChoice {
                        column: TaskTableModel.ProgressColumn
                        Rectangle {
                            implicitWidth: tableView.columnWidth(column)
                            implicitHeight: tableView.rowHeight
                            color: dialog.taskRowColor(model.task_id, row)
                            border.color: QuiColor.Border
                            border.width: 1

                            TapHandler {
                                onTapped: dialog.selectedTaskId = model.task_id
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                spacing: 8

                                QuiProgressBar {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 14
                                    value: Math.max(0, Math.min(100, model.progress || 0)) / 100
                                    textVisible: false
                                    strokeWidth: 14
                                }

                                QuiText {
                                    Layout.preferredWidth: 42
                                    text: (model.progress || 0) + "%"
                                    horizontalAlignment: Text.AlignRight
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }
                    }

                    DelegateChoice {
                        column: TaskTableModel.ActionsColumn
                        Rectangle {
                            implicitWidth: tableView.columnWidth(column)
                            implicitHeight: tableView.rowHeight
                            color: dialog.taskRowColor(model.task_id, row)
                            border.color: QuiColor.Border
                            border.width: 1

                            TapHandler {
                                onTapped: dialog.selectedTaskId = model.task_id
                            }

                            RowLayout {
                                anchors.centerIn: parent
                                spacing: 4

                                QuiTextIconButton {
                                    width: 30
                                    height: 28
                                    text: "开始"
                                    display: Button.IconOnly
                                    iconSource: QuiFontIcon.Play
                                    enabled: dialog.taskManager && (model.can_start || false)
                                    onClicked: {
                                        dialog.selectedTaskId = model.task_id
                                        dialog.taskManager.startTask(model.task_id)
                                    }
                                }

                                QuiTextIconButton {
                                    width: 30
                                    height: 28
                                    text: "暂停"
                                    display: Button.IconOnly
                                    iconSource: QuiFontIcon.Pause
                                    enabled: dialog.taskManager && (model.can_pause || false)
                                    onClicked: {
                                        dialog.selectedTaskId = model.task_id
                                        dialog.taskManager.pauseTask(model.task_id)
                                    }
                                }

                                QuiTextIconButton {
                                    width: 30
                                    height: 28
                                    text: "停止"
                                    display: Button.IconOnly
                                    iconSource: QuiFontIcon.Stop
                                    enabled: dialog.taskManager && (model.can_stop || false)
                                    onClicked: {
                                        dialog.selectedTaskId = model.task_id
                                        dialog.taskManager.stopTask(model.task_id)
                                    }
                                }

                                QuiTextIconButton {
                                    width: 30
                                    height: 28
                                    text: "删除"
                                    display: Button.IconOnly
                                    iconSource: QuiFontIcon.Delete
                                    enabled: dialog.taskManager !== null
                                    onClicked: {
                                        dialog.selectedTaskId = model.task_id
                                        dialog.taskManager.deleteTask(model.task_id)
                                    }
                                }
                            }
                        }
                    }

                    DelegateChoice {
                        Rectangle {
                            implicitWidth: tableView.columnWidth(column)
                            implicitHeight: tableView.rowHeight
                            color: dialog.taskRowColor(model.task_id, row)
                            border.color: QuiColor.Border
                            border.width: 1

                            TapHandler {
                                onTapped: dialog.selectedTaskId = model.task_id
                            }

                            QuiText {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                text: model.display || ""
                                elide: Text.ElideRight
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }
            }
        }
    }
}
