import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import quickui
import "../component"

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
            onClicked: createDatasetDialog.openForm()
        }
    }

    DataNameFormDialog {
        id: createDatasetDialog
        title: "创建数据集"
        fieldLabel: "数据集名称"
        placeholderText: "输入数据集名称"
        emptyError: "请输入数据集名称"
        nameValidator: function(datasetName) {
            if (!header.dataManager) {
                return "数据集管理器不可用"
            }
            return header.dataManager.isValidDatasetName(datasetName, -1)
        }
        onSubmitted: function(datasetName) {
            if (header.dataManager) {
                header.dataManager.addDataset(datasetName)
            }
        }
    }
}
