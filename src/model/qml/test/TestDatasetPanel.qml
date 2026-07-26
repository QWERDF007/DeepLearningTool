import QtQuick
import QtQuick.Layouts

import dltool.data
import dltool.model
import dltool.ui
import quickui


Rectangle {
    id: control
    color: QuiColor.Primary

    property DataManager dataManager: null
    property IModel selectedModel: null

    ColumnLayout {
        anchors.fill: parent
        spacing: 5

        QuiText {
            Layout.fillWidth: true
            Layout.topMargin: 5
            Layout.leftMargin: 5
            Layout.preferredHeight: 40
            text: qsTr("数据集选择")
            font: QuiFont.Title
        }

        DatasetPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            roleTitle: qsTr("测试数据集")
            dataManager: control.dataManager
            selectionModel: control.selectedModel ? control.selectedModel.testDatasetViewModel : null
        }
    }
}