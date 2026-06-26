import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.settings
import quickui

QuiPopup {
    id: dialog

    property var dataManager
    readonly property var controller: dataManager ? dataManager.fewShotLearning : null
    readonly property var fewShotSettings: GlobalSettings.settingsObjectFor(SettingsAccessor.FewShotLearning)
    readonly property var softwareSettings: GlobalSettings.settingsObjectFor(SettingsAccessor.Software)
    property var datasetNames: []
    property var selectedTrainDatasetMap: ({})
    property var selectedTestDatasetMap: ({})
    property var selectedClassMap: ({})

    implicitWidth: 720
    implicitHeight: 780
    focus: true
    closePolicy: Popup.CloseOnEscape

    function openForStart() {
        resetSelections()
        open()
    }

    function resetSelections() {
        Qt.callLater(function () {
            datasetNames = dataManager ? dataManager.getAllDatasetsName() : []
            selectedTrainDatasetMap = {}
            selectedTestDatasetMap = {}
            selectedClassMap = {}
        })
    }

    function selectedDatasetIds(selectionMap) {
        let ids = []
        if (!dataManager) {
            return ids
        }
        for (let i = 0; i < datasetNames.length; ++i) {
            let id = dataManager.getDatasetId(datasetNames[i])
            if (selectionMap[String(id)] === true) {
                ids.push(id)
            }
        }
        return ids
    }

    function selectedTrainDatasetIds() {
        return selectedDatasetIds(selectedTrainDatasetMap)
    }

    function selectedTestDatasetIds() {
        return selectedDatasetIds(selectedTestDatasetMap)
    }

    function selectedClassIds() {
        let ids = []
        let allIds = dataManager ? dataManager.getAllLabelClassIds() : []
        for (let i = 0; i < allIds.length; ++i) {
            if (classSelected(allIds[i])) {
                ids.push(allIds[i])
            }
        }
        return ids
    }

    function datasetSelected(selectionMap, datasetName) {
        if (!dataManager) {
            return false
        }
        let id = dataManager.getDatasetId(datasetName)
        return selectionMap[String(id)] === true
    }

    function setDatasetSelected(selectionMapName, datasetName, selected) {
        if (!dataManager) {
            return
        }
        let source = selectionMapName === "train" ? selectedTrainDatasetMap : selectedTestDatasetMap
        let next = {}
        for (let key in source) {
            next[key] = source[key]
        }
        next[String(dataManager.getDatasetId(datasetName))] = selected
        if (selectionMapName === "train") {
            selectedTrainDatasetMap = next
        } else {
            selectedTestDatasetMap = next
        }
    }

    function classSelected(labelClassId) {
        return selectedClassMap[String(labelClassId)] === true
    }

    function setClassSelected(labelClassId, selected) {
        let next = {}
        for (let key in selectedClassMap) {
            next[key] = selectedClassMap[key]
        }
        next[String(labelClassId)] = selected
        selectedClassMap = next
    }

    function canStart() {
        return controller
                && !controller.running
                && dataManager
                && selectedTrainDatasetIds().length > 0
                && selectedTestDatasetIds().length > 0
                && selectedClassIds().length >= 1
                && softwareSettings
                && String(softwareSettings.pythonEnvPath || "").length > 0
    }

    function startFewShot() {
        if (!canStart()) {
            return
        }
        if (controller.startFsSam2(selectedTrainDatasetIds(), selectedTestDatasetIds(), selectedClassIds())) {
            close()
        }
    }

    onOpened: resetSelections()

    ColumnLayout {
        width: parent.width
        height: parent.height
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 18
            Layout.bottomMargin: 10
            spacing: 10

            QuiText {
                Layout.fillWidth: true
                text: "小样本学习"
                font: QuiFont.Title
                color: QuiColor.FontPrimary
            }
        }

        QuiScrollablePage {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 0

            ColumnLayout {
                width: parent.width
                spacing: 12

                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    Layout.topMargin: 12
                    implicitHeight: dataSection.implicitHeight + 24
                    radius: 4
                    color: QuiColor.Primary
                    border.color: QuiColor.Border

                    ColumnLayout {
                        id: dataSection

                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 12

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            QuiText {
                                text: "训练数据集"
                                color: QuiColor.FontDark
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 176
                                radius: 4
                                color: QuiColor.Background
                                border.color: QuiColor.Border
                                clip: true

                                ListView {
                                    id: trainDatasetList

                                    anchors.fill: parent
                                    anchors.margins: 6
                                    boundsBehavior: Flickable.StopAtBounds
                                    clip: true
                                    model: dialog.datasetNames

                                    ScrollBar.vertical: ScrollBar {}

                                    delegate: QuiCheckBox {
                                        width: trainDatasetList.width
                                        height: 30
                                        text: modelData
                                        checked: dialog.datasetSelected(dialog.selectedTrainDatasetMap, modelData)
                                        onToggled: dialog.setDatasetSelected("train", modelData, checked)
                                    }
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            QuiText {
                                text: "测试数据集"
                                color: QuiColor.FontDark
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 176
                                radius: 4
                                color: QuiColor.Background
                                border.color: QuiColor.Border
                                clip: true

                                ListView {
                                    id: testDatasetList

                                    anchors.fill: parent
                                    anchors.margins: 6
                                    boundsBehavior: Flickable.StopAtBounds
                                    clip: true
                                    model: dialog.datasetNames

                                    ScrollBar.vertical: ScrollBar {}

                                    delegate: QuiCheckBox {
                                        width: testDatasetList.width
                                        height: 30
                                        text: modelData
                                        checked: dialog.datasetSelected(dialog.selectedTestDatasetMap, modelData)
                                        onToggled: dialog.setDatasetSelected("test", modelData, checked)
                                    }
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            QuiText {
                                text: "类别"
                                color: QuiColor.FontDark
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 176
                                radius: 4
                                color: QuiColor.Background
                                border.color: QuiColor.Border
                                clip: true

                                ListView {
                                    id: classList

                                    anchors.fill: parent
                                    anchors.margins: 6
                                    boundsBehavior: Flickable.StopAtBounds
                                    clip: true
                                    model: dialog.dataManager ? dialog.dataManager.labelClasses : null

                                    ScrollBar.vertical: ScrollBar {}

                                    delegate: QuiCheckBox {
                                        width: classList.width
                                        height: 30
                                        text: model.name
                                        checked: dialog.classSelected(model.label_class_id)
                                        onToggled: dialog.setClassSelected(model.label_class_id, checked)
                                    }
                                }
                            }
                        }
                    }
                }

                SettingsFieldsPanel {
                    fieldModel: fewShotSettings ? fewShotSettings.fieldModel : null
                }

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 16
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: QuiColor.Border
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            spacing: 10

            QuiText {
                Layout.fillWidth: true
                text: controller && controller.lastError.length > 0
                      ? controller.lastError
                      : (!softwareSettings || String(softwareSettings.pythonEnvPath || "").length === 0
                         ? "请先在软件设置中配置 Python 环境目录"
                         : "")
                color: "red"
                elide: Text.ElideRight
            }

            QuiButton {
                text: "取消"
                onClicked: dialog.close()
            }

            QuiButton {
                text: controller && controller.running ? "运行中" : "启动"
                enabled: dialog.canStart()
                onClicked: dialog.startFewShot()
            }
        }
    }
}
