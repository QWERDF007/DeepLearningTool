import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import quickui

Item {
    id: header
    property DataManager dataManager

    signal clearClicked

    RowLayout {
        anchors.fill: parent
        QuiText {
            text: "类别筛选"
            font: QuiFont.Subtitle
        }
        Item {
            Layout.fillWidth: true
        }
        QuiTextIconButton {
            id: addBtn
            iconSource: QuiFontIcon.Clear
            text: "清除筛选"
            onClicked: {
                if (!header.dataManager || !header.dataManager.globalFilter) {
                    return
                }

                header.dataManager.globalFilter.clearFilter(GlobalFilter.FilterType.LabelClass)
                header.dataManager.globalFilter.setFilterEnabled(GlobalFilter.FilterType.LabelClass, false)
                header.clearClicked()
            }
        }
    }
}