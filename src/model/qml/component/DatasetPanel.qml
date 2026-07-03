import QtQuick
import QtQuick.Layouts

import dltool.data
import dltool.model
import dltool.ui
import quickui

Rectangle {
    id: control

    property IModel selectedModel: null
    property int partSpacing: 5
    property int scrollbarReserve: 8

    radius: 4
    clip: true
    color: QuiColor.Primary

    QuiScrollablePage {
        anchors.fill: parent
        anchors.leftMargin: control.partSpacing
        anchors.topMargin: control.partSpacing
        anchors.rightMargin: 0
        anchors.bottomMargin: control.partSpacing
        animationEnabled: false

        DatasetSelectionTreeView {
            Layout.fillWidth: true
            Layout.rightMargin: control.scrollbarReserve
            roleTitle: qsTr("训练数据集")
            selectionModel: control.selectedModel ? control.selectedModel.trainDatasetViewModel : null
            treeHeight: 180
        }

        DatasetSelectionTreeView {
            Layout.fillWidth: true
            Layout.rightMargin: control.scrollbarReserve
            Layout.topMargin: 8
            roleTitle: qsTr("验证数据集")
            selectionModel: control.selectedModel ? control.selectedModel.validationDatasetViewModel : null
            treeHeight: 180
        }
    }
}
