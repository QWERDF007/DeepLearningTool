from qtpy.QtCore import Qt, QSize
from qtpy.QtWidgets import QSizePolicy
from qtpy.QtWidgets import QWidget, QFrame, QLabel
from qtpy.QtWidgets import QHBoxLayout, QVBoxLayout

from deep_learning_tool.utils import newIcon

class HLine(QFrame):
    def __init__(self, parent: QWidget | None = None, flags: Qt.WindowFlags | Qt.WindowType = Qt.WindowFlags()) -> None:
        super().__init__(parent, flags)
        self.setFrameShape(QFrame.Shape.HLine)

class VLine(QFrame):
    def __init__(self, parent: QWidget | None = None, flags: Qt.WindowFlags | Qt.WindowType = Qt.WindowFlags()) -> None:
        super().__init__(parent, flags)
        self.setFrameShape(QFrame.Shape.VLine)


class ProjectInfo(QWidget):
    def __init__(self, project_name, project_type, icon=None, icon_size=QSize(32,32)) -> None:
        super().__init__()
        self.project_name = QLabel(project_name, self)
        self.project_name.setSizePolicy(QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Minimum)
        self.project_type = QLabel(project_type, self)
        self.project_type.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Minimum)
        self.setIcon(icon, icon_size)

        line = QFrame(self)
        line.setFrameShape(QFrame.Shape.HLine) 
        hlayout = QHBoxLayout()
        hlayout.setContentsMargins(0,0,0,0)
        hlayout.addWidget(self.icon)
        hlayout.addWidget(self.project_type)

        vlayout = QVBoxLayout()
        vlayout.setContentsMargins(0,0,0,0)
        vlayout.addWidget(self.project_name)
        vlayout.addWidget(line)
        vlayout.addLayout(hlayout)

        self.setLayout(vlayout)
        
        self.setSizePolicy(QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Minimum)


    def setIcon(self, icon, icon_size):
        self.icon = QLabel("", self)
        if icon is None:
            icon = newIcon("objects")
            self.icon.setPixmap(icon.pixmap(icon_size))
        else:
            pass
        