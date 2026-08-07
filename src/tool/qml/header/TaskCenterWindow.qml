import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

import dltool.model
import dltool.project
import dltool.ui
import quickui

Window {
    id: dialog

    property var taskManager: TaskManager
    property var taskModel: taskManager
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
        function onRowsInserted(parent, first, last) {
            dialog.rebuildTasks()
        }
        function onRowsRemoved(parent, first, last) {
            dialog.rebuildTasks()
        }
        function onModelReset() {
            dialog.rebuildTasks()
        }
        function onDataChanged(topLeft, bottomRight, roles) {
            if (!dialog.taskModel) {
                return
            }
            for (let row = topLeft.row; row <= bottomRight.row; ++row) {
                dialog.updateTaskRow(row)
            }
        }
    }

    function displayAt(sourceRow, column) {
        return dialog.taskModel.data(dialog.taskModel.index(sourceRow, column), Qt.DisplayRole)
    }

    function roleData(sourceRow, role) {
        return dialog.taskModel.data(dialog.taskModel.index(sourceRow, 0), role)
    }

    function snapshotTask(sourceRow) {
        return {
            task_id: roleData(sourceRow, TaskManager.TaskIdRole),
            model_name: displayAt(sourceRow, TaskManager.ModelNameColumn),
            task_type: displayAt(sourceRow, TaskManager.TaskTypeColumn),
            status: displayAt(sourceRow, TaskManager.StatusColumn),
            created_at: displayAt(sourceRow, TaskManager.CreatedAtColumn),
            running_time: displayAt(sourceRow, TaskManager.RunningTimeColumn),
            eta: displayAt(sourceRow, TaskManager.EtaColumn),
            progress: roleData(sourceRow, TaskManager.ProgressRole),
            can_start: roleData(sourceRow, TaskManager.CanStartRole),
            can_pause: roleData(sourceRow, TaskManager.CanPauseRole),
            can_stop: roleData(sourceRow, TaskManager.CanStopRole),
            can_delete: roleData(sourceRow, TaskManager.CanDeleteRole),
            progress_cell: tableView.customItem(com_progress),
            actions_cell: tableView.customItem(com_action)
        }
    }

    function rebuildTasks() {
        if (!dialog.taskModel) {
            return
        }
        let rows = []
        for (let i = 0; i < dialog.taskModel.count; ++i) {
            let row = snapshotTask(i)
            let old = tableView.getRow(i)
            if (old && old._key) {
                row._key = old._key
            }
            rows.push(row)
        }
        tableView.dataSource = rows
    }

    function updateTaskRow(sourceRow) {
        let row = snapshotTask(sourceRow)
        let old = tableView.getRow(sourceRow)
        if (old && old._key) {
            row._key = old._key
        }
        tableView.setRow(sourceRow, row)
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
        rebuildTasks()
    }

    Component {
        id: com_progress
        Item {
            anchors.fill: parent
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8

                QuiProgressBar {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 14
                    value: Math.max(0, Math.min(100, rowModel ? rowModel.progress : 0)) / 100
                    textVisible: false
                    strokeWidth: 14
                }

                QuiText {
                    Layout.preferredWidth: 42
                    text: (rowModel ? rowModel.progress : 0) + "%"
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    Component {
        id: com_action
        Item {
            anchors.fill: parent
            RowLayout {
                anchors.centerIn: parent
                spacing: 4

                QuiTextIconButton {
                    width: 30
                    height: 28
                    text: "开始"
                    display: Button.IconOnly
                    iconSource: QuiFontIcon.Play
                    enabled: rowModel ? (rowModel.can_start || false) : false
                    onClicked: {
                        dialog.selectedTaskId = rowModel.task_id
                        dialog.taskManager.startTask(rowModel.task_id)
                    }
                }

                QuiTextIconButton {
                    width: 30
                    height: 28
                    text: "暂停"
                    display: Button.IconOnly
                    iconSource: QuiFontIcon.Pause
                    enabled: rowModel ? (rowModel.can_pause || false) : false
                    onClicked: {
                        dialog.selectedTaskId = rowModel.task_id
                        dialog.taskManager.pauseTask(rowModel.task_id)
                    }
                }

                QuiTextIconButton {
                    width: 30
                    height: 28
                    text: "停止"
                    display: Button.IconOnly
                    iconSource: QuiFontIcon.Stop
                    enabled: rowModel ? (rowModel.can_stop || false) : false
                    onClicked: {
                        dialog.selectedTaskId = rowModel.task_id
                        dialog.taskManager.stopTask(rowModel.task_id)
                    }
                }

                QuiTextIconButton {
                    width: 30
                    height: 28
                    text: "删除"
                    display: Button.IconOnly
                    iconSource: QuiFontIcon.Delete
                    enabled: rowModel ? (rowModel.can_delete || false) : false
                    onClicked: {
                        dialog.selectedTaskId = rowModel.task_id
                        dialog.taskManager.deleteTask(rowModel.task_id)
                    }
                }
            }
        }
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
                dataSource: []
                rowHeight: 42
                headerHeight: 34
                headerColor: QuiColor.Background
                borderColor: QuiColor.Border
                showGridLines: true
                minimumColumnWidth: 80
                fitColumnsToWidth: true
                columnSource: [
                    { title: "任务ID", dataIndex: "task_id", width: 80, minimumWidth: 80, stretch: false },
                    { title: "模型名称", dataIndex: "model_name", width: 180, minimumWidth: 120 },
                    { title: "任务类型", dataIndex: "task_type", width: 140, minimumWidth: 100 },
                    { title: "任务状态", dataIndex: "status", width: 120, minimumWidth: 90 },
                    { title: "任务创建时间", dataIndex: "created_at", width: 170, minimumWidth: 140 },
                    { title: "运行时间", dataIndex: "running_time", width: 110, minimumWidth: 90 },
                    { title: "剩余时间", dataIndex: "eta", width: 110, minimumWidth: 90 },
                    { title: "进度", dataIndex: "progress_cell", width: 180, minimumWidth: 140 },
                    { title: "操作", dataIndex: "actions_cell", width: 180, minimumWidth: 160, stretch: false }
                ]

                onCurrentChanged: {
                    dialog.selectedTaskId = current ? current.task_id : -1
                }
            }
        }
    }
}
