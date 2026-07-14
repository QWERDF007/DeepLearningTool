import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebEngine
import dltool.ui
import dltool.data
import dltool.model
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
    property bool tensorBoardPanelVisible: visible && trainTabBar.currentIndex === 1
    property var currentModelData: ({})
    property var trainState: currentModelData && currentModelData.extra_data
                              ? currentModelData.extra_data.train || ({}) : ({})
    property var testState: currentModelData && currentModelData.extra_data
                             ? currentModelData.extra_data.test || ({}) : ({})

    function openTensorBoard() {
        if (!tensorBoardPanelVisible || !modelManager || currentModelUuid.length === 0) {
            tensorBoardUrl = ""
            return
        }

        tensorBoardUrl = modelManager.startTensorBoard(currentModelUuid)
        tensorBoardReload.restart()
    }

    function refreshCurrentModelData() {
        currentModelData = modelManager && currentModelUuid.length > 0
                           ? modelManager.modelRecordForUuid(currentModelUuid) : ({})
    }

    onCurrentModelUuidChanged: {
        refreshCurrentModelData()
        tensorBoardUrl = ""
        if (tensorBoardPanelVisible)
            openTensorBoard()
    }
    onModelManagerChanged: {
        refreshCurrentModelData()
        openTensorBoard()
    }
    Component.onCompleted: {
        refreshCurrentModelData()
        openTensorBoard()
    }
    onTensorBoardPanelVisibleChanged: openTensorBoard()

    Connections {
        target: control.modelManager

        function onModelExtraDataChanged(modelUuid) {
            if (modelUuid === control.currentModelUuid)
                control.refreshCurrentModelData()
        }
    }

    Timer {
        id: tensorBoardReload
        interval: 1500
        onTriggered: {
            if (tensorBoardView.visible)
                tensorBoardView.reload()
        }
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

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 10

                ColumnLayout { // 状态面板
                    Layout.fillHeight: true
                    Layout.minimumWidth: 250
                    Layout.preferredWidth: 300
                    Layout.maximumWidth: 360
                    spacing: 8

                    TrainStatusPanel {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 230
                        stateData: control.trainState
                    }

                    EvaluationStatusPanel {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 170
                        metricText: control.testState.metrics || ""
                    }
                }

                Item { // TensorBoard 区域
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumWidth: 280

                    WebEngineView {
                        id: tensorBoardView
                        anchors.fill: parent
                        url: control.tensorBoardUrl
                        visible: control.tensorBoardPanelVisible && control.tensorBoardUrl.toString().length > 0
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

}
