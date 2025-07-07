import QtQuick
import QtQuick.Controls


Rectangle{
    id:control
    property bool isDot: false
    property bool showZero: false
    property int count: 0
    property int max: 99
    color: Qt.rgba(255/255,77/255,79/255,1)
    width: {
        if(isDot)
            return 10
        return content_text.implicitWidth + 12
    }
    height: {
        if(isDot)
            return 10
        return 20
    }
    radius: {
        if(isDot)
            return 5
        return 10
    }
    border.width: 1
    border.color: Qt.rgba(1,1,1,1)
    visible: {
        if(showZero)
            return true
        return count!==0
    }
    DltText{
        id: content_text
        anchors.centerIn: parent
        color: Qt.rgba(1,1,1,1)
        visible: !isDot
        text:{
            return count <= max ? count: "%1+".arg(max)
        }
    }
}
