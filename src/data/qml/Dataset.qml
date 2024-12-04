import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui

Rectangle {
    width: 1080
    height: 1920
    color: DltColor.Background
    DltSplitView {
        anchors.fill: parent
        anchors.margins: 5
        DltSplitView {
            SplitView.fillHeight: true
            SplitView.minimumWidth: 200
            SplitView.maximumWidth: parent.width / 2
            SplitView.preferredWidth: 300
            orientation: Qt.Vertical
            Rectangle {
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.maximumHeight: 500
                SplitView.preferredHeight: 300
            }
            Rectangle {
                SplitView.fillWidth: true
                SplitView.fillHeight: true
                SplitView.minimumHeight: 200
            }
        }
        Rectangle {
            SplitView.fillHeight: true
            SplitView.fillWidth: true
        }
    }
}
