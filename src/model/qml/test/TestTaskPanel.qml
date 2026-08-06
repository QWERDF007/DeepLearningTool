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
            model: control.taskManager
            textRole: "name"
            currentIndex: control.taskManager ? control.taskManager.currentIndex : -1
            displayText: control.taskManager ? control.taskManager.currentTaskName : ""
            onActivated: {
                if (control.taskManager && currentIndex >= 0) {
                    control.taskManager.switchTask(control.taskData(currentIndex, ModelTestTaskManager.UuidRole))
                }
            }
        }

        QuiTextIconButton {
            text: qsTr("添加")
            iconSource: QuiFontIcon.Add
            enabled: !!control.taskManager && !control.taskManager.currentTaskRunning
            onClicked: {
                taskName.text = qsTr("测试 %1").arg(control.taskManager ? control.taskManager.count + 1 : 1)
                createDialog.open()
            }
        }

        QuiTextIconButton {
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
            text: qsTr("删除")
            iconSource: QuiFontIcon.Delete
            enabled: !!control.taskManager && control.taskManager.count > 1
                     && control.taskManager.currentTaskUuid.length > 0
                     && !control.taskManager.currentTaskRunning
            onClicked: deleteDialog.open()
        }

        QuiText {
            visible: !!control.taskManager && control.taskManager.currentTaskStatus.length > 0
            text: qsTr("%1  %2%").arg(control.taskManager ? control.taskManager.currentTaskStatus : "")
                                  .arg(control.taskManager ? control.taskManager.currentTaskProgress : 0)
        }

        Item {
            Layout.fillWidth: true

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
                font: QuiFont.Subtitle
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
