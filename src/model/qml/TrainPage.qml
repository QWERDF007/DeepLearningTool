import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.model
import dltool.project

Rectangle {
    id: labelPage
    width: 1080
    height: 1920
    color: DltColor.Background

    property ModelManager modelManager: ProjectManager.currentProject ? ProjectManager.currentProject.modelManager : null

    DltSplitView {
        anchors.fill: parent
        anchors.margins: 5

        ModelView { // 左侧模型列表
            id: modelView
            SplitView.fillHeight: true
            SplitView.minimumWidth: 200
            SplitView.preferredWidth: 300
            SplitView.maximumWidth: parent.width / 2
            color: DltColor.Primary
            headerTitle: "模型训练:"
            addEnable: true
            modelManager: labelPage.modelManager
        }

        Item {
            id: trainPanel
            SplitView.fillHeight: true
            SplitView.fillWidth: true
            // color: DltColor.Background

            property var selectedModelId: modelView.currentModelId
            property string selectedNetworkStructure: modelView.currentNetworkStructure
            property IModel selectedModel: labelPage.modelManager && selectedModelId >= 0
                                           && selectedNetworkStructure.length > 0
                                           ? labelPage.modelManager.modelForId(
                                                 selectedModelId, selectedNetworkStructure) : null
            property ITrainParams trainParams: selectedModel && selectedModel.config
                                               ? selectedModel.config.trainParams : null

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
                            params: trainPanel.trainParams
                            emptyText: labelPage.modelManager ? qsTr("请选择模型后设置训练参数") : qsTr("请先打开项目")
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
                                text: modelView.currentModelId >= 0 ? qsTr("暂无训练结果") : qsTr("请选择模型")
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
    }
}
