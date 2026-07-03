import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.settings
import dltool.data
import dltool.feature
import quickui

QuiPopup {
    id: dialog

    property DataManager dataManager
    property FeatureManager featureManager
    readonly property FewShotLearningController controller: featureManager ? featureManager.fewShotLearning : null
    readonly property var fewShotSettings: GlobalSettings.settingsObjectFor(SettingsAccessor.FewShotLearning)
    property string pythonEnvPath: ""
    property var datasetNames: []
    property var selectedTrainDatasetMap: ({})
    property var selectedValidationDatasetMap: ({})
    property var selectedTestDatasetMap: ({})
    property var selectedClassMap: ({})
    readonly property var datasetSelectorSpecs: [
        { "title": "训练数据集", "selection": "train" },
        { "title": "验证数据集", "selection": "validation" },
        { "title": "测试数据集", "selection": "test" },
    ]

    implicitWidth: 720
    implicitHeight: 780
    focus: true
    closePolicy: Popup.CloseOnEscape

    function openForStart() {
        if (controller && !controller.running) {
            controller.clearLastError()
        }
        refreshDataLists()
        open()
    }

    function availableDatasetNames() {
        return dataManager ? dataManager.getAllDatasetsName() : []
    }

    function refreshDataLists() {
        Qt.callLater(function () {
            datasetNames = availableDatasetNames()
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

    function datasetSelectionMap(selectionMapName) {
        if (selectionMapName === "train") {
            return selectedTrainDatasetMap
        }
        if (selectionMapName === "validation") {
            return selectedValidationDatasetMap
        }
        return selectedTestDatasetMap
    }

    function selectedTrainDatasetIds() {
        return selectedDatasetIds(selectedTrainDatasetMap)
    }

    function selectedValidationDatasetIds() {
        return selectedDatasetIds(selectedValidationDatasetMap)
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
        let source = selectionMapName === "train"
                ? selectedTrainDatasetMap
                : selectionMapName === "validation"
                  ? selectedValidationDatasetMap
                  : selectedTestDatasetMap
        let next = {}
        for (let key in source) {
            next[key] = source[key]
        }
        next[String(dataManager.getDatasetId(datasetName))] = selected
        if (selectionMapName === "train") {
            selectedTrainDatasetMap = next
        } else if (selectionMapName === "validation") {
            selectedValidationDatasetMap = next
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
                && String(pythonEnvPath || "").length > 0
    }

    function startFewShot() {
        if (!canStart()) {
            return
        }
        if (controller.startFsSam2(selectedTrainDatasetIds(), selectedValidationDatasetIds(),
                                   selectedTestDatasetIds(), selectedClassIds())) {
            close()
        }
    }

    onOpened: {
        refreshDataLists()
        refreshSettings()
    }

    function refreshSettings() {
        pythonEnvPath = GlobalSettings.valueForField(
                    SettingsAccessor.Software,
                    SoftwareField.PythonEnvPath,
                    "")
    }

    Connections {
        target: GlobalSettings.catalog
        function onValueChanged() {
            dialog.refreshSettings()
        }
    }

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

                        Repeater {
                            model: dialog.datasetSelectorSpecs

                            delegate: ColumnLayout {
                                id: datasetSelector

                                required property var modelData
                                property string selectionName: modelData.selection

                                Layout.fillWidth: true
                                spacing: 4

                                QuiText {
                                    text: datasetSelector.modelData.title
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
                                        id: datasetList

                                        anchors.fill: parent
                                        anchors.margins: 6
                                        boundsBehavior: Flickable.StopAtBounds
                                        clip: true
                                        model: dialog.datasetNames

                                        ScrollBar.vertical: ScrollBar {}

                                        delegate: QuiCheckBox {
                                            width: datasetList.width
                                            height: 30
                                            text: modelData
                                            checked: dialog.datasetSelected(
                                                         dialog.datasetSelectionMap(datasetSelector.selectionName),
                                                         modelData)
                                            onToggled: dialog.setDatasetSelected(datasetSelector.selectionName,
                                                                                modelData, checked)
                                        }
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
                      : (String(pythonEnvPath || "").length === 0
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
