import functools

from qtpy.QtCore import Qt
from qtpy.QtCore import Signal, Slot
from qtpy.QtWidgets import QMainWindow, QWidget, QTabWidget, QToolBar, QAction
from qtpy.QtWidgets import QGridLayout

from deep_learning_tool import __appname__
from deep_learning_tool import LOGGER
from deep_learning_tool import utils
# from deep_learning_tool.widgets import ToolBar
from deep_learning_tool.widgets import GalleryWindow, LabelWindow
from deep_learning_tool.data import Project

class MainWindow(QMainWindow):
    def __init__(self, parent: QWidget | None = None, flags: Qt.WindowFlags | Qt.WindowType = Qt.WindowType.Window) -> None:
        super().__init__()
        self.resize(1600, 1000)

        self.project = Project("示例项目", "目标检测")

        self.tab_widget = QTabWidget(self)
        self.tab_widget.tabBar().hide()
        self.setCentralWidget(self.tab_widget)
        layout = QGridLayout()
        layout.setContentsMargins(0,0,0,0)
        self.tab_widget.setLayout(layout)
        
        self.project_window = QWidget(self)
        self.gallery_window = GalleryWindow(self)
        self.gallery_window.setProject(self.project)
        self.label_window = LabelWindow(self)
        self.label_window.setProject(self.project)
        self.review_window = QWidget(self)
        self.split_window = QWidget(self)
        self.training_window = QWidget(self)
        self.evaluation_window = QWidget(self)
        self.export_window = QWidget(self)


        self.gallery_window.galleryItemDoubleClicked.connect(self.setLabelImage)

        self.tab_widget.addTab(self.project_window, '项目')
        self.tab_widget.addTab(self.gallery_window, '图库')
        self.tab_widget.addTab(self.label_window, '标注')
        self.tab_widget.addTab(self.review_window, '检查')
        self.tab_widget.addTab(self.split_window, '拆分')
        self.tab_widget.addTab(self.training_window, '训练')
        self.tab_widget.addTab(self.evaluation_window, '评估')
        self.tab_widget.addTab(self.export_window, '导出')

        self.actions = utils.struct(
            menu=(),
            tools=(),
        )

        self.tools = self.toolbar("工具")
        self.createTools()

        self.setCurrentTab(self.gallery_window, self.actions.tools[1])

        LOGGER.debug(f"{self.tab_widget.currentIndex()}")

    @Slot(str)
    def setLabelImage(self, image_path):
        # 要先切换widget显示，后面的view的fitInView才正常
        self.setCurrentTab(self.label_window)
        self.label_window.setLabelImage(image_path)

    def toolbar(self, title, actions=None):
        toolbar = QToolBar(title, self)
        toolbar.setFloatable(False)
        toolbar.setMovable(False)
        toolbar.setFixedHeight(48)
        self.addToolBar(Qt.ToolBarArea.TopToolBarArea, toolbar)
        return toolbar

    
    def createTools(self):
        action = functools.partial(utils.newAction, self)
        project = action(
            self.tr("项目"),
            lambda: self.setCurrentTab(self.project_window, project),
            shortcut=None,
            icon=None,
            tip=None,
            enabled=True,
            checkable=True,
        )

        gallery = action(
            self.tr("图库"),
            lambda: self.setCurrentTab(self.gallery_window, gallery),
            shortcut=None,
            icon=None,
            tip=None,
            enabled=True,
            checkable=True,
        )

        label = action(
            self.tr("标注"),
            lambda: self.setCurrentTab(self.label_window, label),
            shortcut=None,
            icon=None,
            tip=None,
            enabled=True,
            checkable=True,
        )

        review = action(
            self.tr("检查"),
            lambda: self.setCurrentTab(self.review_window, review),
            shortcut=None,
            icon=None,
            tip=None,
            enabled=True,
            checkable=True,
        )

        split = action(
            self.tr("拆分"),
            lambda: self.setCurrentTab(self.split_window, split),
            shortcut=None,
            icon=None,
            tip=None,
            enabled=True,
            checkable=True,
        )

        training = action(
            self.tr("训练"),
            lambda: self.setCurrentTab(self.training_window, training),
            shortcut=None,
            icon=None,
            tip=None,
            enabled=True,
            checkable=True,
        )

        evaluation = action(
            self.tr("评估"),
            lambda: self.setCurrentTab(self.evaluation_window, evaluation),
            shortcut=None,
            icon=None,
            tip=None,
            enabled=True,
            checkable=True,
        )

        export = action(
            self.tr("导出"),
            lambda: self.setCurrentTab(self.export_window, export),
            shortcut=None,
            icon=None,
            tip=None,
            enabled=True,
            checkable=True,
        )


        self.actions.tools = (
            project,
            gallery,
            label,
            review,
            split,
            training,
            evaluation,
            export,
        )

        self.tools.addActions(self.actions.tools)

    
    def setCurrentTab(self, widget : QWidget, action : QAction = None):
        LOGGER.debug(f"set tab {widget.__class__}")
        index = self.tab_widget.indexOf(widget)
        if index != self.tab_widget.currentIndex():
            self.tab_widget.setCurrentIndex(index)
            if widget == self.label_window and self.label_window.label_image is None:
                self.label_window.initLabelImage()
        if action != None:
            for tool in self.actions.tools:
                if tool != action:
                    tool.setChecked(False)
            if not action.isChecked():
                action.setChecked(True)
