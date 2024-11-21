import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DltPage {
    default property alias content: container.data
    Flickable{
        clip: true
        anchors.fill: parent
        ScrollBar.vertical: DltScrollBar {
            snapMode: ScrollBar.SnapAlways
        }
        boundsBehavior: Flickable.StopAtBounds
        contentHeight: container.height
        ColumnLayout{
            id:container
            width: parent.width
        }
    }
}
