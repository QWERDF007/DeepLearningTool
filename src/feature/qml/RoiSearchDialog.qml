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
    property var queryLabelIds: []
    property bool roiSearchEnabled: true
    readonly property var roiSearchSettings: GlobalSettings.settingsObjectFor(SettingsAccessor.RoiSearch)

    DataSelectionTreeModel {
        id: datasetSelectionModel
    }

    implicitWidth: 680
    implicitHeight: 760
    focus: true
    closePolicy: Popup.CloseOnEscape

    function roiSearchController() {
        return featureManager ? featureManager.roiSearch : null
    }

    function bindDatasetSelectionModel() {
        let manager = dialog.dataManager
        datasetSelectionModel.setDatasetClassSourceModels(
                    manager ? manager.datasets : null,
                    manager ? manager.labelClasses : null,
                    manager ? manager.imageInstances : null,
                    manager ? manager.labelInstances : null)
    }

    function openForLabels(labelIds) {
        queryLabelIds = labelIds ? labelIds : []
        resetDatasetSelection()
        open()
    }

    function resetDatasetSelection() {
        datasetSelectionModel.clearSelection()
    }

    function selectedSearchScope() {
        return datasetSelectionModel.selectedDatasetClassScope()
    }

    function refreshRoiSearchEnabled() {
        roiSearchEnabled = GlobalSettings.valueForField(
                    SettingsAccessor.RoiSearch,
                    RoiSearchField.Enabled,
                    true)
    }

    function startSearch() {
        let controller = roiSearchController()
        if (!controller || !roiSearchEnabled || !queryLabelIds || queryLabelIds.length === 0) {
            return
        }

        let started = controller.search(queryLabelIds, selectedSearchScope())
        if (started) {
            close()
        }
    }

    onOpened: {
        resetDatasetSelection()
        refreshRoiSearchEnabled()
    }

    onDataManagerChanged: bindDatasetSelectionModel()
    Component.onCompleted: bindDatasetSelectionModel()

    Connections {
        target: roiSearchSettings ? roiSearchSettings.fieldModel : null

        function onValueChanged(name, value) {
            dialog.refreshRoiSearchEnabled()
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
                text: "标注搜索"
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

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 12
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    implicitHeight: datasetSection.implicitHeight + 24
                    radius: 4
                    color: QuiColor.Primary
                    border.color: QuiColor.Border

                    ColumnLayout {
                        id: datasetSection

                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 10

                        DatasetSelectionTreeView {
                            Layout.fillWidth: true
                            roleTitle: "搜索数据集和类别"
                            selectionModel: datasetSelectionModel
                            treeHeight: 150
                        }
                    }
                }

                SettingsFieldsPanel {
                    fieldModel: roiSearchSettings ? roiSearchSettings.fieldModel : null
                }

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 16
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.bottomMargin: 10
            spacing: 10

            QuiText {
                Layout.fillWidth: true
                text: dialog.roiSearchController() ? dialog.roiSearchController().lastError : ""
                color: "red"
                elide: Text.ElideRight
            }

            QuiButton {
                text: "取消"
                onClicked: dialog.close()
            }
            QuiButton {
                text: "开始搜索"
                enabled: dialog.roiSearchController()
                         && !dialog.roiSearchController().running
                         && queryLabelIds
                         && queryLabelIds.length > 0
                         && dialog.roiSearchEnabled
                         && dialog.selectedSearchScope().length > 0
                onClicked: dialog.startSearch()
            }
        }
    }
}
