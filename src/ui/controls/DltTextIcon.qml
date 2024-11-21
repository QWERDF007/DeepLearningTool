import QtQuick
import QtQuick.Controls

Text {
    property int iconSource
    property int iconSize: 20
    property color iconColor: "#000000"
    id:control
    font.family: "Segoe Fluent Icons"
    font.pixelSize: iconSize
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
    color: iconColor
    text: (String.fromCharCode(iconSource).toString(16))
    FontLoader{
        source: "/Font/Icons.ttf"
    }
}
