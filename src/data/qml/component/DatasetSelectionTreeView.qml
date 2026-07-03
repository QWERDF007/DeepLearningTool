import QtQuick

import dltool.data
import quickui

DataSelectionTreeView {
    id: control

    property string roleTitle: ""

    title: roleTitle.length > 0 ? roleTitle : qsTr("数据集/类别")
    emptyText: qsTr("暂无数据集")
    showColor: true
}
