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
    // The test-task manager owns the editable dataset selection.  Keep the
    // model optional so the panel can still fall back to the model template
    // while a project/model context is being bound.
    property DataSelectionTreeModel selectionModel: null

    ColumnLayout {
        anchors.fill: parent
        spacing: 5

        QuiText {
            Layout.fillWidth: true
            Layout.topMargin: 5
            Layout.leftMargin: 5
            text: qsTr("数据集选择")
            font: QuiFont.Subtitle
        }

        DatasetPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            roleTitle: qsTr("测试数据集")
            dataManager: control.dataManager
            selectionModel: control.selectionModel
                             ? control.selectionModel
                             : (control.selectedModel ? control.selectedModel.testDatasetViewModel : null)
        }
    }
}
