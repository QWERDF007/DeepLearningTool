import os

from qtpy.QtCore import Qt, Signal, Slot
from qtpy.QtCore import QSize, QRectF, QPointF

from qtpy.QtWidgets import QWidget, QMenu
from qtpy.QtWidgets import QGraphicsScene, QGraphicsItem, QGraphicsView, QGraphicsTextItem, QStyleOptionGraphicsItem
from qtpy.QtWidgets import QGraphicsSceneMouseEvent

from qtpy.QtGui import QPixmap, QPainter, QPainterPath, QColor, QFontMetrics
from qtpy.QtGui import QMouseEvent, QResizeEvent, QWheelEvent, QKeyEvent, QContextMenuEvent

from deep_learning_tool import LOGGER
from deep_learning_tool.utils import newIcon, newAction




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
        
