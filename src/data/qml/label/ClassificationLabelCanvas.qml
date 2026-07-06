import QtQuick

import dltool.ui
import quickui

LabelCanvasBase {
    id: labelCanvas

    property var classificationBadgeData: null
    property var badgeDataProvider: function() {
        if (!imageLabelsList || imageLabelsList.rowCount() <= 0) {
            return null
        }
        return imageLabelsList.getData(0)
    }

    onBadgeDataProviderChanged: refreshClassificationBadge()
    onImageInstancesChanged: refreshClassificationBadge()
    onImageLabelsListChanged: refreshClassificationBadge()
    onLabelClassesChanged: refreshClassificationBadge()

    Connections {
        target: imageInstances
        function onCurrentImageChanged() { labelCanvas.refreshClassificationBadge() }
    }

    Connections {
        target: imageLabelsList
        function onRowsInserted(parent, first, last) { labelCanvas.refreshClassificationBadge() }
        function onRowsRemoved(parent, first, last) { labelCanvas.refreshClassificationBadge() }
        function onDataChanged(topLeft, bottomRight, roles) { labelCanvas.refreshClassificationBadge() }
        function onModelReset() { labelCanvas.refreshClassificationBadge() }
    }

    Connections {
        target: labelClasses
        function onDataChanged(topLeft, bottomRight, roles) { labelCanvas.refreshClassificationBadge() }
        function onModelReset() { labelCanvas.refreshClassificationBadge() }
    }

    Rectangle {
        id: classificationBadge
        visible: labelCanvas.imageView.image.status === Image.Ready
                 && labelCanvas.classificationBadgeData !== null
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 4
        width: Math.max(56, badgeText.implicitWidth + 14)
        height: Math.max(22, badgeText.implicitHeight + 6)
        radius: 2
        color: labelCanvas.classificationBadgeData && labelCanvas.classificationBadgeData.color
               ? labelCanvas.classificationBadgeData.color
               : labelCanvas.drawingColor
        border.color: Qt.rgba(1, 1, 1, 0.18)
        border.width: 1
        z: 30

        QuiText {
            id: badgeText
            anchors.fill: parent
            anchors.leftMargin: 7
            anchors.rightMargin: 7
            anchors.topMargin: 2
            anchors.bottomMargin: 2
            text: labelCanvas.badgeLabelText()
            textColor: "white"
            font.pixelSize: 12
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    function refreshClassificationBadge() {
        let provider = badgeDataProvider
        if (!provider) {
            classificationBadgeData = null
            return
        }

        let data = provider()
        classificationBadgeData = data && data.label_class_id !== undefined ? data : null
    }

    function badgeLabelText() {
        if (!classificationBadgeData) {
            return ""
        }

        let name = classificationBadgeData.label_class_name
        return name === undefined || name === null ? "" : String(name)
    }

    Component.onCompleted: refreshClassificationBadge()
}
