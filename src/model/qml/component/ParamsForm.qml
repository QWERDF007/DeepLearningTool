import QtQuick
import QtQuick.Layouts
import dltool.data
import dltool.ui
import dltool.model
import quickui

Item {
    id: control

    property IParams params: null
    property DataManager dataManager: null
    property IModel selectedModel: null
    property string emptyText: qsTr("Select a model")
    property int partSpacing: 5
    property int scrollbarReserve: 8

    function hasGroups() {
        return control.params && control.params.count > 0;
    }

    QuiText {
        anchors.centerIn: parent
        width: Math.max(parent.width - 32, 0)
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        color: QuiColor.FontDark
        text: control.emptyText
        visible: !control.hasGroups()
    }

    RowLayout {
        anchors.fill: parent
        visible: control.hasGroups()
        spacing: control.partSpacing

        DatasetPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 0
            dataManager: control.dataManager
            selectedModel: control.selectedModel
            partSpacing: control.partSpacing
            scrollbarReserve: control.scrollbarReserve
        }

        Repeater {
            model: 2

            ParamPanel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 0
                params: control.params
                targetPartIndex: index
                partSpacing: control.partSpacing
            }
        }
    }
}
