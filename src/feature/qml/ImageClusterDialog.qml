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
    property var clusterImageIds: []
    property var clusterDatasetIds: []
    property bool imageClusterEnabled: true
    readonly property var imageClusterSettings: GlobalSettings.settingsObjectFor(SettingsAccessor.ImageCluster)

    implicitWidth: 680
    implicitHeight: 760
    focus: true
    closePolicy: Popup.CloseOnEscape

    function imageClusterController() {
        return featureManager ? featureManager.imageCluster : null
    }

    function imageMode() {
        return clusterImageIds && clusterImageIds.length > 0
    }

    function openForImages(imageIds) {
        clusterImageIds = imageIds ? imageIds : []
        clusterDatasetIds = []
        resetDatasetSelection()
        open()
    }

    function openForDatasets(datasetIds) {
        clusterImageIds = []
        clusterDatasetIds = datasetIds ? datasetIds : []
        resetDatasetSelection()
        open()
    }

    function resetDatasetSelection() {
        Qt.callLater(function () {
            for (let i = 0; i < datasetRepeater.count; ++i) {
                let item = datasetRepeater.itemAt(i)
                if (item) {
                    item.checked = clusterDatasetIds.length === 0
                            || clusterDatasetIds.indexOf(item.datasetId) >= 0
                }
            }
        })
    }

    function selectedDatasetIds() {
        let ids = []
        for (let i = 0; i < datasetRepeater.count; ++i) {
            let item = datasetRepeater.itemAt(i)
            if (item && item.checked) {
                ids.push(item.datasetId)
            }
        }
        return ids
    }

    function refreshImageClusterEnabled() {
        imageClusterEnabled = GlobalSettings.valueForField(
                    SettingsAccessor.ImageCluster,
                    ImageClusterField.Enabled,
                    true)
    }

    function startCluster() {
        let controller = imageClusterController()
        if (!controller) {
            return
        }

        let started = false
        if (imageMode()) {
            started = controller.cluster(clusterImageIds, [])
        } else {
            started = controller.cluster([], selectedDatasetIds())
        }
        if (started) {
            close()
        }
    }

    onOpened: {
        resetDatasetSelection()
        refreshImageClusterEnabled()
    }

    Connections {
        target: imageClusterSettings ? imageClusterSettings.fieldModel : null

        function onValueChanged(name, value) {
            dialog.refreshImageClusterEnabled()
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
                text: "图像聚类"
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
                    implicitHeight: sourceSection.implicitHeight + 24
                    radius: 4
                    color: QuiColor.Primary
                    border.color: QuiColor.Border

                    ColumnLayout {
                        id: sourceSection

                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            visible: dialog.imageMode()

                            QuiText {
                                text: "聚类图像"
                                color: QuiColor.FontDark
                            }

                            QuiText {
                                text: "选中图像: " + (dialog.clusterImageIds ? dialog.clusterImageIds.length : 0)
                                color: QuiColor.FontPrimary
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            visible: !dialog.imageMode()

                            QuiText {
                                text: "聚类数据集"
                                color: QuiColor.FontDark
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.min(150, Math.max(48, datasetColumn.implicitHeight + 12))
                                radius: 4
                                color: QuiColor.Background
                                border.color: QuiColor.Border
                                clip: true

                                Flickable {
                                    anchors.fill: parent
                                    anchors.margins: 6
                                    contentHeight: datasetColumn.implicitHeight
                                    boundsBehavior: Flickable.StopAtBounds
                                    clip: true

                                    Column {
                                        id: datasetColumn

                                        width: parent.width
                                        spacing: 4

                                        Repeater {
                                            id: datasetRepeater

                                            model: dialog.dataManager ? dialog.dataManager.datasets : null
                                            delegate: QuiCheckBox {
                                                property int datasetId: model.dataset_id

                                                width: datasetColumn.width
                                                text: model.name
                                                checked: true
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                SettingsFieldsPanel {
                    fieldModel: imageClusterSettings ? imageClusterSettings.fieldModel : null
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
                text: dialog.imageClusterController() ? dialog.imageClusterController().lastError : ""
                color: "red"
                elide: Text.ElideRight
            }

            QuiButton {
                text: "取消"
                onClicked: dialog.close()
            }
            QuiButton {
                text: "开始聚类"
                enabled: dialog.imageClusterController()
                         && !dialog.imageClusterController().running
                         && dialog.imageClusterEnabled
                         && (dialog.imageMode() || dialog.selectedDatasetIds().length > 0)
                onClicked: dialog.startCluster()
            }
        }
    }
}
