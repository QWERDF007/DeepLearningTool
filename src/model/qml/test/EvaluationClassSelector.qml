import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import quickui

QuiComboBox {
    id: control

    normalColor: QuiColor.Button
    property var classOptions: []
    property int currentClassId: -1
    signal classChanged(int newClassId)

    property bool _updating: false

    implicitWidth: 200
    implicitHeight: 32
    model: classOptions

    displayText: {
        if (currentIndex < 0 || currentIndex >= classOptions.length)
            return ""
        var current = classOptions[currentIndex]
        return current && current.name !== undefined ? String(current.name) : ""
    }

    Component.onCompleted: syncCurrentIndex()

    onClassOptionsChanged: syncCurrentIndex()
    onCurrentClassIdChanged: syncCurrentIndex()

    function syncCurrentIndex() {
        if (_updating)
            return
        _updating = true
        currentIndex = -1
        for (var index = 0; index < classOptions.length; ++index) {
            var option = classOptions[index]
            if (option && Number(option.classId) === Number(currentClassId)) {
                currentIndex = index
                _updating = false
                return
            }
        }
        _updating = false
    }

    onActivated: function(index) {
        if (_updating || index < 0 || index >= classOptions.length)
            return
        var option = classOptions[index]
        if (!option)
            return
        var classId = Number(option.classId)
        if (classId !== Number(currentClassId))
            classChanged(classId)
    }

    delegate: ItemDelegate {
        id: delegateItem
        width: ListView.view ? ListView.view.width : control.width
        height: 32
        highlighted: control.highlightedIndex === index
        hoverEnabled: control.hoverEnabled

        property var option: modelData
        property color delegateColor: option && option.color !== undefined
                                      ? option.color : QuiColor.Transparent

        contentItem: RowLayout {
            spacing: 8
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8

            Rectangle {
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                radius: 3
                color: delegateItem.delegateColor
                border.width: 1
                border.color: Qt.darker(delegateItem.delegateColor, 1.3)
            }

            QuiText {
                text: delegateItem.option && delegateItem.option.name !== undefined
                      ? String(delegateItem.option.name) : ""
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
        }

        background: Rectangle {
            color: delegateItem.highlighted ? QuiColor.Hovered : QuiColor.Transparent
        }
    }

    contentItem: Item {
        implicitHeight: 32

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: control.indicator ? control.indicator.width + 8 : 8
            spacing: 8

            Rectangle {
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                radius: 3
                visible: control.currentIndex >= 0 && control.currentIndex < control.classOptions.length
                color: {
                    if (!visible)
                        return QuiColor.Transparent
                    var option = control.classOptions[control.currentIndex]
                    return option && option.color !== undefined ? option.color : QuiColor.Transparent
                }
                border.width: visible ? 1 : 0
                border.color: visible ? Qt.darker(color, 1.3) : QuiColor.Transparent
            }

            QuiText {
                text: control.displayText
                Layout.fillWidth: true
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}
