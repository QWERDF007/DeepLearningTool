import QtQuick
import QtQuick.Controls
import quickui

QuiMenu {
    id: control
    title: "复制到"
    width: 200

    property var dataManager: null
    property var datasetsModel: dataManager ? dataManager.datasets : null
    property bool itemEnabled: true

    signal datasetTriggered(int datasetId, string datasetName)

    Instantiator {
        id: instantiator
        model: control.datasetsModel
        delegate: QuiMenuItem {
            text: model.name || ""
            iconSource: QuiFontIcon.Folder
            enabled: control.itemEnabled
            onClicked: {
                control.datasetTriggered(model.dataset_id, model.name)
            }
        }
        onObjectAdded: function(index, object) {
            control.insertItem(index, object)
        }
        onObjectRemoved: function(index, object) {
            control.removeItem(object)
        }
    }
}
