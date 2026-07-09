import QtQuick

import dltool.feature

ClassificationLabelCanvas {
    id: labelCanvas

    property FeatureManager featureManager
    property alias showBoundingBoxes: annotationLayer.showBoundingBoxes
    property alias actions: annotationLayer.actions
    labelClassShortcutHandler: function(classId, event) {
        let selectedIds = selectedLabelIds()
        return selectedIds.length > 0
                ? applyClassToSelectedLabels(selectedIds, classId)
                : applyClassToCurrentImage(classId)
    }

    badgeDataProvider: function() {
        if (!dataManager || !imageInstances || imageInstances.currentImageId < 0) {
            return null
        }

        let data = dataManager.getImageLevelLabelData(imageInstances.currentImageId)
        return data && data.label_class_id !== undefined ? data : null
    }

    LabelCanvasAnnotationLayer {
        id: annotationLayer
        canvas: labelCanvas
        rectangleToolAvailable: false
        polygonRegionMode: true
    }

    function selectedLabelIds() {
        if (!imageLabelsList) {
            return []
        }

        let ids = imageLabelsList.getSelectedLabelIds()
        return ids ? ids : []
    }

    function applyClassToSelectedLabels(labelIds, classId) {
        if (!dataManager || !labelIds || labelIds.length <= 0 || classId < 0) {
            return false
        }

        let classIds = new Array(labelIds.length).fill(classId)
        dataManager.updateLabelsClass(labelIds, classIds)
        return true
    }

    function applyClassToCurrentImage(classId) {
        if (!dataManager || !imageInstances || classId < 0) {
            return false
        }

        let imageId = imageInstances.currentImageId !== undefined ? Number(imageInstances.currentImageId) : -1
        if (imageId < 0) {
            return false
        }

        return dataManager.setImageLabelClass(imageId, classId)
    }
}
