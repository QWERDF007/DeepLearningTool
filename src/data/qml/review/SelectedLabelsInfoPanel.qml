import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import quickui

Rectangle {
    id: root
    color: QuiColor.Primary

    // 公共属性
    property DataManager dataManager
    property SelectedLabelsInfoModel info: dataManager ? dataManager.selectedLabelsInfo : null
    property LabelClassesModel labelClasses: dataManager ? dataManager.labelClasses : null

    // 可见性绑定：只有当有选中项时才显示
    visible: info ? info.hasSelection : false

    QuiMenu {
        id: menu
        width: 200
        QuiMenuItem {
            text: "复制"
            onTriggered: {
                copyboard.selectAll()
                copyboard.copy()
            }
        }
    }
    TextEdit {
        id: copyboard
        visible: false
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5

        SelectedLabelsInfoHeader {
            Layout.fillWidth: true
            height: 32
            onClicked: if (root.info) root.info.clearSelection()
        }

        // 行1: 选中数量统计
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            QuiText {
                Layout.leftMargin: 5
                Layout.rightMargin: 5
                text: "选中:"
                textColor: QuiColor.FontDark
            }

            QuiText {
                Layout.fillWidth: true
                Layout.leftMargin: 5
                Layout.rightMargin: 5
                text: root.info ? root.info.selectedCount + "/" + root.info.totalCount : "0/0"
                
            }
        }

        InfoTextItem {
            title:  "数据集"
            text: root.info ? root.info.datasetText : ""
            onClicked: {
                copyboard.text = text
                menu.popup()
            }
        }

        InfoTextItem {
            title:  "图像"
            text: root.info ? root.info.imageText : ""
            onClicked: {
                copyboard.text = text
                menu.popup()
            }
        }

        InfoTextItem {
            title:  "图像Tag"
            text: root.info ? root.info.tagText : ""
            onClicked: {
                copyboard.text = text
                menu.popup()
            }
        }

        // 行4: 类别选择器
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            QuiText {
                Layout.leftMargin: 5
                Layout.rightMargin: 5
                text: "类别:"
                textColor: QuiColor.FontDark
            }

            LabelClassSelector {
                id: classSelector
                Layout.fillWidth: true
                Layout.leftMargin: 5
                Layout.rightMargin: 5
                Layout.preferredHeight: 32

                labelClassesModel: root.labelClasses
                currentClassId: root.info ? root.info.currentClassId : -1
                showMultipleDifferent: root.info ? root.info.multipleClasses : false
                multipleDifferentText: "不同类别"

                onClassChanged: function(newClassId) {
                    if (root.info) {
                        root.info.changeSelectedLabelsClass(newClassId)
                    }
                }
            }
        }
        // 填充剩余空间
        Item {
            Layout.fillHeight: true
        }
    }

}
