import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.model

Rectangle {
    id: labelPage
    width: 1080
    height: 1920
    color: DltColor.Background

    DltSplitView {
        anchors.fill: parent
        anchors.margins: 5

        ModelView { // 模型面板
            SplitView.fillHeight: true
            SplitView.minimumWidth: 200
            SplitView.preferredWidth: 300
            SplitView.maximumWidth: parent.width / 2
            color: DltColor.Primary
            headerTitle: "模型训练:"
            addEnable: true
        }

        Item { // 训练面板
            SplitView.fillHeight: true
            SplitView.fillWidth: true
            // color: DltColor.Primary
        }
        
    }
}
