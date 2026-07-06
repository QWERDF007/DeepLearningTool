import QtQuick

import dltool.feature

LabelCanvasBase {
    id: labelCanvas

    property FeatureManager featureManager
    property alias rectangleToolAvailable: annotationLayer.rectangleToolAvailable
    property alias polygonRegionMode: annotationLayer.polygonRegionMode
    property alias showBoundingBoxes: annotationLayer.showBoundingBoxes
    property alias actions: annotationLayer.actions

    LabelCanvasAnnotationLayer {
        id: annotationLayer
        canvas: labelCanvas
        rectangleToolAvailable: true
        polygonRegionMode: false
    }
}
