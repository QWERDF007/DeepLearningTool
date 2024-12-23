import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import  dltool.ui

T.ComboBox {
    id: control
    signal commit(string text)
    property bool disabled: false
    property color normalColor: DltColor.Primary
    property color hoverColor: DltColor.Hovered
    // property alias bg: _bg
    property alias content: _content
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)
    font: DltFont.Body
    leftPadding: padding + (!control.mirrored || !indicator || !indicator.visible ? 0 : indicator.width + spacing)
    rightPadding: padding + (control.mirrored || !indicator || !indicator.visible ? 0 : indicator.width + spacing)
    enabled: !disabled
    delegate: DltItemDelegate {
        width: ListView.view.width
        text: control.textRole ? (Array.isArray(control.model) ? modelData[control.textRole] : model[control.textRole]) : modelData
        palette.text: control.palette.text
        font: control.font
        palette.highlightedText: control.palette.highlightedText
        highlighted: control.highlightedIndex === index
        // hoverEnabled: control.hoverEnabled
        hoverEnabled: true
    }
    focusReason: Qt.MouseFocusReason
    // focusPolicy:Qt.TabFocus
    indicator: DltTextIcon {
        x: control.mirrored ? control.padding : control.width - width - control.padding
        y: control.topPadding + (control.availableHeight - height) / 2
        width: 28
        iconSource: DltFontIcon.ChevronDown
        iconColor: DltColor.FontPrimary
        iconSize: 15
        opacity: enabled ? 1 : 0.3
    }
    contentItem: T.TextField {
        id: _content
        property bool disabled: !control.editable
        leftPadding: !control.mirrored ? 10 : control.editable && activeFocus ? 3 : 1
        rightPadding: control.mirrored ? 10 : control.editable && activeFocus ? 3 : 1
        topPadding: 6 - control.padding
        bottomPadding: 6 - control.padding
        renderType: Text.NativeRendering
        selectionColor: Utils.withOpacity(DltColor.Primary,0.5)
        selectedTextColor: color
        text: control.editable ? control.editText : control.displayText
        enabled: control.editable
        autoScroll: control.editable
        font:control.font
        readOnly: control.down
        color: DltColor.FontPrimary
        inputMethodHints: control.inputMethodHints
        validator: control.validator
        selectByMouse: true
        verticalAlignment: Text.AlignVCenter
        background: DltControlBackground {
            id: _bg
            // border.width: 1
            bottomMargin: !control.editabl ? 1 : contentItem && contentItem.activeFocus ? 2 : 1
            color: {
                if (control.pressed) {
                    return control.normalColor
                }
                if (control.popup.visible) {
                    return control.hovered ? control.hoverColor : control.normalColor
                }
                return control.hovered ? control.hoverColor : control.normalColor
            }
        }

        Component.onCompleted: {
            forceActiveFocus()
        }
        Keys.onEnterPressed: (event)=> handleCommit(event)
        Keys.onReturnPressed:(event)=> handleCommit(event)
        function handleCommit(event){
            control.commit(control.editText)
            accepted()
        }
    }
    background: Rectangle {
        implicitWidth: 140
        implicitHeight: 32
        // border.color: DltColor.Background
        // border.width: 1
        visible: !control.flat || control.down
        radius: 4
        DltFocusRectangle{
            visible: control.visualFocus
            radius:4
            anchors.margins: -2
        }
        color:{
            if (control.pressed) {
                return control.normalColor
            }
            if (control.popup.visible) {
                return control.hovered ? control.hoverColor : control.normalColor
            }
            return control.hovered ? control.hoverColor : control.normalColor
        }
    }

    popup: T.Popup {
        id: _popup
        y: control.height
        width: control.width
        height: Math.min(contentItem.implicitHeight, control.Window.height - topMargin - bottomMargin)
        topMargin: 6
        bottomMargin: 6
        modal: true
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            highlightMoveDuration: 0
            boundsMovement: Flickable.StopAtBounds
            T.ScrollIndicator.vertical: ScrollIndicator { }
        }
        enter: Transition {
            NumberAnimation {
                property: "opacity"
                from:0
                to:1
                duration: 83
            }
        }
        exit:Transition {
            NumberAnimation {
                property: "opacity"
                from:1
                to:0
                duration: 83
            }
        }
        background: Rectangle{
            color: control.normalColor
            border.color: Qt.darker(1.2, control.normalColor)
            border.width: 1
            radius: 5
            DltShadow{
                radius: 5
            }
        }
    }
}
