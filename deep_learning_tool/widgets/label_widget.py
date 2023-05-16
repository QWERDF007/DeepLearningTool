import typing
from typing import Any

from qtpy.QtCore import QObject, Signal, Slot
from qtpy.QtCore import Qt, QLineF, QRectF, QPointF

from qtpy.QtWidgets import QWidget, QMainWindow, QDockWidget, QSizePolicy, QListWidget
from qtpy.QtWidgets import QGridLayout
from qtpy.QtWidgets import QGraphicsScene, QGraphicsView, QGraphicsLineItem, QGraphicsRectItem, QGraphicsItem, QGraphicsPixmapItem
from qtpy.QtGui import QPixmap, QPen


from qtpy.QtCore import QEvent
from qtpy.QtGui import QMouseEvent, QResizeEvent, QWheelEvent, QKeyEvent

from .widget import ProjectInfo, HLine

from deep_learning_tool.data import Project
from deep_learning_tool import LOGGER


class RectItem(QObject, QGraphicsRectItem):
    rectChanged = Signal(QRectF)

    def __init__(self, parent: typing.Optional[QGraphicsItem] = None) -> None:
        super().__init__(parent)

    def __init__(self, rect: QRectF, parent: typing.Optional[QGraphicsItem] = None) -> None:
        super().__init__(rect, parent)

    def __init__(self, x: float, y: float, w: float, h: float, parent: typing.Optional[QGraphicsItem] = None) -> None:
        super().__init__(x, y, w, h, parent)

    def itemChange(self, change: QGraphicsItem.GraphicsItemChange, value: Any) -> Any:
        # 限制移动不超出 parentItem 的 Pixmap
        parent = self.parentItem()
        
        if parent is not None and change == QGraphicsItem.GraphicsItemChange.ItemPositionChange:
            new_pos = value.toPointF()

        return super().itemChange(change, value)


class ImageItem(QGraphicsPixmapItem):
    def __init__(self, parent: typing.Optional[QGraphicsItem] = None) -> None: 
        super().__init__(parent)

    def __init__(self, pixmap: QPixmap, parent: typing.Optional[QGraphicsItem] = None) -> None: 
        super().__init__(pixmap, parent)


class ImageView(QGraphicsView):
    def __init__(self, scene, parent=None) -> None:
        LOGGER.debug("ImageView")
        super().__init__(scene, parent)

        self.label_rects = []

        self.drawing_rect = None
        
        self.init_cross_line()

        self.current_viewport_rect = self.getCurrentViewRect()

        self.setResizeAnchor(QGraphicsView.ViewportAnchor.AnchorUnderMouse)

        self.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)

        self.label_image = None

        self.middle_button_press = False
        self.last_pos = self.pos()


    def setLabelImage(self, label_image : QGraphicsPixmapItem):
        LOGGER.debug("setLabelImage")
        self.label_image = label_image
        self.label_image.setPos(0,0)
        self.fitInView(self.label_image, Qt.AspectRatioMode.KeepAspectRatio)


    def init_cross_line(self):
        LOGGER.debug("init_cross_line")
        self.cross_line_enable = True
        pen = QPen(Qt.PenStyle.DashLine)
        line1 = QGraphicsLineItem(QLineF())
        # line1.setFlags(QGraphicsItem.GraphicsItemFlag.ItemIgnoresTransformations)
        line1.setPen(pen)
        line2 = QGraphicsLineItem(QLineF())
        line2.setPen(pen)
        line1.setZValue(1000)
        line2.setZValue(1000)
        self.cross_line = []
        self.cross_line.append(line1)
        self.cross_line.append(line2)
        self.scene().addItem(self.cross_line[0])
        self.scene().addItem(self.cross_line[1])

    def enable_cross_line(self, enable=True):
        if enable:
            self.cross_line_enable = True
            self.setMouseTracking(True) # 追踪鼠标，否则要按下鼠标才会触发鼠标事件
        else:
            self.setMouseTracking(False)
            self.cross_line_enable = False
            self.cross_line[0].setLine(QLineF())
            self.cross_line[1].setLine(QLineF())

    
    def resizeEvent(self, event: QResizeEvent) -> None:
        LOGGER.debug("resizeEvent")
        self.current_viewport_rect = self.getCurrentViewRect()
        return super().resizeEvent(event)
    
    def getCurrentViewRect(self):
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

    def mousePressEvent(self, event: QMouseEvent) -> None:
        if event.button() == Qt.MouseButton.MiddleButton:
            self.setCursor(Qt.CursorShape.ClosedHandCursor)
            self.middle_button_press = True
            self.last_pos = event.pos()
        return super().mousePressEvent(event)

    def mouseMoveEvent(self, event: QMouseEvent) -> None:
        # LOGGER.debug("mouseMoveEvent")
        
            # self.setCursor(Qt.CursorShape.ClosedHandCursor)
            
        if self.label_image is not None:
            if self.middle_button_press:
                LOGGER.debug(f"move {self.last_pos}")
                offset = event.pos() - self.last_pos
                self.last_pos = event.pos()
                self.horizontalScrollBar().setValue(self.horizontalScrollBar().value() - offset.x())
                self.verticalScrollBar().setValue(self.verticalScrollBar().value() - offset.y())
            
            if self.cross_line_enable:
                pos = self.mapToScene(event.pos())
                self.current_viewport_rect = self.getCurrentViewRect()
                
                new_line1 = QLineF(QPointF(self.current_viewport_rect.x() + 5, pos.y()), 
                                QPointF(self.current_viewport_rect.x() + self.current_viewport_rect.width() - 5, pos.y()))
                
                new_line2 = QLineF(QPointF(pos.x(), self.current_viewport_rect.y() + 5), 
                                QPointF(pos.x(), self.current_viewport_rect.y() + self.current_viewport_rect.height() - 5))
                
                self.cross_line[0].setLine(new_line1)
                self.cross_line[1].setLine(new_line2)
        return super().mouseMoveEvent(event)
    
    def mouseReleaseEvent(self, event: QMouseEvent) -> None:
        self.setCursor(Qt.CursorShape.ArrowCursor)
        self.middle_button_press = False
        return super().mouseReleaseEvent(event)
    
    def wheelEvent(self, event: QWheelEvent) -> None:
        return super().wheelEvent(event)
    
    def leaveEvent(self, event: QEvent) -> None:
        if self.cross_line:
            self.cross_line[0].setLine(QLineF())
            self.cross_line[1].setLine(QLineF())
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

        
