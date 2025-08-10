import QtQuick
import QtQuick.Controls

Canvas {
        id: crosshairCanvas
        anchors.fill: parent
        z: 100
        property point mousePos

        property var mousePosChangedDelegate: function() {
            requestPaint()
        }
        onMousePosChanged: {
            if (mousePosChangedDelegate) {
                mousePosChangedDelegate()
            }
        }

        onPaint: {
            var ctx = crosshairCanvas.getContext("2d")
            ctx.clearRect(0, 0, width, height)
            if (mousePos.x < 0 || mousePos.y < 0)
                return

            ctx.save()
            ctx.setLineDash([6, 6])
            ctx.strokeStyle = "#00FFFF"
            ctx.lineWidth = 1

            // 画竖线
            ctx.beginPath()
            ctx.moveTo(mousePos.x, 0)
            ctx.lineTo(mousePos.x, height)
            ctx.stroke()

            // 画横线
            ctx.beginPath()
            ctx.moveTo(0, mousePos.y)
            ctx.lineTo(width, mousePos.y)
            ctx.stroke()

            ctx.restore()
        }
    }