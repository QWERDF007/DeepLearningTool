import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.model
import dltool.ui
import quickui

Item {
    id: control
    property ModelEvaluationViewModel evaluation: null
    property real thumbnailScale: 1.0

    ColumnLayout {
        anchors.fill: parent
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            QuiText { text: qsTr("缩略图") }
            QuiSlider {
                id: zoomSlider
                Layout.fillWidth: true
                from: 0.75
                to: 1.75
                stepSize: 0.05
                value: control.thumbnailScale
                onMoved: control.thumbnailScale = value
            }
            QuiText {
                Layout.preferredWidth: 52
                horizontalAlignment: Text.AlignRight
                text: qsTr("%1×").arg(control.thumbnailScale.toFixed(2))
            }
            QuiButton {
                text: qsTr("重置")
                onClicked: control.thumbnailScale = 1.0
            }
        }

        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            cellWidth: 180 * control.thumbnailScale
            cellHeight: 150 * control.thumbnailScale
            clip: true
            focus: true
            keyNavigationEnabled: true
            keyNavigationWraps: false
            highlightFollowsCurrentItem: true
            ScrollBar.vertical: QuiScrollBar { }
            model: control.evaluation && control.evaluation.hasInstanceEvents
                   ? control.evaluation.filteredInstances : null
            highlight: Rectangle {
                color: "transparent"
                border.width: 2
                border.color: QuiColor.Highlight
                radius: 2
            }
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
                width: grid.cellWidth - 6
                height: grid.cellHeight - 6
                color: QuiColor.Primary
                border.color: model.status === "true_positive" ? "#43a047"
                              : model.status === "class_mismatch" ? "#fb8c00" : "#e53935"
                EvaluationInstanceThumbnail {
                    anchors.fill: parent
                    anchors.margins: 2
                    record: ({imagePath: model.imagePath, thumbnailUrl: model.thumbnailUrl,
                              imageWidth: model.imageWidth,
                              imageHeight: model.imageHeight, cropBounds: model.cropBounds,
                              gtGeometry: model.gtGeometry, predGeometry: model.predGeometry,
                              gtBounds: model.gtBounds, predBounds: model.predBounds,
                              gtOverlayBounds: model.gtOverlayBounds, predOverlayBounds: model.predOverlayBounds,
                              gtOverlayPoints: model.gtOverlayPoints, predOverlayPoints: model.predOverlayPoints,
                              gtMaskUrl: model.gtMaskUrl, predMaskUrl: model.predMaskUrl,
                              gtClassColor: model.gtClassColor, predClassColor: model.predClassColor})
                }
                QuiText {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.margins: 4
                    text: model.imageName + "\n" + (model.statusText || model.status)
                    color: "white"
                    style: Text.Outline
                    styleColor: "black"
                }
                Rectangle {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 4
                    radius: 3
                    color: model.status === "true_positive" ? "#43a047"
                          : model.status === "class_mismatch" ? "#fb8c00" : "#e53935"
                    implicitWidth: statusBadge.implicitWidth + 10
                    implicitHeight: statusBadge.implicitHeight + 4
                    QuiText {
                        id: statusBadge
                        anchors.centerIn: parent
                        text: model.statusText || model.status
                        color: "white"
                        font.pixelSize: 10
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
                    onWheel: function (wheel) {
                        if (wheel.modifiers & Qt.ControlModifier) {
                            var step = wheel.angleDelta.y > 0 ? 0.05 : -0.05
                            control.thumbnailScale = Math.max(0.75,
                                                               Math.min(1.75, control.thumbnailScale + step))
                            wheel.accepted = true
                        } else {
                            wheel.accepted = false
                        }
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
            Layout.fillWidth: true
            Layout.fillHeight: true
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
