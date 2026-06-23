import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import dltool.ui
import dltool.model
import "../component"
import quickui

Item {
    id: control

    property ModelManager modelManager: null
    property var currentModelId: -1
    property string currentModelUuid: ""
    property string currentModelName: ""
    property string currentNetworkStructure: ""
    property IModel selectedModel: modelManager && currentModelUuid.length > 0 ? modelManager.modelForUuid(currentModelUuid) : null
    property ITrainParams trainParams: selectedModel && selectedModel.config ? selectedModel.config.trainParams : null

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        TabBar {
            id: trainTabBar
            Layout.fillWidth: true

            QuiTabButton {
                text: qsTr("训练参数设置")
                textColor: trainTabBar.currentIndex === 0 ? QuiColor.Highlight : QuiColor.FontPrimary
            }

            QuiTabButton {
                text: qsTr("训练结果")
                textColor: trainTabBar.currentIndex === 1 ? QuiColor.Highlight : QuiColor.FontPrimary
            }
        }

        StackLayout {
            id: trainStack
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: trainTabBar.currentIndex

            Item {
                ParamsForm {
                    anchors.fill: parent
                    params: control.trainParams
                    emptyText: control.modelManager ? qsTr("请选择模型后设置训练参数") : qsTr("请先打开项目")
                }
            }

            Rectangle {
                color: QuiColor.Primary
                radius: 4
                border.color: QuiColor.Border

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10

                    QuiText {
                        Layout.fillWidth: true
                        text: qsTr("训练结果")
                        font: QuiFont.Title
                        color: QuiColor.FontPrimary
                    }

                    QuiText {
                        Layout.fillWidth: true
                        text: control.currentModelUuid.length > 0 ? qsTr("暂无训练结果") : qsTr("请选择模型")
                        color: QuiColor.FontDark
                        wrapMode: Text.Wrap
                    }

                    Item {
                        Layout.fillHeight: true
                    }
                }
            }
        }

    }
}
