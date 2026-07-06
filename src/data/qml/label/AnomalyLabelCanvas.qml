import QtQuick

import dltool.feature

ClassificationLabelCanvas {
    id: labelCanvas

    property FeatureManager featureManager
    property alias showBoundingBoxes: annotationLayer.showBoundingBoxes
    property alias actions: annotationLayer.actions

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
        imageLevelLabelMode: true
        afterLabelAdded: function(imageId, labelClassId, data) {
            if (labelCanvas.dataManager) {
                labelCanvas.dataManager.setImageLabelClass(imageId, labelClassId)
            }
        }
    }
}
