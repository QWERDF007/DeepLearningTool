import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import dltool.ui
import dltool.model
import "../component"

Item {
    id: control

    property ModelManager modelManager: null
    property var currentModelId: -1
    property string currentNetworkStructure: ""
    property IModel selectedModel: modelManager && currentModelId >= 0 && currentNetworkStructure.length > 0 ? modelManager.modelForId(currentModelId, currentNetworkStructure) : null
    property ITrainParams trainParams: selectedModel && selectedModel.config ? selectedModel.config.trainParams : null

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        TabBar {
            id: trainTabBar
            Layout.fillWidth: true

            DltTabButton {
                text: qsTr("训练参数设置")
                textColor: trainTabBar.currentIndex === 0 ? DltColor.Highlight : DltColor.FontPrimary
            }

            DltTabButton {
                text: qsTr("训练结果")
                textColor: trainTabBar.currentIndex === 1 ? DltColor.Highlight : DltColor.FontPrimary
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
                color: DltColor.Primary
                radius: 4
                border.color: DltColor.Border

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10

                    DltText {
                        Layout.fillWidth: true
                        text: qsTr("训练结果")
                        font: DltFont.Title
                        color: DltColor.FontPrimary
                    }

                    DltText {
                        Layout.fillWidth: true
                        text: control.currentModelId >= 0 ? qsTr("暂无训练结果") : qsTr("请选择模型")
                        color: DltColor.FontDark
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
