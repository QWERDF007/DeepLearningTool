import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.model
import dltool.ui
import quickui

/*
 * 实例图像网格 Base：
 * - 收拢缩放控制、滚轮、选择联动与 thumbnail delegate 公共逻辑。
 * - 子类覆盖 formatMetric(model) 决定单元格指标文本。
 */
Rectangle {
    id: control
    color: QuiColor.Primary
    property ModelEvaluationViewModel evaluation: null
    property real thumbnailScale: 1.0
    property int spacing: 8
    property int baseCellWidth: 180
    property int baseCellHeight: 150
    property string title: qsTr("实例图像")

    function formatMetric(model) {
        var score = Number(model.score)
        return isFinite(score) ? score.toFixed(3) : "—"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 5
        anchors.rightMargin: 0
        anchors.topMargin: 5
        anchors.bottomMargin: 5

        // 顶栏 Header 容器（包含标题与缩放控制）
        RowLayout {
            id: headerHost
            Layout.fillWidth: true
            Layout.rightMargin: 5
            Layout.preferredHeight: 32

            QuiText {
                Layout.fillWidth: true
                text: control.title
                font: QuiFont.Subtitle
            }

            QuiSpinEditor {
                id: zoomEditor
                Layout.preferredWidth: 122
                Layout.preferredHeight: 32
                label: ""
                value: control.thumbnailScale
                minValue: 0.1
                maxValue: 10
                step: 0.05
                decimals: 2
                onEditingFinished: {
                    const next = Math.max(minValue, Math.min(maxValue, Number(value)))
                    if (isFinite(next))
                        control.thumbnailScale = next
                }
            }

            QuiTextIconButton {
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                iconSize: 18
                iconSource: QuiFontIcon.Refresh
                text: qsTr("重置缩放倍率")
                normalColor: QuiColor.Button
                hoverColor: Qt.lighter(QuiColor.Button, 1.2)
                pressedColor: Qt.lighter(QuiColor.Button, 1.3)
                onClicked: control.thumbnailScale = 1.0
            }
        }

        Item {
            id: contentHost
            Layout.fillWidth: true
            Layout.fillHeight: true

            GridView {
                id: grid
                anchors.fill: parent
                boundsBehavior: Flickable.StopAtBounds
                cellWidth: Math.round(control.baseCellWidth * control.thumbnailScale) + control.spacing
                cellHeight: Math.round(control.baseCellHeight * control.thumbnailScale) + control.spacing
                clip: true
                focus: true
                keyNavigationEnabled: true
                keyNavigationWraps: false
                ScrollBar.vertical: QuiScrollBar { }

                WheelHandler {
                    id: thumbnailZoomWheelHandler
                    onWheel: function (wheel) {
                        if (!(wheel.modifiers & Qt.ControlModifier)) {
                            const maxContentY = Math.max(0, grid.contentHeight - grid.height)
                            const wheelDelta = Number(wheel.angleDelta.y) / 120
                            const scrollStep = wheelDelta * grid.cellHeight * 2.0
                            grid.contentY = Math.max(0, Math.min(maxContentY, grid.contentY - scrollStep))
                            wheel.accepted = true
                            return
                        }
                        const delta = wheel.angleDelta.y > 0 ? 0.05 : -0.05
                        const next = Math.max(0.1, Math.min(10, control.thumbnailScale + delta))
                        control.thumbnailScale = Math.round(next * 100) / 100
                        wheel.accepted = true
                    }
                }

                model: control.evaluation && control.evaluation.hasInstanceEvents
                       ? control.evaluation.filteredInstances : null
                onCurrentIndexChanged: {
                    if (control.evaluation && currentIndex >= 0)
                        control.evaluation.selectInstance(currentIndex)
                    if (currentIndex >= 0)
                        positionViewAtIndex(currentIndex, GridView.Contain)
                }
                onCountChanged: {
                    if (count === 0)
                        currentIndex = -1
                    else if (control.evaluation && control.evaluation.selectedInstanceRow >= 0
                             && control.evaluation.selectedInstanceRow < count)
                        currentIndex = control.evaluation.selectedInstanceRow
                    else if (currentIndex < 0)
                        currentIndex = 0
                }
                delegate: Rectangle {
                    width: Math.min(Math.round(control.baseCellWidth * control.thumbnailScale), Math.max(50, grid.width - control.spacing - 10))
                    height: Math.round(control.baseCellHeight * control.thumbnailScale)
                    readonly property bool consistentStatus: model.statusKind === EvaluationInstanceModel.StatusTruePositive
                                                              || model.statusKind === EvaluationInstanceModel.StatusTrueNegative

                    function formatMetric() {
                        return control.formatMetric(model)
                    }

                    color: QuiColor.Background
                    radius: 4
                    border.width: 2
                    border.color: model.selected ? QuiColor.Highlight : QuiColor.Transparent
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 5
                        spacing: 2

                        EvaluationInstanceThumbnail {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumHeight: 1
                            record: ({imagePath: model.imagePath, thumbnailUrl: model.thumbnailUrl,
                                      imageWidth: model.imageWidth,
                                      imageHeight: model.imageHeight,
                                      gtGeometry: model.gtGeometry, predGeometry: model.predGeometry,
                                      gtBounds: model.gtBounds, predBounds: model.predBounds,
                                      gtMaskUrl: model.gtMaskUrl, predMaskUrl: model.predMaskUrl,
                                      gtClassColor: model.gtClassColor, predClassColor: model.predClassColor})
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 16
                            spacing: 4

                            QuiText {
                                id: metricText
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                                text: formatMetric()
                                font: QuiFont.Caption
                                color: QuiColor.FontPrimary
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }

                            QuiTextIcon {
                                Layout.preferredWidth: 16
                                Layout.preferredHeight: 16
                                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                                iconSource: consistentStatus ? QuiFontIcon.CheckMark : QuiFontIcon.Cancel
                                iconColor: consistentStatus ? "#43a047" : "#e53935"
                                iconSize: 14
                            }
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        propagateComposedEvents: true
                        onClicked: {
                            grid.forceActiveFocus()
                            grid.currentIndex = index
                            if (control.evaluation)
                                control.evaluation.selectInstance(index)
                        }
                    }
                }
            }

            Connections {
                target: control.evaluation
                function onSelectedInstanceChanged() {
                    if (!control.evaluation)
                        return
                    var selected = control.evaluation.selectedInstanceRow
                    if (selected >= 0 && selected < grid.count) {
                        grid.currentIndex = selected
                        grid.positionViewAtIndex(selected, GridView.Contain)
                    } else if (selected < 0) {
                        grid.currentIndex = -1
                    }
                }
            }

            QuiText {
                anchors.fill: parent
                anchors.rightMargin: 5
                visible: !control.evaluation || !control.evaluation.hasInstanceEvents
                         || grid.count === 0
                text: !control.evaluation || !control.evaluation.hasInstanceEvents
                      ? qsTr("当前方法没有实例事件") : qsTr("当前过滤条件下没有实例")
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: QuiColor.FontDark
            }
        }
    }

    onThumbnailScaleChanged: {
        if (zoomEditor.value !== thumbnailScale)
            zoomEditor.value = thumbnailScale
    }
}
