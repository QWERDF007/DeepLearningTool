import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebEngine
import dltool.ui
import dltool.data
import dltool.model
import "../component"
import quickui

Item {
    id: control

    property ModelManager modelManager: null
    property DataManager dataManager: null
    property int currentModelId: -1
    property string currentModelUuid: ""
    property string currentModelName: ""
    property string currentFrameworkName: ""
    property string currentModelArchitecture: ""
    property IModel selectedModel: modelManager && currentModelUuid.length > 0 ? modelManager.modelForUuid(currentModelUuid) : null
    property ITrainParams trainParams: selectedModel && selectedModel.config ? selectedModel.config.trainParams : null
    property url tensorBoardUrl: ""

    function openTensorBoard() {
        tensorBoardUrl = modelManager && currentModelUuid.length > 0
                         ? modelManager.startTensorBoard(currentModelUuid) : ""
    }

    onCurrentModelUuidChanged: openTensorBoard()
    Component.onCompleted: openTensorBoard()
    onTensorBoardUrlChanged: tensorBoardReload.restart()

    Timer {
        id: tensorBoardReload
        interval: 1500
        onTriggered: tensorBoardView.reload()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        TabBar {
            id: trainTabBar
            Layout.fillWidth: true

            background: Item{}

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
                    dataManager: control.dataManager
                    selectedModel: control.selectedModel
                    emptyText: control.modelManager ? qsTr("请选择模型后设置训练参数") : qsTr("请先打开项目")
                }
            }

            Item {
                WebEngineView {
                    id: tensorBoardView
                    anchors.fill: parent
                    url: control.tensorBoardUrl
                    visible: control.tensorBoardUrl.toString().length > 0
                }

                QuiText {
                    anchors.centerIn: parent
                    text: control.currentModelUuid.length > 0 ? qsTr("无法启动 TensorBoard") : qsTr("请选择模型")
                    color: QuiColor.FontDark
                    visible: !tensorBoardView.visible
                }
            }
        }

    }
}
