import QtQuick
import QtQuick.Layouts

import dltool.ui
import dltool.model
import quickui

Rectangle {
    id: control

    property var stateData: ({})
    property TaskManager taskManager: null
    property string modelUuid: ""
    // Keep the QML binding tied to task lifecycle changes. The actual elapsed
    // value is calculated by TaskManager from its local clock.
    property int taskRevision: taskManager ? taskManager.revision : 0
    property int runtimeRefreshRevision: 0

    function localRunningTimeText() {
        const taskRevision = control.taskRevision
        const runtimeRefreshRevision = control.runtimeRefreshRevision
        if (!control.taskManager || control.modelUuid.length === 0)
            return control.stateData.elapsed || "-"

        const taskId = control.taskManager.findModelTask(control.modelUuid, ModelTaskTypes.Train, true)
        return taskId >= 0 ? control.taskManager.taskRunningTime(taskId)
                           : (control.stateData.elapsed || "-")
    }

    color: QuiColor.Primary
    border.color: QuiColor.Border
    border.width: 1

    Timer {
        interval: 1000
        repeat: true
        running: {
            // taskRevision makes the timer stop/start when the task changes
            // state; it otherwise only needs to tick while a task is active.
            const revision = control.taskRevision
            return control.visible && control.taskManager !== null
                   && control.modelUuid.length > 0
                   && control.taskManager.hasActiveModelTasks(control.modelUuid)
        }
        onTriggered: control.runtimeRefreshRevision += 1
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6

        QuiText {
            Layout.fillWidth: true
            text: "训练状态"
            font: QuiFont.Subtitle
        }

        ModelStatusRow {
            label: "Epoch"
            value: control.stateData.epoch || "-"
        }
        ModelStatusRow {
            label: "Iter"
            value: control.stateData.iter || "-"
        }
        ModelStatusRow {
            label: "学习率"
            value: control.stateData.lr || "-"
        }
        ModelStatusRow {
            label: "Loss"
            value: control.stateData.loss || "-"
        }
        ModelStatusRow {
            label: "运行时间"
            value: control.localRunningTimeText()
        }

        ModelStatusRow {
            label: "剩余时间"
            value: control.stateData.eta || "-"
        }

        Item { Layout.fillHeight: true }
    }
}
