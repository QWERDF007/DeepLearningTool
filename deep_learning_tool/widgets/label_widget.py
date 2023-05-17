import typing
from typing import Any, Optional

from qtpy.QtCore import QObject, Signal, Slot
from qtpy.QtCore import Qt, QLineF, QRectF, QPointF, QRect

from qtpy.QtWidgets import QWidget, QMainWindow, QDockWidget, QSizePolicy, QListWidget
from qtpy.QtWidgets import QGridLayout
from qtpy.QtWidgets import (QGraphicsScene, QGraphicsView, QGraphicsLineItem, QGraphicsRectItem, 
                            QGraphicsItem, QGraphicsPixmapItem, QStyleOptionGraphicsItem)
from qtpy.QtWidgets import QGraphicsSceneHoverEvent
from qtpy.QtGui import QPixmap, QPen, QPainter, QPainterPath, QPolygonF, QTransform


from qtpy.QtCore import QEvent
from qtpy.QtGui import QMouseEvent, QResizeEvent, QWheelEvent, QKeyEvent

from .widget import ProjectInfo, HLine

from deep_learning_tool.data import Project
from deep_learning_tool import LOGGER


class RectItem(QGraphicsRectItem):
    # rectChanged = Signal(QRectF)

    def __init__(self, rect: QRectF, parent: typing.Optional[QGraphicsItem] = ...) -> None:
        LOGGER.debug("init")
        super().__init__(rect, parent)
        # self.acceptHoverEvents(True)
        self.setFlag(QGraphicsItem.GraphicsItemFlag.ItemIsMovable, True)
        self.setFlag(QGraphicsItem.GraphicsItemFlag.ItemIsSelectable, True)
        self.setFlag(QGraphicsItem.GraphicsItemFlag.ItemSendsGeometryChanges, True)
        self.setAcceptHoverEvents(True)

    def __del__(self):
        LOGGER.debug("del")
        


    def itemChange(self, change: QGraphicsItem.GraphicsItemChange, value: QPointF) -> Any:
        # 限制移动不超出 parentItem 的 Pixmap
        parent = self.parentItem()
        LOGGER.debug(parent)
        if parent is not None and isinstance(parent, QGraphicsPixmapItem) and change == QGraphicsItem.GraphicsItemChange.ItemPositionChange:
            LOGGER.debug(value)
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
    
    def hoverEnterEvent(self, event: QGraphicsSceneHoverEvent) -> None:
        if self.isSelected():
            self.setCursor(Qt.CursorShape.SizeAllCursor)
        return super().hoverEnterEvent(event)
    
    def hoverMoveEvent(self, event: QGraphicsSceneHoverEvent) -> None:
        if self.isSelected():
            self.setCursor(Qt.CursorShape.SizeAllCursor)
        return super().hoverMoveEvent(event)
    
    def hoverLeaveEvent(self, event: QGraphicsSceneHoverEvent) -> None:
        self.setCursor(Qt.CursorShape.ArrowCursor)
        return super().hoverLeaveEvent(event)

class ImageItem(QGraphicsPixmapItem):
    def __init__(self, pixmap: QPixmap, parent: typing.Optional[QGraphicsItem] = None) -> None: 
        super().__init__(pixmap, parent)


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

class ImageView(QGraphicsView):
    def __init__(self, scene, parent=None) -> None:
        LOGGER.debug("ImageView")
        super().__init__(scene, parent)

        self.label_rects = []

        self.p0 = None
        self.p1 = None
        self.drawing_rect = None
        
        self.initCrossLine()

        self.current_viewport_rect = self.getCurrentViewRectOnScene()

        self.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.setRenderHint(QPainter.RenderHint.Antialiasing)
        self.setResizeAnchor(QGraphicsView.ViewportAnchor.AnchorUnderMouse)
        self.setTransformationAnchor(QGraphicsView.ViewportAnchor.AnchorUnderMouse)

        self.setViewportUpdateMode(QGraphicsView.ViewportUpdateMode.FullViewportUpdate)

        self.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)

        self.label_image = None

        self.middle_button_press = False
        self.last_pos = self.pos()

        self.current_select_item = None


    def setLabelImage(self, label_image : QGraphicsPixmapItem):
        LOGGER.debug("setLabelImage")
        self.label_image = label_image
        # 设置 sceneRect 缩放才正常
        self.scene().setSceneRect(self.label_image.boundingRect())
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
            pos = self.mapToScene(pos)
            self.current_viewport_rect = self.getCurrentViewRectOnScene()
            
            new_line1 = QLineF(QPointF(self.current_viewport_rect.x(), pos.y()), 
                            QPointF(self.current_viewport_rect.x() + self.current_viewport_rect.width(), pos.y()))
            
            new_line2 = QLineF(QPointF(pos.x(), self.current_viewport_rect.y()), 
                            QPointF(pos.x(), self.current_viewport_rect.y() + self.current_viewport_rect.height()))
            
            self.cross_line.setCrossLine(new_line1, new_line2)

    
    def drawingRect(self, pos):
        # 映射回parent，保证移动图像后不变
        self.p1 = self.label_image.mapFromScene(pos)
        top_left = QPointF(min(self.p0.x(), self.p1.x()), min(self.p0.y(), self.p1.y()))
        bottom_right = QPointF(max(self.p0.x(), self.p1.x()), max(self.p0.y(), self.p1.y()))
        rect = QRectF(top_left, bottom_right)
        return rect
    
    def getRectInImage(self, rect : QRectF) -> QRectF:
        LOGGER.debug(self.label_image.boundingRect())
        # polygon = self.mapToScene(self.label_image.boundingRect())
        # prect = QRectF(polygon[0], polygon[2])
        prect = self.label_image.boundingRect()
        LOGGER.debug(f"{prect} {rect}")
        return rect.intersected(prect)


    def finishedRect(self, rect) -> bool:
        if rect.width() < 1 or rect.height() < 1:
            self.scene().removeItem(self.drawing_rect)
            del self.drawing_rect
            self.drawing_rect = None
            return False
        else:
            rect = self.getRectInImage(rect)
            self.drawing_rect.setRect(rect)
            self.drawing_rect = None
            return True
        

    def mousePressEvent(self, event: QMouseEvent) -> None:
        pos = event.pos()
        if event.button() == Qt.MouseButton.LeftButton:
            if event.modifiers() == Qt.KeyboardModifier.ControlModifier:
                return super().mousePressEvent(event)
            selected_items = self.scene().selectedItems()
            selected_items_count = len(selected_items)
            if selected_items_count > 0:
                super().mousePressEvent(event)
            elif self.label_image is not None:
                self.p0 = self.label_image.mapFromScene(self.mapToScene(pos))
                self.drawing_rect = RectItem(QRectF(self.p0, self.p0), self.label_image)
                return
        elif event.button() == Qt.MouseButton.MiddleButton:
            self.setCursor(Qt.CursorShape.ClosedHandCursor)
            self.middle_button_press = True
            self.last_pos = self.mapToScene(event.pos())
            return
        super().mousePressEvent(event)


    def mouseMoveEvent(self, event: QMouseEvent) -> None:
        if self.label_image is not None:
            self.drawCrossLine(event.pos())

            if self.drawing_rect is not None:
                self.scene().clearSelection()
                self.drawing_rect.setRect(self.drawingRect(self.mapToScene(event.pos())))
                return

            if self.middle_button_press:
                pos = self.mapToScene(event.pos())
                offset = pos - self.last_pos
                self.label_image.moveBy(offset.x(), offset.y())
                self.last_pos = pos
                return 
        super().mouseMoveEvent(event)
    

    
    def mouseReleaseEvent(self, event: QMouseEvent) -> None:
        pos = event.pos()
        self.setCursor(Qt.CursorShape.ArrowCursor)
        self.middle_button_press = False
        if event.button() == Qt.MouseButton.LeftButton:
            if self.label_image is not None and self.drawing_rect is not None:
                LOGGER.debug("drawingRect")
                rect = self.drawingRect(self.mapToScene(pos))
                LOGGER.debug("finishedRect")
                if not self.finishedRect(rect):
                    items = self.items(pos)
                    LOGGER.debug(f"{items}")
                    for i, item in enumerate(items):
                        if isinstance(item, RectItem):
                            self.current_select_item = item
                            item.setSelected(True)
                            break
                    super().mouseReleaseEvent(event)
        return super().mouseReleaseEvent(event)
    
    
    def wheelEvent(self, event: QWheelEvent) -> None:
        new_scale = 1.0 + event.angleDelta().y()  * 0.00125
        self.scale(new_scale, new_scale)
        self.update()
        self.drawCrossLine(event.pos())
        return super().wheelEvent(event)
    
    def leaveEvent(self, event: QEvent) -> None:
        if self.cross_line:
            self.cross_line.setCrossLine(QLineF(), QLineF())
        return super().leaveEvent(event)
    

class LabelWindow(QMainWindow):
    def __init__(self, parent: QWidget | None = None, flags: Qt.WindowFlags | Qt.WindowType = Qt.WindowFlags()) -> None:
        super().__init__(parent, flags)
        self.init()

    def init(self):
        LOGGER.debug("init")
        self.createCentralWidget()
        self.createDockWidgets()

        self.image_view.enable_cross_line(True)
        
        self.label_image = None
        

    def initLabelImage(self):
        LOGGER.debug("initLabelImage")
        if self.label_image is None and len(self.project.images_path) >= 1:
            path, index = next(iter(self.project.images_path.items()))
            self.label_image = self.scene.addPixmap(QPixmap(path))
            self.image_view.setLabelImage(self.label_image)


    def setLabelImage(self, image_path : str):
        LOGGER.debug("setLabelImage")
        if self.label_image is not None:
            self.label_image.setPixmap(QPixmap(image_path))
        else:
            self.label_image = self.scene.addPixmap(QPixmap(image_path))
        self.image_view.setLabelImage(self.label_image)
        

    def createCentralWidget(self):
        LOGGER.debug("createCentralWidget")
        label_widget = QWidget(self)
        label_widget_layout = QGridLayout()
        label_widget_layout.setContentsMargins(0,0,0,0)
        label_widget.setLayout(label_widget_layout)
        self.scene = QGraphicsScene(self)
        self.image_view = ImageView(self.scene, label_widget)
        self.image_view.show()
        label_widget_layout.addWidget(self.image_view)
        self.setCentralWidget(label_widget)


    def setProject(self, project : Project):
        self.project = project
        self.project_dock.setWidget(ProjectInfo(project.name, project.type))

    
    def createDockWidgets(self):
        LOGGER.debug("createDockWidgets")
        empty_widget = QWidget(self)
        # line = HLine(self)

        self.project_dock = QDockWidget(self.tr("项目"), self)
        self.project_dock.setTitleBarWidget(empty_widget)
        self.project_dock.setFeatures(QDockWidget.DockWidgetFeature.NoDockWidgetFeatures)
        self.project_dock.setContentsMargins(0,0,0,0)
        self.project_dock.setFixedHeight(100)
        self.project_dock.setMinimumWidth(100)
        self.project_dock.setMaximumWidth(500)
        self.project_dock.setSizePolicy(QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Minimum)
        self.addDockWidget(Qt.DockWidgetArea.LeftDockWidgetArea, self.project_dock)


        self.file_dock = QDockWidget(self.tr("图像"), self)
        # self.file_dock.setTitleBarWidget(line)
        self.file_dock.setFeatures(QDockWidget.DockWidgetFeature.NoDockWidgetFeatures)
        self.file_dock.setWidget(QWidget(self))
        self.file_dock.setFixedHeight(120)
        self.file_dock.setSizePolicy(QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Minimum)
        self.addDockWidget(Qt.DockWidgetArea.LeftDockWidgetArea, self.file_dock)


        self.label_list_dock = QDockWidget(self.tr("标签列表"), self)
        self.label_list_dock.setFeatures(QDockWidget.DockWidgetFeature.NoDockWidgetFeatures)
        self.label_list_dock.setWidget(QListWidget(self))
        self.label_list_dock.setMinimumHeight(100)
        self.label_list_dock.setSizePolicy(QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Minimum)
        self.addDockWidget(Qt.DockWidgetArea.LeftDockWidgetArea, self.label_list_dock)


        self.split_mapping_dock = QDockWidget(self.tr("拆分映射"), self)
        self.split_mapping_dock.setFeatures(QDockWidget.DockWidgetFeature.NoDockWidgetFeatures)
        self.split_mapping_dock.setWidget(QWidget(self))
        self.split_mapping_dock.setMinimumHeight(100)
        self.split_mapping_dock.setSizePolicy(QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Minimum)
        self.addDockWidget(Qt.DockWidgetArea.LeftDockWidgetArea, self.split_mapping_dock)


        self.navigator_dock = QDockWidget(self.tr("导航器"), self)
        self.navigator_dock.setFeatures(QDockWidget.DockWidgetFeature.NoDockWidgetFeatures)
        self.navigator_dock.setWidget(QWidget(self))
        self.navigator_dock.setFixedHeight(100)
        self.navigator_dock.setMinimumWidth(100)
        self.navigator_dock.setMaximumWidth(500)
        self.navigator_dock.setSizePolicy(QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Minimum)
        self.addDockWidget(Qt.DockWidgetArea.RightDockWidgetArea, self.navigator_dock)


        self.image_enhance_dock = QDockWidget(self.tr("图像增强"), self)
        self.image_enhance_dock.setFeatures(QDockWidget.DockWidgetFeature.NoDockWidgetFeatures)
        self.image_enhance_dock.setWidget(QWidget(self))
        self.image_enhance_dock.setFixedHeight(100)
        self.image_enhance_dock.setSizePolicy(QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Minimum)
        self.addDockWidget(Qt.DockWidgetArea.RightDockWidgetArea, self.image_enhance_dock)


        self.shape_dock = QDockWidget(self.tr("标签实例"), self)
        self.shape_dock.setFeatures(QDockWidget.DockWidgetFeature.NoDockWidgetFeatures)
        self.shape_dock.setWidget(QListWidget(self))
        self.shape_dock.setMinimumHeight(100)
        self.shape_dock.setSizePolicy(QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Minimum)
        self.addDockWidget(Qt.DockWidgetArea.RightDockWidgetArea, self.shape_dock)
        

        self.shape_edit_dock = QDockWidget(self.tr("标签实例编辑"), self)
        self.shape_edit_dock.setFeatures(QDockWidget.DockWidgetFeature.NoDockWidgetFeatures)
        self.shape_edit_dock.setWidget(QWidget(self))
        self.shape_edit_dock.setMinimumHeight(100)
        self.shape_edit_dock.setSizePolicy(QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Minimum)
        self.addDockWidget(Qt.DockWidgetArea.RightDockWidgetArea, self.shape_edit_dock)

        
