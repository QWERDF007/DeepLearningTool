import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import quickui

Item {
    id: header
    property DataManager dataManager
    RowLayout {
        anchors.fill: parent
        QuiText {
            text: "数据集:"
            font: QuiFont.Subtitle
        }
        Item {
            Layout.fillWidth: true
        }
        QuiTextIconButton {
            id: addBtn
            iconSource: QuiFontIcon.Add
            text: "添加数据集"
            onClicked: {
                editor.text = ""
                let pos = mapToItem(null, 0, 0)
                editor.x = pos.x + 20
                editor.y = pos.y + 20
                editor.open()
            }
        }
    }
    QuiEditor {
        id: editor
        description: "输入数据集名称"
        onEditTextChanged: function (datasetName) {
            if (dataManager && dataManager.isValidDatasetName(datasetName, -1).length === 0) {
                dataManager.addDataset(datasetName)
            }
        }
    }
}
