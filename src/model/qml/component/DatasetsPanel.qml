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
    property int partSpacing: 5
    property int scrollbarReserve: 8

    ColumnLayout {
        anchors.fill: parent
        spacing: control.partSpacing

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
            dataManager: control.dataManager
            roleTitle: qsTr("训练数据集")
            selectionModel: control.selectedModel ? control.selectedModel.trainDatasetViewModel : null
        }

        DatasetPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            dataManager: control.dataManager
            roleTitle: qsTr("验证数据集")
            selectionModel: control.selectedModel ? control.selectedModel.validationDatasetViewModel : null
        }
            
    }
}
