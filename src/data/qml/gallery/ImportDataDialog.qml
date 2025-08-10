import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform

import dltool.ui
import dltool.data

DltPopup {
    id: importDataDialog
    width: 600
    height: 400

    property DataManager dataManager

    property alias datasetName: importDataForm.datasetName
    property alias datasetsModel: importDataForm.datasetsModel
    property alias dataFormatModel: importDataForm.dataFormatModel

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        DltText {
            text: "导入数据"
            font: DltFont.Subtitle
        }
        ImportDataForm {
            id: importDataForm
            Layout.fillWidth: true
            Layout.fillHeight: true
            // height: 320
        }
        Item {
            Layout.fillWidth: true
            height: 32
            DltButton {
                anchors.right: createBtn.left
                anchors.rightMargin: 5
                width: parent.width / 4
                height: parent.height
                text: "取消"
                onClicked: {
                    importDataDialog.close()
                }
            }

            DltButton {
                id: createBtn
                enabled: importDataForm.image_dir
                anchors.right: parent.right
                width: parent.width / 4
                height: parent.height
                text: " 导入"
                normalColor: DltColor.Highlight
                onClicked: {
                    importDataDialog.close()
                    if (dataManager) {
                        let dataset_id = dataManager.getDatasetId(importDataForm.datasetName)
                        let data_format = DataFormat.getDataFormat(importDataForm.dataFormat)
                        dataManager.importData(dataset_id, data_format, importDataForm.image_dir, importDataForm.data_dir)
                    }
                }
            }
        }
    }
}
