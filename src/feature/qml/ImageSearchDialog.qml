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
    property var queryImageIds: []
    property bool imageSearchEnabled: true
    readonly property var imageSearchSettings: GlobalSettings.settingsObjectFor(SettingsAccessor.ImageSearch)

    DataSelectionTreeModel {
        id: datasetSelectionModel
    }

    implicitWidth: 680
    implicitHeight: 720
    focus: true
    closePolicy: Popup.CloseOnEscape

    function imageSearchController() {
        return featureManager ? featureManager.imageSearch : null
    }

    function bindDatasetSelectionModel() {
        let manager = dialog.dataManager
        datasetSelectionModel.setDatasetClassSourceModels(
                    manager ? manager.datasets : null,
                    manager ? manager.labelClasses : null,
                    manager ? manager.imageInstances : null,
                    manager ? manager.labelInstances : null)
    }

    function openForSearch() {
        queryImageIds = []
        resetDatasetSelection()
        open()
    }

    function openForImages(imageIds) {
        queryImageIds = imageIds ? imageIds : []
        resetDatasetSelection()
        open()
    }

    function resetDatasetSelection() {
        datasetSelectionModel.clearSelection()
    }

    function selectedSearchScope() {
        return datasetSelectionModel.selectedDatasetClassScope()
    }

    function refreshImageSearchEnabled() {
        imageSearchEnabled = GlobalSettings.valueForField(
                    SettingsAccessor.ImageSearch,
                    ImageSearchField.Enabled,
                    true)
    }

    function startSearch() {
        let controller = imageSearchController()
        if (!controller) {
            return
        }

        let searchScope = selectedSearchScope()
        let started = false
        if (queryImageIds && queryImageIds.length > 0) {
            started = controller.search(queryImageIds, searchScope)
        } else {
            started = controller.searchSelectedImages(searchScope)
        }
        if (started) {
            close()
        }
    }

    onOpened: {
        resetDatasetSelection()
        refreshImageSearchEnabled()
    }

    onDataManagerChanged: bindDatasetSelectionModel()
    Component.onCompleted: bindDatasetSelectionModel()

    Connections {
        target: imageSearchSettings ? imageSearchSettings.fieldModel : null

        function onValueChanged(name, value) {
            dialog.refreshImageSearchEnabled()
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
                text: "图像搜索"
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
                    fieldModel: imageSearchSettings ? imageSearchSettings.fieldModel : null
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
                text: dialog.imageSearchController() ? dialog.imageSearchController().lastError : ""
                color: "red"
                elide: Text.ElideRight
            }

            QuiButton {
                text: "取消"
                onClicked: dialog.close()
            }
            QuiButton {
                text: "开始搜索"
                enabled: dialog.imageSearchController()
                         && !dialog.imageSearchController().running
                         && dialog.imageSearchEnabled
                         && dialog.selectedSearchScope().length > 0
                onClicked: dialog.startSearch()
            }
        }
    }
}
