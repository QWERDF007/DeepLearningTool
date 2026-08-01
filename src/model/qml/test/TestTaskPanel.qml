import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.model
import dltool.ui
import quickui

Rectangle {
    id: control
    color: QuiColor.Primary
    property ModelTestTaskManager taskManager: null

    function taskData(row, role) {
        if (!control.taskManager || row < 0 || row >= control.taskManager.count)
            return ""
        return control.taskManager.data(control.taskManager.index(row, 0), role)
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 6

        QuiText {
            text: qsTr("测试任务")
        }

        QuiComboBox {
            id: taskCombo
            Layout.preferredWidth: 220
            // Keep the C++ list model attached directly.  Falling back to a
            // JavaScript array makes the control switch model kinds while the
            // project/model context is being bound, which can leave the
            // ComboBox delegate with a stale empty model after rows are
            // inserted or restored from tasks.yaml.
            // Use the C++ model's row count as the presentation model.  Each
            // delegate reads its name/UUID on demand from the same C++ model;
            // no task snapshot is created in QML.
            model: control.taskManager ? control.taskManager.count : 0
            delegate: QuiItemDelegate {
                width: ListView.view ? ListView.view.width : taskCombo.width
                text: control.taskData(index, ModelTestTaskManager.NameRole)
                highlighted: taskCombo.highlightedIndex === index
                hoverEnabled: true
            }
            // The manager owns the current task and emits currentTaskChanged
            // after a create/reload/switch.  Binding the display text to that
            // property keeps the selected name visible even when the model is
            // reset before the ComboBox has rebuilt its delegate model.
            displayText: control.taskManager ? control.taskManager.currentTaskName : ""
            // The task bar is fixed at 48 px and is followed by clipped
            // expanders.  Do not let the template derive a zero-height popup
            // from an as-yet-unmeasured delegate list, and keep it above the
            // following layout content.
            popup.height: Math.min(Math.max(40, taskCombo.count * 40 + 12), 360)
            popup.z: 1000
            onActivated: {
                if (control.taskManager && currentIndex >= 0)
                    control.taskManager.switchTask(control.taskData(currentIndex,
                                                                      ModelTestTaskManager.UuidRole))
            }
        }

        Binding {
            target: taskCombo
            property: "currentIndex"
            value: control.taskManager ? control.taskManager.currentIndex : -1
        }

        QuiTextIconButton {
            Layout.preferredWidth: 32
            Layout.preferredHeight: 30
            display: Button.IconOnly
            text: qsTr("添加")
            iconSource: QuiFontIcon.Add
            enabled: !!control.taskManager && !control.taskManager.currentTaskRunning
            onClicked: {
                taskName.text = qsTr("测试 %1").arg(control.taskManager ? control.taskManager.count + 1 : 1)
                createDialog.open()
            }
        }

        QuiTextIconButton {
            Layout.preferredWidth: 32
            Layout.preferredHeight: 30
            display: Button.IconOnly
            text: qsTr("重命名")
            iconSource: QuiFontIcon.Rename
            enabled: !!control.taskManager && control.taskManager.currentTaskUuid.length > 0
                     && !control.taskManager.currentTaskRunning
            onClicked: {
                renameField.text = control.taskManager ? control.taskManager.currentTaskName : ""
                renameDialog.open()
            }
        }

        QuiTextIconButton {
            Layout.preferredWidth: 32
            Layout.preferredHeight: 30
            display: Button.IconOnly
            text: qsTr("删除")
            iconSource: QuiFontIcon.Delete
            enabled: !!control.taskManager && control.taskManager.count > 1
                     && control.taskManager.currentTaskUuid.length > 0
                     && !control.taskManager.currentTaskRunning
            onClicked: deleteDialog.open()
        }

        QuiText {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignRight
            visible: !!control.taskManager && control.taskManager.currentTaskStatus.length > 0
            text: qsTr("%1  %2%").arg(control.taskManager ? control.taskManager.currentTaskStatus : "")
                                  .arg(control.taskManager ? control.taskManager.currentTaskProgress : 0)
        }

        QuiText {
            visible: {
                var evaluation = control.taskManager ? control.taskManager.currentEvaluation : null
                return !!evaluation && (evaluation.inferenceOutdated || evaluation.evaluationOutdated
                                        || evaluation.available)
            }
            text: {
                var evaluation = control.taskManager ? control.taskManager.currentEvaluation : null
                if (!evaluation)
                    return ""
                if (evaluation.inferenceOutdated)
                    return qsTr("推理输入已变化")
                if (evaluation.evaluationOutdated)
                    return qsTr("评估参数或标注已变化")
                return qsTr("结果有效")
            }
            color: QuiColor.FontDark
        }
    }

    QuiPopup {
        id: createDialog
        width: 420
        height: 260
        maskOpacity: 0.2
        onOpened: taskName.forceActiveFocus()

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            QuiText {
                Layout.fillWidth: true
                text: qsTr("添加测试任务")
                font: QuiFont.Title
            }

            QuiTextField {
                id: taskName
                Layout.fillWidth: true
                placeholderText: qsTr("测试任务名称")
                text: qsTr("测试 %1").arg(control.taskManager ? control.taskManager.count + 1 : 1)
            }

            Item {
                Layout.fillWidth: true
                Layout.minimumHeight: 36
                Layout.preferredHeight: 36
                QuiText {
                    anchors.fill: parent
                    color: "#D83B01"
                    text: control.taskManager ? control.taskManager.validateTaskName(taskName.text) : ""
                    wrapMode: Text.Wrap
                    visible: text.length > 0
                }
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Item { Layout.fillWidth: true }
                QuiButton {
                    text: qsTr("取消")
                    onClicked: createDialog.close()
                }
                QuiButton {
                    text: qsTr("确认")
                    enabled: !!control.taskManager && taskName.text.trim().length > 0
                             && control.taskManager.validateTaskName(taskName.text).length === 0
                    onClicked: {
                        const uuid = control.taskManager.createTask(taskName.text)
                        if (uuid && !uuid.includes("失败") && !uuid.includes("不能为空")) {
                            taskName.clear()
                            createDialog.close()
                        }
                    }
                }
            }
        }
    }

    QuiPopup {
        id: renameDialog
        width: 420
        height: 260
        maskOpacity: 0.2
        onOpened: renameField.forceActiveFocus()

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            QuiText {
                Layout.fillWidth: true
                text: qsTr("重命名测试任务")
                font: QuiFont.Title
            }

            QuiTextField {
                id: renameField
                Layout.fillWidth: true
                placeholderText: qsTr("测试任务名称")
            }

            Item {
                Layout.fillWidth: true
                Layout.minimumHeight: 36
                Layout.preferredHeight: 36
                QuiText {
                    anchors.fill: parent
                    color: "#D83B01"
                    wrapMode: Text.Wrap
                    text: control.taskManager ? control.taskManager.validateTaskName(renameField.text) : ""
                    visible: text.length > 0
                }
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Item { Layout.fillWidth: true }
                QuiButton {
                    text: qsTr("取消")
                    onClicked: renameDialog.close()
                }
                QuiButton {
                    text: qsTr("确认")
                    enabled: !!control.taskManager && renameField.text.trim().length > 0
                             && control.taskManager.validateTaskName(renameField.text).length === 0
                    onClicked: {
                        control.taskManager.renameTask(control.taskManager.currentTaskUuid, renameField.text)
                        renameDialog.close()
                    }
                }
            }
        }
    }

    QuiContentDialog {
        id: deleteDialog
        title: qsTr("删除测试任务")
        message: qsTr("确定删除当前测试任务及其结果吗？")
        negativeText: qsTr("取消")
        positiveText: qsTr("确认")
        useNeutralButton: false
        useNegativeButton: true
        usePositiveButton: true
        onPositiveClicked: {
            if (control.taskManager)
                control.taskManager.deleteTask(control.taskManager.currentTaskUuid)
        }
    }
}
