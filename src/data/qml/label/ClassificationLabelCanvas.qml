import QtQuick

import dltool.ui
import quickui

LabelCanvasBase {
    id: labelCanvas

    property var classificationBadgeData: null
    property string classificationBadgeName: ""
    property string classificationBadgeColor: "#ff0000"
    labelClassShortcutHandler: function(classId, event) {
        return applyClassToCurrentImage(classId)
    }
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
        color: labelCanvas.classificationBadgeColor
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
            clearClassificationBadge()
            return
        }

        let data = provider()
        if (!data || data.label_class_id === undefined) {
            clearClassificationBadge()
            return
        }

        classificationBadgeData = data
        classificationBadgeName = stringField(data.label_class_name)

        let color = stringField(data.color)
        if (color.length > 0) {
            classificationBadgeColor = color
        } else {
            classificationBadgeColor = "#ff0000"
        }
    }

    function badgeLabelText() {
        return classificationBadgeName
    }

    function clearClassificationBadge() {
        classificationBadgeData = null
        classificationBadgeName = ""
        classificationBadgeColor = "#ff0000"
    }

    function stringField(value) {
        if (value === undefined || value === null) {
            return ""
        }
        return String(value)
    }

    function applyClassToCurrentImage(classId) {
        if (!dataManager || !imageInstances || classId < 0) {
            return false
        }

        let imageId = imageInstances.currentImageId !== undefined ? Number(imageInstances.currentImageId) : -1
        if (imageId < 0) {
            return false
        }

        let labelData = currentImageLabelData()
        let labelId = labelData && labelData.label_id !== undefined ? Number(labelData.label_id) : -1
        let currentClassId = labelData && labelData.label_class_id !== undefined ? Number(labelData.label_class_id) : -1

        if (labelId >= 0) {
            if (currentClassId !== classId) {
                dataManager.updateLabelsClass([labelId], [classId])
            }
            return true
        }

        return dataManager.addLabel(imageId, classId, {})
    }

    function currentImageLabelData() {
        if (!imageLabelsList || imageLabelsList.rowCount() <= 0) {
            return null
        }
        return imageLabelsList.getData(0)
    }

    Component.onCompleted: refreshClassificationBadge()
}
