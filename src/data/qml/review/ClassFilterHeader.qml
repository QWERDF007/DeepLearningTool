import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data

Item {
    id: header
    property DataManager dataManager

    signal clearClicked

    RowLayout {
        anchors.fill: parent
        DltText {
            text: "类别筛选"
            font: DltFont.Subtitle
        }
        Item {
            Layout.fillWidth: true
        }
        DltTextIconButton {
            id: addBtn
            iconSource: DltFontIcon.Clear
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