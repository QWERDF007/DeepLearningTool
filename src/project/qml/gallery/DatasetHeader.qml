import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.project

Item {
    id: header
    property DataManager dataManager
    RowLayout {
        anchors.fill: parent
        DltText {
            text: "数据集:"
            font: DltFont.Subtitle
        }
        Item {
            Layout.fillWidth: true
        }
        DltTextIconButton {
            id: addBtn
            iconSource: DltFontIcon.Add
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
    DltEditor {
        id: editor
        description: "输入数据集名称"
        onEditTextChanged: function (datasetName) {
            if (dataManager) {
                dataManager.addDataset(datasetName)
            }
        }
    }
}
