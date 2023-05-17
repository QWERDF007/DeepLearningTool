import typing

from qtpy.QtCore import Qt, QLineF, QRectF, QPointF, QRect

from qtpy.QtWidgets import QWidget, QListWidget, QListWidgetItem


from qtpy.QtCore import QEvent
from qtpy.QtGui import QKeyEvent

from deep_learning_tool.utils import newPixmap
from deep_learning_tool import LOGGER

class UniqueLabelListWidget(QListWidget):
    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)


    def findItemByLabel(self, label):
        for row in range(self.count()):
            item = self.item(row)
            if item.data(Qt.UserRole) == label:
                return item


    def mousePressEvent(self, event):
        if not self.indexAt(event.pos()).isValid():
            self.clearSelection()
        return super().mousePressEvent(event)
    
    def createItemFromLabel(self, label):
        if self.findItemByLabel(label):
            raise ValueError(
                "Item for label '{}' already exists".format(label)
            )

        item = QListWidgetItem()
        item.setData(Qt.UserRole, label)
        return item


    def keyPressEvent(self, event: QKeyEvent) -> None:
        if event.key() == Qt.Key_Escape:
            self.clearSelection()
        return super().keyPressEvent(event)


