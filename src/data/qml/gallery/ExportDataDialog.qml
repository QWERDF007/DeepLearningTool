import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform

import dltool.ui
import dltool.data

DltPopup {
    id: exportDataDialog
    width: 600
    height: 320

    property DataManager dataManager
    property int datasetId: -1

    property alias datasetName: exportDataForm.datasetName
    property alias dataFormatModel: exportDataForm.dataFormatModel

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        DltText {
            text: "导出数据"
            font: DltFont.Subtitle
        }
        ExportDataForm {
            id: exportDataForm
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
        Item {
            Layout.fillWidth: true
            height: 32
            DltButton {
                anchors.right: exportBtn.left
                anchors.rightMargin: 5
                width: parent.width / 4
                height: parent.height
                text: "取消"
                onClicked: {
                    exportDataDialog.close()
                }
            }

            DltButton {
                id: exportBtn
                enabled: exportDataForm.output_dir.length > 0 && datasetId >= 0
                anchors.right: parent.right
                width: parent.width / 4
                height: parent.height
                text: "导出"
                normalColor: DltColor.Highlight
                onClicked: {
                    exportDataDialog.close()
                    if (dataManager) {
                        let data_format = DataFormat.getDataFormat(exportDataForm.dataFormat)
                        dataManager.exportDataset(datasetId, data_format, exportDataForm.output_dir)
                    }
                }
            }
        }
    }
}
