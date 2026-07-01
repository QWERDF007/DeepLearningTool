import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform

import dltool.ui
import dltool.data
import quickui

QuiPopup {
    id: exportDataDialog
    width: 600
    height: 320

    property DataManager dataManager
    property var datasetIds: []

    property alias datasetName: exportDataForm.datasetName
    property alias dataFormatModel: exportDataForm.dataFormatModel

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        QuiText {
            text: "导出数据"
            font: QuiFont.Subtitle
        }
        ExportDataForm {
            id: exportDataForm
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
        Item {
            Layout.fillWidth: true
            height: 32
            QuiButton {
                anchors.right: exportBtn.left
                anchors.rightMargin: 5
                width: parent.width / 4
                height: parent.height
                text: "取消"
                onClicked: {
                    exportDataDialog.close()
                }
            }

            QuiButton {
                id: exportBtn
                enabled: exportDataForm.output_dir.length > 0 && datasetIds.length > 0
                anchors.right: parent.right
                width: parent.width / 4
                height: parent.height
                text: "导出"
                normalColor: QuiColor.Highlight
                onClicked: {
                    exportDataDialog.close()
                    if (dataManager) {
                        let data_format = DataFormat.getDataFormat(exportDataForm.dataFormat)
                        dataManager.exportDatasets(datasetIds, data_format, exportDataForm.output_dir)
                    }
                }
            }
        }
    }
}
