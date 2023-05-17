
import typing
import os

from qtpy.QtCore import QObject, Signal, Slot, QDir, QDirIterator
from qtpy.QtCore import Qt, QSize, QRectF, QPointF
from qtpy.QtWidgets import QMainWindow, QDockWidget, QWidget, QPushButton, QLabel, QSpacerItem
from qtpy.QtWidgets import QGraphicsScene, QGraphicsItem, QGraphicsView, QGraphicsTextItem, QStyleOptionGraphicsItem
from qtpy.QtWidgets import QMenu
from qtpy.QtWidgets import QGraphicsSceneMouseEvent

from qtpy.QtWidgets import QHBoxLayout, QVBoxLayout, QGridLayout

from qtpy.QtWidgets import QFileDialog
from qtpy.QtWidgets import QSizePolicy
from qtpy.QtGui import QPixmap, QPainter, QPainterPath, QPen, QColor, QFontMetrics
from qtpy.QtGui import QMouseEvent, QResizeEvent, QWheelEvent, QKeyEvent, QContextMenuEvent

from deep_learning_tool.utils import newIcon, newAction
from deep_learning_tool.data import Project
from deep_learning_tool.configs import image_suffixs_filter
from deep_learning_tool import LOGGER

from .widget import ProjectInfo, HLine


class GalleryImageInfo(QWidget):
    # open_image = QtCore.Signal(bool)
    def __init__(self, parent: QWidget | None = None, flags: Qt.WindowFlags | Qt.WindowType = Qt.WindowFlags()) -> None:
        super().__init__(parent, flags)

        self.label = QLabel("图像", self)
        ft = self.label.font()
        ft.setPointSize(14)
        self.label.setFont(ft)
        self.add_image_btn = QPushButton(newIcon("file"), "", self)
        self.add_image_btn.setIconSize(QSize(24,24))
        self.add_image_btn.setFixedSize(QSize(32,32))
        self.add_image_btn.setSizePolicy(QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Minimum)
        self.add_folder_btn = QPushButton(newIcon("folder"), "", self)
        self.add_folder_btn.setIconSize(QSize(24,24))
        self.add_folder_btn.setFixedSize(QSize(32,32))
        self.add_folder_btn.setSizePolicy(QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Minimum)

        
        hlayout = QHBoxLayout()
        hlayout.setContentsMargins(0,0,0,0)
        spacer_1 = QSpacerItem(20,20, QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Minimum)

        hlayout.addWidget(self.label)
        hlayout.addSpacerItem(spacer_1)
        hlayout.addWidget(self.add_image_btn)
        hlayout.addWidget(self.add_folder_btn)
        
        spacer_2 = QSpacerItem(20,20, QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Expanding)
        
        vlayout = QVBoxLayout()
        vlayout.setContentsMargins(0,0,0,0)
        vlayout.addLayout(hlayout)
        vlayout.addSpacerItem(spacer_2)

        self.setLayout(vlayout)

        self.setSizePolicy(QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Minimum)

        # TODO: 实现抽贴效果

        # box = QToolBox(self)
        # box.addItem(QWidget(self), "test")



class TextItem(QGraphicsTextItem):
    def __init__(self, text, width, parent = None):
        super().__init__(parent)
        self.fname = os.path.basename(text)
        self.fpath = text
        font = QFontMetrics(self.font())
        self.width = font.horizontalAdvance(text)
        self.height = font.height()
        self.setToolTip(self.fpath)
        elided_text = font.elidedText(self.fname, Qt.TextElideMode.ElideMiddle, width)
        self.setPlainText(elided_text)
        self.setFlags(QGraphicsItem.GraphicsItemFlag.ItemIgnoresParentOpacity)


class GalleryItem(QGraphicsItem):
    def __init__(self, image_path : str, parent: QGraphicsItem | None = None) -> None:
        super().__init__(parent)

        self.text_height = 24
        self.image_size = 512
        self.margin = 5 # LTBR
        self.width = self.image_size + self.margin * 2
        self.height = self.width + self.text_height
        self.image_path = image_path
        self.original_width = 0
        self.original_height = 0
        self.setFlags(QGraphicsItem.GraphicsItemFlag.ItemIsSelectable)
        self.setPixmap(image_path)

        self.text = TextItem(self.image_path, self.image_size, self)
        self.text.setPos(self.margin, self.margin + self.image_size)
        self.text.setZValue(self.zValue() + 1)
        self.text.setTextWidth(self.image_size)

    def setPixmap(self, image_path : str):
        pixmap = QPixmap(image_path)
        self.original_height = pixmap.height()
        self.original_width = pixmap.width()
        if pixmap.height() != self.image_size or pixmap.width() != self.image_size:
            self.pixmap = pixmap.scaled(QSize(self.image_size, self.image_size), Qt.KeepAspectRatio)
        else:
            self.pixmap = pixmap

    def mousePressEvent(self, event: QGraphicsSceneMouseEvent) -> None:
        if event.button() == Qt.LeftButton:
            super().mousePressEvent(event)
        elif event.button() == Qt.RightButton:
            self.setSelected(True)


    def boundingRect(self) -> QRectF:
        return QRectF(0, 0, self.width, self.height)
    

    def paint(self, painter: QPainter, option: QStyleOptionGraphicsItem, widget: QWidget | None = None) -> None:
        if self.isSelected():
            path = QPainterPath()
            path.addRoundedRect(0, 0, self.width, self.height, 10, 10)
            painter.fillPath(path, QColor(0, 200, 0, 100))

        x = (self.width - self.pixmap.width()) / 2
        y = (self.height - self.text_height - self.pixmap.height()) / 2
        painter.drawPixmap(QPointF(x, y), self.pixmap)
        
    def contains(self, point: QPointF) -> bool:
        return super().contains(point)
    
    def shape(self) -> QPainterPath:
        path = QPainterPath()
        path.addRect(self.boundingRect())
        return path


class GalleryView(QGraphicsView):
    galleryItemDoubleClicked = Signal(str)

    def __init__(self, parent=None) -> None:
        super().__init__(parent)

        self.setScene(QGraphicsScene(parent))

        self.spacing = 5
        self.current_x = 0
        self.current_y = 0
        self.setAlignment(Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignTop)
        self.setResizeAnchor(QGraphicsView.ViewportAnchor.AnchorViewCenter)
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
        self.width_limit = self.mapToScene(self.viewport().rect()).boundingRect().width()

        self.createContextMenu()


    def createContextMenu(self):
        self.context_menu = QMenu(self)
        self._open_action = newAction(self, self.tr("打开选中的图像"), slot=None, shortcut=None)
        self._delete_action = newAction(self, self.tr("删除选中的图像"), slot=None, shortcut=None)
        self.context_menu.addActions((self._open_action, self._delete_action,))

    def resizeEvent(self, event: QResizeEvent) -> None:
        self.width_limit = self.mapToScene(self.viewport().rect()).boundingRect().width()
        return super().resizeEvent(event)
    
    
    def mousePressEvent(self, event: QMouseEvent) -> None:
        item = self.itemAt(event.pos())
        selected_item = self.scene().selectedItems()
        if event.button() == Qt.MouseButton.RightButton and event.modifiers() != Qt.KeyboardModifier.ControlModifier:
            if item not in selected_item:
                self.scene().clearSelection()
        return super().mousePressEvent(event)
    
    
    def contextMenuEvent(self, event: QContextMenuEvent) -> None:
        selected_items_count = len(self.scene().selectedItems())
        if selected_items_count <= 0:
            super().contextMenuEvent(event)
        else:
            if selected_items_count >= 2:
                self._delete_action.setEnabled(False)
                self._open_action.setEnabled(False)
            else:
                self._delete_action.setEnabled(True)
                self._open_action.setEnabled(True)
            # mapToGlobal 会在鼠标右下方
            self.context_menu.exec(self.mapToGlobal(event.pos()))
    
    
    def mouseDoubleClickEvent(self, event: QMouseEvent) -> None:
        item = self.itemAt(event.pos())
        if event.button() == Qt.LeftButton and isinstance(item, GalleryItem):
            self.galleryItemDoubleClicked.emit(item.image_path)
        return super().mouseDoubleClickEvent(event)


    def mouseMoveEvent(self, event: QMouseEvent) -> None:
        return super().mouseMoveEvent(event)
    
    
    def wheelEvent(self, event: QWheelEvent) -> None:
        if event.modifiers() == Qt.KeyboardModifier.ControlModifier:
            new_scale = 1.0 + event.angleDelta().y()  * 0.00125
            LOGGER.debug(new_scale)
            self.scale(new_scale, new_scale)
            self.updateItemsPos()
        else:
            super().wheelEvent(event)

    
    def keyPressEvent(self, event: QKeyEvent) -> None:
        return super().keyPressEvent(event)
    
    
    def addImage(self, item : QGraphicsItem):
        self.scene().addItem(item)
        self.updateItemPos(item)

    def updateItemPos(self, item : QGraphicsItem):
        if isinstance(item, GalleryItem):
            item_size =  item.boundingRect().size()
            self.current_x = 0 if self.current_x == 0 else self.current_x + self.spacing
            if self.current_x > 0 and self.current_x + item_size.width() > self.width_limit:
                x = 0
                y = self.current_y + item_size.height() + self.spacing
                self.current_y = y
            else:
                x = self.current_x
                y = self.current_y

            item.setPos(x, y)
            self.current_x = x + item_size.width()

    def reset(self):
        self.current_x = 0
        self.current_y = 0
        self.width_limit = self.mapToScene(self.viewport().rect()).boundingRect().width()

    def updateItemsPos(self):
        self.reset()
        for item in self.scene().items(Qt.SortOrder.AscendingOrder):
            self.updateItemPos(item)
        

class GalleryWindow(QMainWindow):
    
    galleryItemDoubleClicked = Signal(str)

    def __init__(self, parent: QWidget | None = None, flags: Qt.WindowFlags | Qt.WindowType = Qt.WindowFlags()) -> None:
        super().__init__(parent, flags)
        self.init()

    def init(self):
        self.createCentralWidget()
        self.createDockWidgets()

        self.scene = self.gallery_view.scene()
        self.gallery_view.show()
        self.gallery_view.galleryItemDoubleClicked.connect(self._galleryItemDoubleClicked)

        self.initSignals()


    def initSignals(self):
        self.gallery_image_info.add_folder_btn.clicked.connect(self.openFolder)
        self.gallery_image_info.add_image_btn.clicked.connect(self.openImage)

    @Slot()
    def openImage(self):
        file_path, _ = QFileDialog().getOpenFileName(self, "选择图片加入项目", "/home",  image_suffixs_filter)
        if file_path != "" and self.project.images_path.get(file_path) is None:
            self.project.addImage(file_path)
            self.gallery_view.addImage(GalleryItem(file_path))
            self.gallery_view.update()

    @Slot()
    def openFolder(self):
        filter = ["*.png", "*.jpg", "*.bmp"]
        folder = QFileDialog().getExistingDirectory(self, 
                                                    "选择目录加入项目", 
                                                    "/home", 
                                                    QFileDialog.Option.ShowDirsOnly | QFileDialog.Option.DontResolveSymlinks)
        if folder != "":
            LOGGER.debug(f"folder: {folder}")
            for fname in QDirIterator(folder, 
                                      filter, 
                                      QDir.Filter.NoDotAndDotDot | QDir.Filter.Files | QDir.Filter.NoSymLinks, 
                                      QDirIterator.IteratorFlag.Subdirectories):
                LOGGER.debug(fname)

    @Slot(str)
    def _galleryItemDoubleClicked(self, image_path : str) -> None:
        self.galleryItemDoubleClicked.emit(image_path)


    def createCentralWidget(self):
        gallery_widget = QWidget(self)
        gallery_widget_layout = QGridLayout()
        gallery_widget_layout.setContentsMargins(0,0,0,0)
        gallery_widget.setLayout(gallery_widget_layout)
        self.gallery_view = GalleryView(gallery_widget)
        gallery_widget_layout.addWidget(self.gallery_view)
        self.setCentralWidget(gallery_widget)

    def setProject(self, project : Project):
        self.project = project
        self.project_dock.setWidget(ProjectInfo(project.name, project.type))


    def createDockWidgets(self):
        empty_widget = QWidget(self)
        # line = HLine(self)
        # line.setLineWidth(2)

        self.project_dock = QDockWidget(self.tr("项目"), self)
        self.project_dock.setTitleBarWidget(empty_widget)
        self.project_dock.setFeatures(QDockWidget.DockWidgetMovable)
        self.project_dock.setWidget(ProjectInfo("示例项目", "目标检测"))
        self.project_dock.setContentsMargins(0,0,0,0)
        self.project_dock.setMinimumWidth(120)
        self.project_dock.setMaximumWidth(500)
        self.project_dock.setFixedHeight(100)
        self.addDockWidget(Qt.DockWidgetArea.LeftDockWidgetArea, self.project_dock)


        self.file_dock = QDockWidget(self.tr(""), self)
        # self.file_dock.setTitleBarWidget(line)
        self.file_dock.setFeatures(QDockWidget.DockWidgetMovable)
        self.gallery_image_info = GalleryImageInfo(self)
        self.gallery_image_info.setMinimumHeight(40)
        self.gallery_image_info.setSizePolicy(QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Minimum)
        self.file_dock.setWidget(self.gallery_image_info)
        self.file_dock.setMinimumHeight(40)
        self.file_dock.setSizePolicy(QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Minimum)
        self.addDockWidget(Qt.DockWidgetArea.LeftDockWidgetArea, self.file_dock)

        # self.file_dock = QDockWidget(self.tr("图像"), self)
        # self.select_dock = QDockWidget(self.tr("已选择"), self)
        self.split_mapping_dock = QDockWidget(self.tr("拆分映射"), self)
        self.split_mapping_dock.setFeatures(QDockWidget.NoDockWidgetFeatures)
        widget = QWidget(self)
        widget.setSizePolicy(QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Expanding)
        self.split_mapping_dock.setWidget(widget)
        self.split_mapping_dock.setMinimumHeight(100)
        self.split_mapping_dock.setSizePolicy(QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Expanding)
        self.addDockWidget(Qt.DockWidgetArea.LeftDockWidgetArea, self.split_mapping_dock)

        
        