

from qtpy.QtCore import Qt, QSize
from qtpy.QtWidgets import QWidget, QPushButton, QLabel, QSpacerItem

from qtpy.QtWidgets import QHBoxLayout, QVBoxLayout

from qtpy.QtWidgets import QSizePolicy

from deep_learning_tool.utils import newIcon


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




