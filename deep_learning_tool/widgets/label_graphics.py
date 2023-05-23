import typing
from typing import Any, Optional
from enum import Enum
import PySide6.QtWidgets


from qtpy.QtCore import Signal, Slot
from qtpy.QtCore import Qt, QLineF, QRectF, QPointF
from qtpy.QtCore import QEvent

from qtpy.QtWidgets import QWidget
from qtpy.QtWidgets import QGraphicsView, QGraphicsItem, QGraphicsPixmapItem, QGraphicsRectItem, QStyleOptionGraphicsItem
from qtpy.QtWidgets import QGraphicsSceneHoverEvent, QGraphicsSceneMouseEvent

from qtpy.QtGui import QPainter, QPen, QPainterPath, QPixmap, QCursor, QPainterPathStroker
from qtpy.QtGui import QMouseEvent, QResizeEvent, QWheelEvent, QKeyEvent

from deep_learning_tool import LOGGER
from deep_learning_tool.utils import newPixmap, distance, distancetoline


def qt_graphicsItem_shapeFromPath(path : QPainterPath, pen : QPen) -> QPainterPath:
    pen_width_zero = 0.00000001
    if path.isEmpty() or pen.style() == Qt.PenStyle.NoPen:
        return path
    
    ps = QPainterPathStroker()
    ps.setCapStyle(pen.capStyle())
    if pen.widthF() <= 0.0:
        ps.setWidth(pen_width_zero)
    else:
        ps.setWidth(pen.widthF())
    
    ps.setJoinStyle(pen.joinStyle())
    ps.setMiterLimit(pen.miterLimit())
    p = ps.createStroke(path)
    p.addPath(path)
    return p


class VertexEdge(Enum):
    # vertex order
    TOP_LEFT = 0
    TOP_RIGHT = 1
    BOTTOM_RIGHT = 2
    BOTTOM_LEFT = 3
    NO_VERTEX = -1

    # edge order
    LEFT = 0
    TOP = 1
    RIGHT = 2
    BOTTOM = 3
    NO_EDGE = -1

class RectItem(QGraphicsRectItem):
    # rectChanged = Signal(QRectF)

    

    def __init__(self, rect: QRectF, parent: typing.Optional[QGraphicsItem] = ...) -> None:
        super().__init__(rect, parent)
        self.selected_vertex = VertexEdge.NO_VERTEX.value
        self.selected_edge = VertexEdge.NO_EDGE.value
        LOGGER.debug(f"init item:\n{self}")

    def __del__(self):
        LOGGER.debug(f"del item:\n{self}")
        

    def itemChange(self, change: QGraphicsItem.GraphicsItemChange, value: typing.Union[Any, QPointF]) -> Any:
        # 限制移动不超出 parentItem 的 Pixmap
        parent = self.parentItem()
        if parent is not None and isinstance(parent, QGraphicsPixmapItem) and change == QGraphicsItem.GraphicsItemChange.ItemPositionChange:
            new_pos = value
            rect = self.rect()
            pos_on_pixmap = new_pos + rect.topLeft()
            prect = QRectF(parent.pixmap().rect())
            if not prect.contains(pos_on_pixmap) or not prect.contains(pos_on_pixmap + QPointF(rect.width(), rect.height())):
                x = min(prect.right() - rect.left() - rect.width(), max(new_pos.x(), prect.left() - rect.left()))
                y = min(prect.bottom() - rect.top() - rect.height(), max(new_pos.y(), prect.top() - rect.top()))
                new_pos.setX(x)
                new_pos.setY(y)
                return new_pos
        return super().itemChange(change, value)

    def Vertices(self):
        rect = self.rect()
        return [rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()]
    
    def nearestVertex(self, pos : QPointF, epsilon : float):
        min_distance = float("inf")
        min_i = VertexEdge.NO_VERTEX.value
        vertices = self.Vertices()
        for i, vertex in enumerate(vertices):
            dist = distance(vertex - pos)
            if dist <= epsilon and dist < min_distance:
                min_distance = dist
                min_i = i
        return min_i
    
    def nearestEdge(self, pos : QPointF, epsilon : float):
        min_distance = float("inf")
        post_i = VertexEdge.NO_EDGE.value
        vertices = self.Vertices()
        for i in range(len(vertices)):
            line = [vertices[i - 1], vertices[i]]
            dist = distancetoline(pos, line)
            if dist <= epsilon and dist < min_distance:
                min_distance = dist
                post_i = i
        return post_i
    
    def setRectFromParent(self, rect : QRectF):
        if isinstance(self.parentItem(), QGraphicsPixmapItem):
            prect = self.parentItem().pixmap().rect()
            self.setRect(self.mapRectFromParent(rect.intersected(prect)))
        else:
            self.setRect(QRectF())
    
    def adjustRect(self, pos):
        if self.selected_vertex != VertexEdge.NO_VERTEX.value:
            rect = self.adjustByVertex(pos)
            self.setRectFromParent(rect)
        elif self.selected_edge != VertexEdge.NO_EDGE.value:
            rect = self.adjustByEdge(pos)
            self.setRectFromParent(rect)
        else:
            # FIXME:
            LOGGER.error("FIXME")
            self.setRect(QRectF())

    def adjustByVertex(self, pos : QPointF):
        LOGGER.debug(f'\n{self}\n{self.parentItem()}')
        if isinstance(self.parentItem(), QGraphicsPixmapItem):
            prect = QRectF(self.parentItem().pixmap().rect())
            min_edge = 10
            x1, x2 = 0, 0
            y1, y2 = 0, 0
            pos = self.mapToParent(pos)
            top_left = self.mapToParent(self.rect().topLeft())
            bottom_right = self.mapToParent(self.rect().bottomRight())

            if self.selected_vertex == VertexEdge.TOP_LEFT.value:
                x1 = min(pos.x(), bottom_right.x() - min_edge)
                y1 = min(pos.y(), bottom_right.y() - min_edge)
                x2 = bottom_right.x()
                y2 = bottom_right.y()
                if not prect.contains(QPointF(x1, y1)):
                    x1 = 0 if x1 < 0 else x1
                    y1 = 0 if y1 < 0 else y1
            elif self.selected_vertex == VertexEdge.TOP_RIGHT.value:
                x1 = top_left.x()
                y1 = min(pos.y(), bottom_right.y() - min_edge)
                x2 = max(pos.x(), x1 + min_edge)
                y2 = bottom_right.y()
                if not prect.contains(QPointF(x2, y1)):
                    x2 = prect.width() if x2 > prect.width() else x2
                    y1 = 0 if y1 < 0 else y1
            elif self.selected_vertex == VertexEdge.BOTTOM_RIGHT.value:
                x1 = top_left.x()
                y1 = top_left.y()
                x2 = max(pos.x(), x1 + min_edge)
                y2 = max(pos.y(), y1 + min_edge)
                if not prect.contains(QPointF(x2, y2)):
                    x2 = prect.width() if x2 > prect.width() else x2
                    y2 = prect.height() if y2 > prect.height() else y2
            elif self.selected_vertex == VertexEdge.BOTTOM_LEFT.value:
                x1 = min(pos.x(), bottom_right.x() - min_edge)
                y1 = top_left.y()
                x2 = bottom_right.x()
                y2 = max(pos.y(), y1 + min_edge)
                if not prect.contains(QPointF(x1, y2)):
                    x1 = 0 if x1 < 0 else x1
                    y2 = prect.height() if y2 > prect.height() else y2
            else:
                # FIXME:
                LOGGER.error("FIXME")
                x1, x2, y1, y2 = 0, 0, 0, 0
            return QRectF(QPointF(x1, y1), QPointF(x2, y2)).intersected(prect)
        else:
            return QRectF()


    def adjustByEdge(self, pos : QPointF) -> QRectF:
        if isinstance(self.parentItem(), QGraphicsPixmapItem):
            prect = QRectF(self.parentItem().pixmap().rect())
            min_edge = 10
            x1, x2 = 0, 0
            y1, y2 = 0, 0
            pos = self.mapToParent(pos)
            top_left = self.mapToParent(self.rect().topLeft())
            bottom_right = self.mapToParent(self.rect().bottomRight())

            if self.selected_edge == VertexEdge.LEFT.value:
                x1 = min(pos.x(), bottom_right.x() - min_edge)
                y1 = top_left.y()
                if not prect.contains(QPointF(x1, y1)):
                    x1 = 0
                x2 = bottom_right.x()
                y2 = bottom_right.y()
            elif self.selected_edge == VertexEdge.TOP.value:
                x1 = top_left.x()
                y1 = min(pos.y(), bottom_right.y() - min_edge)
                if not prect.contains(QPointF(x1, y1)):
                    y1 = 0
                x2 = bottom_right.x()
                y2 = bottom_right.y()
            elif self.selected_edge == VertexEdge.RIGHT.value:
                x1 = top_left.x()
                y1 = top_left.y()
                right = max(pos.x(), top_left.x() + min_edge)
                if not prect.contains(QPointF(right, y1)):
                    right = prect.width()
                x2 = right
                y2 = bottom_right.y()
            elif self.selected_edge == VertexEdge.BOTTOM.value:
                x1 = top_left.x()
                y1 = top_left.y()
                bottom = max(pos.y(), top_left.y() + min_edge)
                if not prect.contains(QPointF(x1, bottom)):
                    bottom = prect.height()
                x2 = bottom_right.x()
                y2 = bottom
            else:
                # FIXME:
                LOGGER.error("FIXME")
                x1, x2, y1, y2 = 0, 0, 0, 0
            return QRectF(QPointF(x1, y1), QPointF(x2, y2)).intersected(prect)
        else:
            return QRectF()

    
    def mousePressEvent(self, event: QGraphicsSceneMouseEvent) -> None:
        LOGGER.debug(f"{event.modifiers()} {event.button()} {event.buttons()}")
        if event.modifiers() == Qt.KeyboardModifier.ControlModifier:
            LOGGER.debug("set movable")
            self.setCursor(Qt.CursorShape.ClosedHandCursor)
            # return super().mousePressEvent(event)
            return
        if event.button() == Qt.MouseButton.LeftButton:
            if self.isSelected():
                scale = self.scene().views()[0].transform().m11()    
                self.selected_vertex = self.nearestVertex(event.pos(), 10 / scale)
                if self.selected_vertex == VertexEdge.NO_VERTEX.value:
                    self.selected_edge = self.nearestEdge(event.pos(), 10 / scale)
            return super().mousePressEvent(event)
        elif event.button() == Qt.MouseButton.MiddleButton or event.buttons() & Qt.MouseButton.MiddleButton:
            self.setCursor(Qt.CursorShape.ClosedHandCursor)
            return 
        else:
            return super().mousePressEvent(event)
    
    def mouseMoveEvent(self, event: QGraphicsSceneMouseEvent) -> None:
        if event.modifiers() == Qt.KeyboardModifier.ControlModifier or event.buttons() & Qt.MouseButton.MiddleButton:
            LOGGER.debug(f"{event.modifiers()} {event.button()} {event.buttons()}")
            self.setCursor(Qt.CursorShape.ClosedHandCursor)
            return
        elif event.buttons() & Qt.MouseButton.LeftButton:
            if self.selected_vertex != VertexEdge.NO_VERTEX.value or self.selected_edge != VertexEdge.NO_EDGE.value:
                self.setFlag(QGraphicsItem.GraphicsItemFlag.ItemIsMovable, False)
                
                scene_selected_count = len(self.scene().selectedItems())
                if scene_selected_count == 1:
                    self.setFlag(QGraphicsItem.GraphicsItemFlag.ItemIsMovable, False)
                    super().mouseMoveEvent(event)
                    self.adjustRect(event.pos())
                    self.setFlag(QGraphicsItem.GraphicsItemFlag.ItemIsMovable, True)
                    return
                elif scene_selected_count > 1:
                    self.setFlag(QGraphicsItem.GraphicsItemFlag.ItemIsMovable, True)
                    return super().mouseMoveEvent(event)
                else:
                    LOGGER.error("FIXME")
                    self.setFlag(QGraphicsItem.GraphicsItemFlag.ItemIsMovable, True)
                    return super().mouseMoveEvent(event)
            else:
                self.setFlag(QGraphicsItem.GraphicsItemFlag.ItemIsMovable, True)
                return super().mouseMoveEvent(event)
        else:
            return super().mouseMoveEvent(event)

        
    def mouseReleaseEvent(self, event: QGraphicsSceneMouseEvent) -> None:
        LOGGER.debug(f"{event.modifiers()} {event.button()} {event.buttons()}")
        if self.isSelected():
            self.setCursorByPos(event.pos())
        return super().mouseReleaseEvent(event)
    
    
    def setCursorByPos(self, pos):
        scale = self.scene().views()[0].transform().m11()
        self.selected_vertex = self.nearestVertex(pos, 10 / scale)
        if self.selected_vertex != VertexEdge.NO_VERTEX.value:
            if self.selected_vertex == VertexEdge.TOP_LEFT.value or self.selected_vertex == VertexEdge.BOTTOM_RIGHT.value:
                self.setCursor(Qt.CursorShape.SizeFDiagCursor)
            elif self.selected_vertex == VertexEdge.TOP_RIGHT.value or self.selected_vertex == VertexEdge.BOTTOM_LEFT.value:
                self.setCursor(Qt.CursorShape.SizeBDiagCursor)
            else:
                # FIXME:
                LOGGER.error("FIXME")
        else:
            self.selected_edge = self.nearestEdge(pos, 10 / scale)
            if self.selected_edge != VertexEdge.NO_EDGE.value:
                if self.selected_edge == VertexEdge.LEFT.value or self.selected_edge == VertexEdge.RIGHT.value:
                    self.setCursor(Qt.CursorShape.SizeHorCursor)
                elif self.selected_edge == VertexEdge.TOP_RIGHT.value or self.selected_edge == VertexEdge.BOTTOM_LEFT.value:
                    self.setCursor(Qt.CursorShape.SizeVerCursor)
                else:
                    # FIXME:
                    LOGGER.error("FIXME")
            else:
                self.setCursor(Qt.CursorShape.SizeAllCursor)
        self.update()

    def keyPressEvent(self, event: QKeyEvent) -> None:
        LOGGER.debug(self.cursor().pos())
        return super().keyPressEvent(event)
    
    def keyReleaseEvent(self, event: QKeyEvent) -> None:
        LOGGER.debug(self.cursor().pos())
        return super().keyReleaseEvent(event)

    def hoverMoveEvent(self, event: QGraphicsSceneHoverEvent) -> None:
        super().hoverMoveEvent(event)
        if event.modifiers() == Qt.KeyboardModifier.ControlModifier:
            self.setCursor(Qt.CursorShape.OpenHandCursor)
            return

        if self.isSelected():
            if len(self.scene().selectedItems()) == 1:
                self.setCursorByPos(event.pos())
            else:
                self.setCursor(Qt.CursorShape.SizeAllCursor)
        else:
            self.setCursor(Qt.CursorShape.ArrowCursor)
        return
    
    def hoverLeaveEvent(self, event: QGraphicsSceneHoverEvent) -> None:
        super().hoverLeaveEvent(event)
        self.setCursor(Qt.CursorShape.ArrowCursor)
    
    def paint(self, painter: QPainter, option: QStyleOptionGraphicsItem, widget: QWidget | None = ...) -> None:
        pen = self.pen()
        pen.setWidth(1)
        pen.setCosmetic(True)
        if self.isSelected():
            pen.setStyle(Qt.PenStyle.DashLine)
        painter.setPen(pen)
        painter.drawRect(self.rect())
        # return super().paint(painter, option, widget)

    def boundingRect(self) -> QRectF:
        # 在rect基础上向外扩充 10 个单位
        scale = self.scene().views()[0].transform().m11()
        offset = 10 / scale
        return self.rect().adjusted(-offset, -offset, offset, offset)

    def shape(self) -> QPainterPath:
        # 重写以扩充鼠标事件响应范围和碰撞检测范围
        path = QPainterPath()
        path.addRect(self.boundingRect())
        return qt_graphicsItem_shapeFromPath(path, self.pen())


class CrossLineItem(QGraphicsItem):
    def __init__(self, line1 : QLineF, line2 : QLineF, parent=None):
        super().__init__(parent)
        self.left = line1.p1()
        self.right = line1.p2()
        self.top = line2.p1()
        self.bottom = line2.p2()
        self.line1 = line1
        self.line2 = line2
        self.pen = QPen()
        self.pen.setWidth(1)
        self.pen.setStyle(Qt.PenStyle.DashLine)

    def setPen(self, pen):
        self.pen = pen

    def setCrossLine(self, line1 : QLineF, line2 : QLineF) -> None:
        if line1 == self.line1 and line2 == self.line2:
            return
        elif line1 == self.line2 and line2 == self.line1:
            return
        self.left = line1.p1()
        self.right = line1.p2()
        self.top = line2.p1()
        self.bottom = line2.p2()
        self.prepareGeometryChange()
        self.line1 = line1
        self.line2 = line2
        self.update()


    def boundingRect(self) -> QRectF:
        return QRectF(self.left.x(), self.top.y(), abs(self.right.x() - self.left.x()), abs(self.bottom.y() - self.top.y()))
    
    
    def shape(self) -> QPainterPath:
        path = QPainterPath()
        path1 = QPainterPath()
        path1.moveTo(self.line1.p1())
        path1.lineTo(self.line1.p2())
        path2 = QPainterPath()
        path2.moveTo(self.line2.p1())
        path2.lineTo(self.line2.p2())
        path.addPath(path1)
        path.addPath(path2)
        return path
    

    def paint(self, painter: QPainter, option: QStyleOptionGraphicsItem, widget: QWidget | None = ...) -> None:
        # 设置画笔在任何变换下具有恒定宽度
        self.pen.setCosmetic(True)
        painter.setPen(self.pen)
        painter.drawLine(self.line1)
        painter.drawLine(self.line2)


class ImageItem(QGraphicsPixmapItem):
    def __init__(self, pixmap: QPixmap, parent: typing.Optional[QGraphicsItem] = None) -> None: 
        super().__init__(pixmap, parent)

class Mode(Enum):
    CREATE = 0
    EDIT = 1

class ImageView(QGraphicsView):
    def __init__(self, scene, parent=None) -> None:
        LOGGER.debug("ImageView")
        super().__init__(scene, parent)

        self.label_rects = []

        self.p0 = None
        self.drawing_rect = None

        self.hasMove = False
        self.mode = Mode.CREATE
        
        self.initCrossLine()

        self.current_viewport_rect = self.getCurrentViewRectOnScene()

        self.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.setRenderHint(QPainter.RenderHint.Antialiasing)
        self.setResizeAnchor(QGraphicsView.ViewportAnchor.AnchorUnderMouse)
        self.setTransformationAnchor(QGraphicsView.ViewportAnchor.AnchorUnderMouse)

        self.setViewportUpdateMode(QGraphicsView.ViewportUpdateMode.FullViewportUpdate)

        self.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOn)
        self.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOn)

        self.label_image = None

        self.middle_button_press = False
        self.press_pos = self.pos()

        self.current_select_rects = set()
        self.rects_has_selected_once = set()


    def setLabelImage(self, label_image : QGraphicsPixmapItem):
        LOGGER.debug("setLabelImage")
        self.label_image = label_image
        # 设置 sceneRect 缩放才正常
        self.scene().setSceneRect(self.label_image.boundingRect().adjusted(-2000,-2000,2000,2000))
        self.label_image.setPos(0,0)
        self.fitInView(self.label_image, Qt.AspectRatioMode.KeepAspectRatio)
        

    def initCrossLine(self):
        self.cross_line_enable = True
        self.cross_line = CrossLineItem(QLineF(), QLineF())
        self.cross_line.setZValue(1000)
        self.scene().addItem(self.cross_line)


    def enable_cross_line(self, enable=True):
        if enable:
            self.cross_line_enable = True
            self.setMouseTracking(True) # 追踪鼠标，否则要按下鼠标才会触发鼠标事件
        else:
            self.setMouseTracking(False)
            self.cross_line_enable = False
            self.cross_line.setCrossLine(QLineF(), QLineF())


    def resizeEvent(self, event: QResizeEvent) -> None:
        self.current_viewport_rect = self.getCurrentViewRectOnScene()
        return super().resizeEvent(event)
    
    
    def getCurrentViewRectOnScene(self):
        polygon = self.mapToScene(self.viewport().rect())
        return QRectF(polygon[0], polygon[2])
    

    @Slot(int)
    def next(self, i):
        pass


    @Slot(int)
    def prev(self, i):
        pass


    def clear(self):
        for item in self.scene().items():
            del item
        self.label_rects = []


    def drawCrossLine(self, pos):
        if self.cross_line_enable:
            self.current_viewport_rect = self.getCurrentViewRectOnScene()
            
            new_line1 = QLineF(QPointF(self.current_viewport_rect.x(), pos.y()), 
                            QPointF(self.current_viewport_rect.x() + self.current_viewport_rect.width(), pos.y()))
            
            new_line2 = QLineF(QPointF(pos.x(), self.current_viewport_rect.y()), 
                            QPointF(pos.x(), self.current_viewport_rect.y() + self.current_viewport_rect.height()))
            
            self.cross_line.setCrossLine(new_line1, new_line2)

    
    def getDrawingRect(self, p0 : QPointF, p1 : QPointF) -> QRectF:
        """获取绘制矩形, p0 和 p1 都是 label_image 上的坐标点
        """
        # 映射回parent，保证移动图像后不变
        p1 = self.label_image.mapFromScene(p1)
        top_left = QPointF(min(p0.x(), p1.x()), min(p0.y(), p1.y()))
        bottom_right = QPointF(max(p0.x(), p1.x()), max(p0.y(), p1.y()))
        rect = QRectF(top_left, bottom_right)
        return rect
    
    def isValidRect(self, rect : QRectF) -> bool:
        """判断是否为有效的矩形

        Args:
            rect (QRectF): 矩形

        Returns:
            bool:
        """
        if rect.width() < 1 or rect.height() < 1:
            return False
        else:
            return True
    
    def getIntersectedOfImage(self, rect : QRectF) -> QRectF:
        """获取矩形和图像矩形的交集, 限制矩形不超出图像范围

        Args:
            rect (QRectF): 矩形

        Returns:
            QRectF: 与图像矩形相交后的矩形
        """
        prect = self.label_image.boundingRect()
        return rect.intersected(prect)
    
    def deleteDrawingRect(self):
        """删除正在绘制的矩形
        """
        LOGGER.debug("deleteDrawingRect")
        if self.drawing_rect is not None:
            self.scene().removeItem(self.drawing_rect)
            del self.drawing_rect
            self.drawing_rect = None


    def finishDrawingRect(self, rect : QRectF) -> None:
        """完成正在绘制的矩形, 添加到scene中, 设置属性

        Args:
            rect (QRectF): 正在绘制的矩形
        """
        LOGGER.debug("finishedRect")
        if self.drawing_rect is not None:
            rect = self.getIntersectedOfImage(rect)
            self.drawing_rect.setRect(rect)
            self.drawing_rect.setFlag(QGraphicsItem.GraphicsItemFlag.ItemIsMovable, True)
            self.drawing_rect.setFlag(QGraphicsItem.GraphicsItemFlag.ItemIsSelectable, True)
            self.drawing_rect.setFlag(QGraphicsItem.GraphicsItemFlag.ItemSendsGeometryChanges, True)
            self.drawing_rect.setAcceptHoverEvents(True)
            self.drawing_rect = None
        else:
            # FIXME:
            LOGGER.error("FIXME")

    def rectItemSize(self, items_count : int) -> int:
        size = items_count - 2
        return size if size >= 0 else 0
    
    def rectItemSizeOnPos(self, pos : QPointF) -> int:
        items = self.items(pos)
        items_count = len(items)
        return self.rectItemSize(items_count)

    
    def changeSelectedRect(self, items : typing.List[QGraphicsItem], items_count : int):
        LOGGER.debug(self.current_select_rects)
        if len(self.current_select_rects) > 1:
            # FIXME:
            return LOGGER.error("FIXME")
        current_select_rect = self.current_select_rects.pop()
        current_select_rect.setZValue(0)
        current_select_rect.setSelected(False)
        if len(self.rects_has_selected_once) == self.rectItemSize(items_count):
            self.rects_has_selected_once.clear()
            for item in items:
                if isinstance(item, RectItem) and item != current_select_rect:
                    item.setZValue(1)
                    item.setSelected(True)
                    self.current_select_rects.add(item)
                    self.rects_has_selected_once.add(item)
                    break
        else:
            for item in items:
                if isinstance(item, RectItem) and item not in self.rects_has_selected_once:
                    item.setZValue(1)
                    item.setSelected(True)
                    self.current_select_rects.add(item)
                    self.rects_has_selected_once.add(item)
                    break
        LOGGER.debug(self.current_select_rects)
            
        
    def selectOneRect(self, items : typing.List[QGraphicsItem], items_count : int, scenePos : QPointF):
        """没按下control, 选中当前位置下的一个矩形, 如果在同一位置有多个rect, 且多次点击, 则在多个rect切换选中

        Args:
            items (typing.List[QGraphicsItem]): 当前位置下的QGraphicsItem
            items_count (int): items的数量
        """
        LOGGER.debug(f'items:\n{items}')
        LOGGER.debug(f'selected:\n{self.scene().selectedItems()}')
        LOGGER.debug(f'current selected:\n{self.current_select_rects}')
        if len(self.current_select_rects) <= 0:
            for item in items:
                if isinstance(item, RectItem):
                    self.selectRect(item)
                    self.rects_has_selected_once.add(item)
                    break
            if len(self.current_select_rects) <= 0:
                # FIXME:
                LOGGER.error("FIXME")
                return
        else:
            self.changeSelectedRect(items, items_count)
        LOGGER.debug(f"selected items:\n{self.scene().selectedItems()}")


    def initDrawingRect(self, scenePos : QPointF) -> None:
        self.p0 = self.label_image.mapFromScene(scenePos)
        self.drawing_rect = RectItem(QRectF(self.p0, self.p0), self.label_image)

    def hasRectItemOnPos(self, pos : QPointF) -> int:
        items = self.items(pos)
        return self.hasRectItem(len(items))

    def hasRectItem(self, items_count : int) -> int:
        """判断当前位置是否有多少个RectItem, 理论上items由一个pixmap、crossline和若干rect组成

        Args:
            items_count (int): 当前位置下的items的数量

        Returns:
            int: rect数量, <= 0 则没有矩形
        """
        return items_count - 2
    
    def setViewportCursor(self, cursor : typing.Union[QCursor, Qt.CursorShape]):
        self.viewport().setCursor(cursor)
    
    def resetViewPortCursor(self):
        self.viewport().setCursor(Qt.CursorShape.ArrowCursor)

    def disableCrossLine(self):
        if self.cross_line:
            self.cross_line.setCrossLine(QLineF(), QLineF())

    def selectRect(self, item : RectItem):
        """选中矩形

        Args:
            item (RectItem): _description_
        """
        if isinstance(item, RectItem):
            item.setZValue(1)
            item.setSelected(True)
            self.current_select_rects.add(item)

    def selectRects(self, items : typing.List[RectItem]):
        """control按下时选择当前位置的矩形, 当前位置全部被选中后再次点击会清空选择

        Args:
            items (typing.List[RectItem]): 矩形
        """
        tmp = set()
        for item in items:
            if isinstance(item, RectItem):
                tmp.add(item)
        
        if tmp.issubset(self.current_select_rects):
            self.unselectRects(tmp)
        else:
            for item in tmp:
                if item not in self.current_select_rects:
                    self.selectRect(item)
                    break


    def selectRectsOnPos(self, pos : QPointF):
        """control按下时选择当前位置的矩形, 当前位置全部被选中后再次点击会清空选择

        Args:
            pos (QPointF): 当前鼠标位置
        """
        items = self.items(pos)
        self.selectRects(items)

        
    def unselectRect(self, item : RectItem):
        """取消选中单个矩形

        Args:
            item (RectItem): 矩形
        """
        if isinstance(item, RectItem):
            item.setZValue(0)
            item.setSelected(False)
            self.current_select_rects.remove(item)
    
    def unselectRects(self, items: typing.List[RectItem]):
        """取消选中多个矩形

        Args:
            items (typing.List[RectItem]): 矩形列表
        """
        for item in items:
            self.unselectRect(item)

    def hasSelectedRect(self, items : typing.List[QGraphicsItem]) -> bool:
        """判断当前items中是否有选中的矩形

        Args:
            items (typing.List[QGraphicsItem]): 图形项列表

        Returns:
            bool: True 有, False 没有
        """
        if len(self.current_select_rects) == 0:
            return False
        for selected_rect in self.current_select_rects:
            if selected_rect in items:
                return True
        return False
    

    def hasSelectedRectOnPos(self, pos : QPointF) -> bool:
        """判断当前鼠标位置的items中是否有选中的矩形

        Args:
            pos (QPointF): 当前鼠标位置

        Returns:
            bool: True 有, False 没有
        """
        items = self.items(pos)
        return self.hasSelectedRect(items)
        
    
    def clearSelectedRects(self):
        """清除选中
        """
        LOGGER.debug(self.current_select_rects)
        # 集合不能在遍历时增加/删除元素
        self.unselectRects(list(self.current_select_rects))
        self.current_select_rects.clear()
        self.rects_has_selected_once.clear()
        LOGGER.debug(self.current_select_rects)


    def moveBy(self, pos : QPointF):
        """移动视图

        Args:
            pos (QPointF): 鼠标坐标
        """
        # 使用 view 的 pos 防抖, 使用 m11 确保在缩放后同样的移动效果
        offset = QPointF(pos - self.press_pos) / self.transform().m11()
        self.centerOn(self.centerAnchor - offset)
        

    def mouseDoubleClickEvent(self, event: QMouseEvent):
        if event.modifiers() != Qt.KeyboardModifier.ControlModifier:
            return super().mouseDoubleClickEvent(event)


    def mousePressEvent(self, event: QMouseEvent) -> None:
        LOGGER.debug(f"{self.mode} {event.modifiers()} {event.button()} {event.buttons()}")
        pos = event.pos()
        scenePos = self.mapToScene(pos)
        self.hasMove = False
        self.press_pos = pos
        self.centerAnchor = self.mapToScene(self.width() / 2, self.height() / 2)
        if event.button() == Qt.MouseButton.LeftButton:
            if event.modifiers() == Qt.KeyboardModifier.ControlModifier:
                if self.viewport().cursor().shape() != Qt.CursorShape.ClosedHandCursor:
                    self.viewport().setCursor(Qt.CursorShape.ClosedHandCursor)
                return
            
            LOGGER.debug(f"{self.mode}")
            if self.label_image is not None:
                if self.mode == Mode.CREATE:
                    self.initDrawingRect(scenePos)
                    return
                elif self.mode == Mode.EDIT:
                    items = self.items(pos)
                    items_count = len(items)
                    if self.hasRectItem(items_count) <= 0:
                        self.mode = Mode.CREATE
                        self.clearSelectedRects()
                        self.initDrawingRect(scenePos)
                        return
                    else:
                        selected_items = self.scene().selectedItems()
                        selected_items_count = len(selected_items)
                        items = self.items(pos)
                        items_count = len(items)
                        # FIXME:
                        LOGGER.debug(f"{items_count} items on {pos}")
                        LOGGER.debug(f"{selected_items_count} selected items on {pos}")
                        LOGGER.debug(f"{len(self.current_select_rects)} select rects")
                        if not self.hasSelectedRect(items):
                            self.mode = Mode.CREATE
                            self.clearSelectedRects()
                            self.initDrawingRect(scenePos)
                            return
                        else:
                            if selected_items_count <= 1:
                                LOGGER.debug(f"event pos {pos} {scenePos}")
                                return super().mousePressEvent(event)
                            else:
                                return super().mousePressEvent(event)
                else:
                    # FIXME:
                    LOGGER.error("FIXME")
                    return
            else:
                return super().mousePressEvent(event)
        elif event.button() == Qt.MouseButton.MiddleButton or event.buttons() & Qt.MouseButton.MiddleButton:
            self.setViewportCursor(Qt.CursorShape.ClosedHandCursor)
            return super().mousePressEvent(event)
        else:
            # TODO: 右键
            LOGGER.warning("right button not implement")
            return super().mousePressEvent(event)


    def mouseMoveEvent(self, event: QMouseEvent) -> None:
        # LOGGER.debug(f"{self.mode} {event.modifiers()} {event.button()} {event.buttons()}")
        pos = event.pos()
        scenePos = self.mapToScene(pos)
        self.hasMove = True
        if self.label_image is not None:
            self.drawCrossLine(scenePos)

            if event.buttons() & Qt.MouseButton.MiddleButton or (event.buttons() & Qt.MouseButton.LeftButton and event.modifiers() == Qt.KeyboardModifier.ControlModifier):
                super().mouseMoveEvent(event)
                self.setViewportCursor(Qt.CursorShape.ClosedHandCursor)
                self.moveBy(pos)
                return
            elif event.modifiers() == Qt.KeyboardModifier.ControlModifier:
                super().mouseMoveEvent(event)
                if event.buttons() == Qt.MouseButton.NoButton:
                    self.setViewportCursor(Qt.CursorShape.OpenHandCursor)
                return

            if self.mode == Mode.CREATE and self.drawing_rect is not None:
                self.drawing_rect.setRect(self.getDrawingRect(self.p0, scenePos))

        return super().mouseMoveEvent(event)
    
    
    def mouseReleaseEvent(self, event: QMouseEvent) -> None:
        LOGGER.debug(f"{self.mode} hasMove: {self.hasMove}  {event.modifiers()} {event.button()} {event.buttons()}")
        pos = event.pos()
        scenePos = self.mapToScene(pos)
        if event.button() == Qt.MouseButton.LeftButton:
            if event.modifiers() == Qt.KeyboardModifier.ControlModifier:
                super().mouseReleaseEvent(event)
                self.setViewportCursor(Qt.CursorShape.OpenHandCursor)
                if not self.hasMove:
                    self.selectRectsOnPos(pos)
                    self.mode = Mode.EDIT
                return 
            if self.label_image is not None:
                if self.mode == Mode.CREATE:
                    rect = self.getDrawingRect(self.p0, scenePos)
                    if self.isValidRect(rect):
                        self.finishDrawingRect(rect)
                        return
                    else:
                        self.deleteDrawingRect()
                        items = self.items(pos)
                        items_count = len(items)
                        LOGGER.debug(f"{items_count} items on {pos}")
                        if self.hasRectItem(items_count) > 0:
                            self.selectOneRect(items, items_count, scenePos)           
                            self.setSelectedRectCursorOnPos(pos, scenePos)
                            self.mode = Mode.EDIT
                            return
                        else:
                            return
                elif self.mode == Mode.EDIT:
                    items = self.items(pos)
                    items_count = len(items)
                    if self.hasMove:
                        return super().mouseReleaseEvent(event)
                    else:
                        super().mouseReleaseEvent(event)
                        rects_count = self.hasRectItem(items_count)
                        if rects_count < 1:
                            return
                        else:
                            if len(self.current_select_rects) > 1:
                                self.clearSelectedRects()
                            return self.selectOneRect(items, items_count, scenePos)
                else:
                    # FIXME:
                    LOGGER.error("FIXME")
                    return super().mouseReleaseEvent(event)
        elif event.button() == Qt.MouseButton.MiddleButton or event.buttons() & Qt.MouseButton.MiddleButton:
            super().mouseReleaseEvent(event)
            if event.modifiers() != Qt.KeyboardModifier.ControlModifier:
                self.resetViewPortCursor()
            else:
                self.setViewportCursor(Qt.CursorShape.OpenHandCursor)
            self.setSelectedRectCursorOnPos(pos, scenePos)
            return

        super().mouseReleaseEvent(event)
        self.resetViewPortCursor()
    
    
    def wheelEvent(self, event: QWheelEvent) -> None:
        new_scale = 1.0 + event.angleDelta().y()  * 0.00125
        self.scale(new_scale, new_scale)
        self.update()
        self.drawCrossLine(self.mapToScene(event.pos()))
        return
        # 不调用父类事件传递下去，避免缩放后触发滚动条
        # return super().wheelEvent(event)
    
    def leaveEvent(self, event: QEvent) -> None:
        self.disableCrossLine()
        self.resetViewPortCursor()
        return super().leaveEvent(event)
    
    def enterEvent(self, event : QEvent) -> None:
        self.resetViewPortCursor()
        return super().enterEvent(event)

    def keyPressEvent(self, event : QEvent) -> None:
        super().keyPressEvent(event)
        if event.key() == Qt.Key.Key_Delete:
            self.deleteItems()
            LOGGER.debug("delete items")
        elif event.key() == Qt.Key.Key_Control:
            self.setViewportCursor(Qt.CursorShape.OpenHandCursor)
        return
    
    def keyReleaseEvent(self, event : QEvent) -> None:
        super().keyReleaseEvent(event)
        self.resetViewPortCursor()
        pos = self.mapFromGlobal(QCursor.pos())
        scenePos = self.mapToScene(pos)
        self.setSelectedRectCursorOnPos(pos, scenePos)

    def selectedRectOnPos(self, pos):
        items = self.items(pos)
        selected_items = self.scene().selectedItems()
        for selected_item in selected_items:
            if isinstance(selected_item, RectItem) and selected_item in items:
                return selected_item
        return None

    def setSelectedRectCursorOnPos(self, pos, scenePos):
        rect = self.selectedRectOnPos(pos)
        if isinstance(rect, RectItem):
            rect.setCursorByPos(rect.mapFromScene(scenePos))

    
    def deleteItems(self):
        LOGGER.debug(f"items before delete\n{self.items()}")
        selected_items = self.scene().selectedItems()
        LOGGER.debug(f"selected_items before delete\n{selected_items}")
        for item in selected_items:
            self.current_select_rects.remove(item)
            self.scene().removeItem(item)
            del item
        self.mode = Mode.CREATE
        LOGGER.debug(f"items after delete\n{self.items()}")
