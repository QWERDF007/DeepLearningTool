import QtQuick
import QtQml

QtObject {
    id: geometry

    property var imageItem: null

    function clonePoints(points) {
        let result = []
        if (!points) {
            return result
        }
        for (let point of points) {
            result.push({x: point.x, y: point.y})
        }
        return result
    }

    function cloneSmartPoints(points) {
        let result = []
        if (!points) {
            return result
        }
        for (let point of points) {
            result.push({x: point.x, y: point.y, label: point.label})
        }
        return result
    }

    function clampPointToImage(point) {
        if (!imageItem || imageItem.status !== Image.Ready) {
            return point
        }
        return Qt.point(Math.max(0, Math.min(imageItem.sourceSize.width, point.x)),
                        Math.max(0, Math.min(imageItem.sourceSize.height, point.y)))
    }

    function distance(pt1, pt2) {
        let dx = pt1.x - pt2.x
        let dy = pt1.y - pt2.y
        return Math.sqrt(dx * dx + dy * dy)
    }

    function boundsFromPoints(points) {
        if (!points || points.length === 0) {
            return {x: 0, y: 0, width: 0, height: 0}
        }

        let xMin = points[0].x
        let yMin = points[0].y
        let xMax = points[0].x
        let yMax = points[0].y
        for (let point of points) {
            xMin = Math.min(xMin, point.x)
            yMin = Math.min(yMin, point.y)
            xMax = Math.max(xMax, point.x)
            yMax = Math.max(yMax, point.y)
        }
        return {x: xMin, y: yMin, width: xMax - xMin, height: yMax - yMin}
    }

    function rectFromPoints(pt1, pt2) {
        let x = Math.min(pt1.x, pt2.x)
        let y = Math.min(pt1.y, pt2.y)
        let width = Math.abs(pt2.x - pt1.x)
        let height = Math.abs(pt2.y - pt1.y)
        return {x: x, y: y, width: width, height: height}
    }

    function toScreen(point) {
        if (!imageItem) {
            return Qt.point(point.x, point.y)
        }
        return Qt.point(imageItem.x + point.x * imageItem.scale,
                        imageItem.y + point.y * imageItem.scale)
    }

    function isPointInsideImage(point) {
        if (!imageItem || imageItem.status !== Image.Ready) {
            return false
        }
        return point.x >= 0 && point.y >= 0
               && point.x <= imageItem.sourceSize.width
               && point.y <= imageItem.sourceSize.height
    }

    function getPosOnImage(event) {
        return getPosOnImagePoint(event.x, event.y)
    }

    function getPosOnImagePoint(x, y) {
        if (!imageItem) {
            return Qt.point(x, y)
        }
        return Qt.point((x - imageItem.x) / imageItem.scale,
                        (y - imageItem.y) / imageItem.scale)
    }
}
