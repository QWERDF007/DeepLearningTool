import functools

from qtpy.QtCore import Qt
from qtpy.QtCore import Signal, Slot
from qtpy import QtWidgets

from deep_learning_tool import __appname__
from deep_learning_tool import utils
# from deep_learning_tool.widgets import ToolBar
from deep_learning_tool.widgets import GalleryWindow, LabelWindow
from deep_learning_tool.data import Project

class MainWindow(QtWidgets.QMainWindow):
    def __init__(self, parent: QtWidgets.QWidget | None = None, flags: Qt.WindowFlags | Qt.WindowType = Qt.WindowType.Window) -> None:
        super().__init__()
        self.resize(1600, 1000)

        self.project = Project("示例项目", "目标检测")

        self.tab_widget = QtWidgets.QTabWidget(self)
        self.tab_widget.tabBar().hide()
        self.setCentralWidget(self.tab_widget)
        layout = QtWidgets.QGridLayout()
        layout.setContentsMargins(0,0,0,0)
        self.tab_widget.setLayout(layout)

        self.gallery_window = GalleryWindow(self)
        self.gallery_window.setProject(self.project)
        self.label_window = LabelWindow(self)
        self.label_window.setProject(self.project)


        self.gallery_window.galleryItemDoubleClicked.connect(self.setLabelImage)

        self.tab_widget.addTab(self.gallery_window, '图库')
        self.tab_widget.addTab(self.label_window, '标注')
        

        # for child in self.tab_widget.findChildren(QtWidgets.QTabBar):
        #     child.hide()

        self.actions = utils.struct(
            menu=(),
            tools=(),
        )

        self.tools = self.toolbar("工具")

        self.createTools()

    @Slot(str)
    def setLabelImage(self, image_path):
        # 要先切换widget显示，后面的view的fitInView才正常
        self.setCurrentTab(self.label_window)
        self.label_window.setLabelImage(image_path)

    def toolbar(self, title, actions=None):
        toolbar = QtWidgets.QToolBar(title, self)
        toolbar.setFloatable(False)
        toolbar.setMovable(False)
        
        # toolbar.setObjectName("%sToolBar" % title)
        # # toolbar.setOrientation(Qt.Vertical)
        # toolbar.setToolButtonStyle(Qt.ToolButtonTextUnderIcon)
        # if actions:
        #     utils.addActions(toolbar, actions)
        self.addToolBar(Qt.ToolBarArea.TopToolBarArea, toolbar)
        return toolbar

    
    def createTools(self):
        action = functools.partial(utils.newAction, self)
        project = action(
            self.tr("项目"),
            None,
            shortcut=None,
            icon=None,
            tip=None,
            enabled=True,
        )

        gallery = action(
            self.tr("图库"),
            lambda: self.setCurrentTab(self.gallery_window),
            shortcut=None,
            icon=None,
            tip=None,
            enabled=True,
        )

        label = action(
            self.tr("标注"),
            lambda: (self.setCurrentTab(self.label_window), self.label_window.initLabelImage()),
            shortcut=None,
            icon=None,
            tip=None,
            enabled=True,
        )


        self.actions.tools = (
            project,
            gallery,
            label,
        )

        self.tools.addActions(self.actions.tools)

    
    def setCurrentTab(self, widget):
        self.tab_widget.setCurrentIndex(self.tab_widget.indexOf(widget))

