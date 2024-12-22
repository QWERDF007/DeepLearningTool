import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform

import dltool.ui
import dltool.project

DltPopup {
    id: importDataDialog
    width: 1000
    height: 600

    property int rowH: 64

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
            height: 320
        }
        Item {
            Layout.fillHeight: true
            Layout.fillWidth: true
        }
    }
}
