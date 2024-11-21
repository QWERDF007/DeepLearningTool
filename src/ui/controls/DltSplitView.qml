import QtQuick
import QtQuick.Controls

SplitView {
    id: splitView
    property int backgroundWidth: splitView.orientation === Qt.Horizontal ? 4 : splitView.width
    property int backgroundHeight: splitView.orientation === Qt.Horizontal ? splitView.height : 4
    property color backgroundColor: "#353535"
    property int handleWidth: splitView.orientation === Qt.Horizontal ? 2 : 12
    property int handleHeight: splitView.orientation === Qt.Horizontal ? 12 : 2
    property color handleColor: "white"
    property int maskWidth: splitView.orientation === Qt.Horizontal ? 12 : splitView.width
    property int maskHeight: splitView.orientation === Qt.Horizontal ? splitView.height : 12

    handle: Rectangle {
        id: handleDelegate
        // 设置 width height 无法正常显示
//        width: splitView.backgroundWidth
//        height: splitView.backgroundHeight
        implicitWidth: splitView.backgroundWidth
        implicitHeight: splitView.backgroundHeight
        color: SplitHandle.pressed ? Qt.lighter(backgroundColor, 1.3) : (SplitHandle.hovered ? Qt.lighter(backgroundColor, 1.2) : backgroundColor)

        Rectangle {
            width: splitView.handleWidth
            height: splitView.handleHeight
            color: splitView.handleColor
            anchors.centerIn: parent
        }
        containmentMask: Item {
            x: (handleDelegate.width - width) / 2
            width: splitView.maskWidth
            height: splitView.maskHeight
        }
    }
}
