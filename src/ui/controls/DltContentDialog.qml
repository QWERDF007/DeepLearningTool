import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Window
import dltool.ui

DltPopup {
    id: control
    property string title: ""
    property string message: ""
    property string neutralText: "关闭"
    property string negativeText: "取消"
    property string positiveText: "确认"
    property int messageTextFormart: Text.AutoText
    property int delayTime: 100
    property int buttonFlags: DltDialogButtonFlag.NegativeButton | DltDialogButtonFlag.PositiveButton
    property var contentDelegate:  Component{ // 默认内容代理为空, 可重载
        Item{
        }
    }
    property var onNeutralClickListener // 按钮点击监听
    property var onNegativeClickListener
    property var onPositiveClickListener
    signal neutralClicked
    signal negativeClicked
    signal positiveClicked
    implicitWidth: 400
    implicitHeight: layout_content.height
    focus: true
    Component{
        id:com_message
        Flickable{
            id:sroll_message
            contentHeight: text_message.height
            contentWidth: width
            clip: true
            boundsBehavior:Flickable.StopAtBounds
            width: parent.width
            height: message === "" ? 0 : Math.min(text_message.height,300)
            ScrollBar.vertical: DltScrollBar {}
            DltText { // 消息
                id:text_message
                font: DltFont.Body
                wrapMode: Text.WrapAnywhere
                text:message
                width: parent.width
                topPadding: 4
                leftPadding: 20
                rightPadding: 20
                bottomPadding: 4
            }
        }
    }
    Rectangle { // 内容
        id:layout_content
        width: parent.width
        height: layout_column.childrenRect.height
        color: 'transparent'
        radius:5
        ColumnLayout{
            id:layout_column
            width: parent.width
            DltText { // 标题
                id:text_title
                font: DltFont.Title
                text:title
                topPadding: 20
                leftPadding: 20
                rightPadding: 20
                wrapMode: Text.WrapAnywhere
            }
            DltLoader { // 加载消息组件
                sourceComponent: com_message
                Layout.fillWidth: true
                Layout.preferredHeight: status===Loader.Ready ? item.height : 0
            }
            DltLoader { // 加载内容
                sourceComponent:control.visible ? control.contentDelegate : undefined
                Layout.fillWidth: true
                onStatusChanged: {
                    if(status===Loader.Ready){
                        Layout.preferredHeight = item.implicitHeight
                    }else{
                        Layout.preferredHeight = 0
                    }
                }
            }
            Rectangle { // 操作按钮布局
                id:layout_actions
                Layout.fillWidth: true
                Layout.preferredHeight: 60
                radius: 5
                color: Qt.rgba(243/255,243/255,243/255,1)
                RowLayout{
                    anchors
                    {
                        centerIn: parent
                        margins: spacing
                        fill: parent
                    }
                    spacing: 10
                    Item{
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        DltButton { // 关闭按钮
                            id:neutral_btn
                            visible: control.buttonFlags&DltDialogButtonFlag.NeutralButton
                            text: neutralText
                            width: parent.width
                            anchors.centerIn: parent
                            onClicked: {
                                if(control.onNeutralClickListener){
                                    control.onNeutralClickListener()
                                }else{
                                    neutralClicked()
                                    control.close()
                                }
                            }
                        }
                    }
                    Item{
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        DltButton { // 取消按钮
                            id:negative_btn
                            visible: control.buttonFlags&DltDialogButtonFlag.NegativeButton
                            width: parent.width
                            anchors.centerIn: parent
                            text: negativeText
                            onClicked: {
                                if(control.onNegativeClickListener){
                                    control.onNegativeClickListener()
                                }else{
                                    negativeClicked()
                                    control.close()
                                }
                            }
                        }
                    }
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        DltFilledButton { // 确认按钮
                            id:positive_btn
                            visible: control.buttonFlags&DltDialogButtonFlag.PositiveButton
                            text: positiveText
                            width: parent.width
                            anchors.centerIn: parent
                            onClicked: {
                                if(control.onPositiveClickListener){
                                    control.onPositiveClickListener()
                                }else{
                                    positiveClicked()
                                    control.close()
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
